// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/thread_pool/delayed_task_manager.h"

#include <algorithm>

#include "base/bind.h"
#include "base/check.h"
#include "base/feature_list.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_features.h"
#include "base/task/task_runner.h"
#include "base/task/thread_pool/task.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {
namespace internal {

DelayedTaskManager::DelayedTask::DelayedTask() = default;

DelayedTaskManager::DelayedTask::DelayedTask(
    Task task,
    PostTaskNowCallback callback,
    scoped_refptr<TaskRunner> task_runner)
    : task(std::move(task)),
      callback(std::move(callback)),
      task_runner(std::move(task_runner)) {}

DelayedTaskManager::DelayedTask::DelayedTask(
    DelayedTaskManager::DelayedTask&& other) = default;

DelayedTaskManager::DelayedTask::~DelayedTask() = default;

DelayedTaskManager::DelayedTask& DelayedTaskManager::DelayedTask::operator=(
    DelayedTaskManager::DelayedTask&& other) = default;

bool DelayedTaskManager::DelayedTask::operator>(
    const DelayedTask& other) const {
  TimeTicks latest_delayed_run_time = task.latest_delayed_run_time();
  TimeTicks other_latest_delayed_run_time =
      other.task.latest_delayed_run_time();
  return std::tie(latest_delayed_run_time, task.sequence_num) >
         std::tie(other_latest_delayed_run_time, other.task.sequence_num);
}

DelayedTaskManager::DelayedTaskManager(const TickClock* tick_clock)
    : process_ripe_tasks_closure_(
          BindRepeating(&DelayedTaskManager::ProcessRipeTasks,
                        Unretained(this))),
      schedule_process_ripe_tasks_closure_(BindRepeating(
          &DelayedTaskManager::ScheduleProcessRipeTasksOnServiceThread,
          Unretained(this))),
      tick_clock_(tick_clock) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  DCHECK(tick_clock_);
}

DelayedTaskManager::~DelayedTaskManager() {
  delayed_task_handle_.CancelTask();
}

void DelayedTaskManager::Start(
    scoped_refptr<SequencedTaskRunner> service_thread_task_runner) {
  DCHECK(service_thread_task_runner);

  TimeTicks process_ripe_tasks_time;
  subtle::DelayPolicy delay_policy;
  {
    CheckedAutoLock auto_lock(queue_lock_);
    DCHECK(!service_thread_task_runner_);
    service_thread_task_runner_ = std::move(service_thread_task_runner);
    align_wake_ups_ = FeatureList::IsEnabled(kAlignWakeUps);
    task_leeway_ = kTaskLeewayParam.Get();
    std::tie(process_ripe_tasks_time, delay_policy) =
        GetTimeAndDelayPolicyToScheduleProcessRipeTasksLockRequired();
  }
  if (!process_ripe_tasks_time.is_max()) {
    service_thread_task_runner_->PostTask(FROM_HERE,
                                          schedule_process_ripe_tasks_closure_);
  }
}

void DelayedTaskManager::AddDelayedTask(
    Task task,
    PostTaskNowCallback post_task_now_callback,
    scoped_refptr<TaskRunner> task_runner) {
  DCHECK(task.task);
  DCHECK(!task.delayed_run_time.is_null());

  // Use CHECK instead of DCHECK to crash earlier. See http://crbug.com/711167
  // for details.
  CHECK(task.task);
  TimeTicks process_ripe_tasks_time;
  subtle::DelayPolicy delay_policy;
  {
    CheckedAutoLock auto_lock(queue_lock_);
    auto [old_process_ripe_tasks_time, old_delay_policy] =
        GetTimeAndDelayPolicyToScheduleProcessRipeTasksLockRequired();
    pending_high_res_task_count_ +=
        (task.delay_policy == subtle::DelayPolicy::kPrecise);
    delayed_task_queue_.insert(DelayedTask(std::move(task),
                                           std::move(post_task_now_callback),
                                           std::move(task_runner)));
    // Not started yet.
    if (service_thread_task_runner_ == nullptr)
      return;

    std::tie(process_ripe_tasks_time, delay_policy) =
        GetTimeAndDelayPolicyToScheduleProcessRipeTasksLockRequired();
    // The next invocation of ProcessRipeTasks() doesn't need to change.
    if (old_process_ripe_tasks_time == process_ripe_tasks_time &&
        old_delay_policy == delay_policy) {
      return;
    }
  }
  if (!process_ripe_tasks_time.is_max()) {
    service_thread_task_runner_->PostTask(FROM_HERE,
                                          schedule_process_ripe_tasks_closure_);
  }
}

void DelayedTaskManager::ProcessRipeTasks() {
  std::vector<DelayedTask> ripe_delayed_tasks;
  TimeTicks process_ripe_tasks_time;

  {
    CheckedAutoLock auto_lock(queue_lock_);
    const TimeTicks now = tick_clock_->NowTicks();
    // A delayed task is ripe if it reached its delayed run time or if it is
    // canceled. If it is canceled, schedule its deletion on the correct
    // sequence now rather than in the future, to minimize CPU wake ups and save
    // power.
    while (!delayed_task_queue_.empty() &&
           (delayed_task_queue_.top().task.earliest_delayed_run_time() <= now ||
            !delayed_task_queue_.top().task.task.MaybeValid())) {
      // The const_cast on top is okay since the DelayedTask is
      // transactionally being popped from |delayed_task_queue_| right after
      // and the move doesn't alter the sort order.
      ripe_delayed_tasks.push_back(
          std::move(const_cast<DelayedTask&>(delayed_task_queue_.top())));
      pending_high_res_task_count_ -=
          (delayed_task_queue_.top().task.delay_policy ==
           subtle::DelayPolicy::kPrecise);
      DCHECK_GE(pending_high_res_task_count_, 0);
      delayed_task_queue_.pop();
    }
    std::tie(process_ripe_tasks_time, std::ignore) =
        GetTimeAndDelayPolicyToScheduleProcessRipeTasksLockRequired();
  }
  if (!process_ripe_tasks_time.is_max()) {
    if (service_thread_task_runner_->RunsTasksInCurrentSequence()) {
      ScheduleProcessRipeTasksOnServiceThread();
    } else {
      // ProcessRipeTasks may be called on another thread under tests.
      service_thread_task_runner_->PostTask(
          FROM_HERE, schedule_process_ripe_tasks_closure_);
    }
  }

  for (auto& delayed_task : ripe_delayed_tasks) {
    std::move(delayed_task.callback).Run(std::move(delayed_task.task));
  }
}

absl::optional<TimeTicks> DelayedTaskManager::NextScheduledRunTime() const {
  CheckedAutoLock auto_lock(queue_lock_);
  if (delayed_task_queue_.empty())
    return absl::nullopt;
  return delayed_task_queue_.top().task.delayed_run_time;
}

bool DelayedTaskManager::HasPendingHighResolutionTasksForTesting() const {
  CheckedAutoLock auto_lock(queue_lock_);
  return pending_high_res_task_count_;
}

std::pair<TimeTicks, subtle::DelayPolicy> DelayedTaskManager::
    GetTimeAndDelayPolicyToScheduleProcessRipeTasksLockRequired() {
  queue_lock_.AssertAcquired();
  if (delayed_task_queue_.empty()) {
    return std::make_pair(TimeTicks::Max(),
                          subtle::DelayPolicy::kFlexibleNoSooner);
  }

  const DelayedTask& ripest_delayed_task = delayed_task_queue_.top();
  subtle::DelayPolicy delay_policy =
      pending_high_res_task_count_ ? subtle::DelayPolicy::kPrecise
                                   : ripest_delayed_task.task.delay_policy;

  TimeTicks delayed_run_time = ripest_delayed_task.task.delayed_run_time;
  if (align_wake_ups_) {
    TimeTicks aligned_run_time =
        ripest_delayed_task.task.earliest_delayed_run_time().SnappedToNextTick(
            TimeTicks(), task_leeway_);
    delayed_run_time = std::min(
        aligned_run_time, ripest_delayed_task.task.latest_delayed_run_time());
  }
  return std::make_pair(delayed_run_time, delay_policy);
}

void DelayedTaskManager::ScheduleProcessRipeTasksOnServiceThread() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  TimeTicks process_ripe_tasks_time;
  subtle::DelayPolicy delay_policy;
  {
    CheckedAutoLock auto_lock(queue_lock_);
    std::tie(process_ripe_tasks_time, delay_policy) =
        GetTimeAndDelayPolicyToScheduleProcessRipeTasksLockRequired();
  }
  DCHECK(!process_ripe_tasks_time.is_null());
  if (process_ripe_tasks_time.is_max())
    return;
  delayed_task_handle_.CancelTask();
  delayed_task_handle_ =
      service_thread_task_runner_->PostCancelableDelayedTaskAt(
          subtle::PostDelayedTaskPassKey(), FROM_HERE,
          process_ripe_tasks_closure_, process_ripe_tasks_time, delay_policy);
}

}  // namespace internal
}  // namespace base
