//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_GENERATE_H
#define _LIBCPP___ALGORITHM_RANGES_GENERATE_H

#include <__algorithm/generate.h>
#include <__algorithm/make_projected.h>
#include <__concepts/constructible.h>
#include <__concepts/invocable.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__utility/forward.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER > 17 && !defined(_LIBCPP_HAS_NO_INCOMPLETE_RANGES)

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
namespace __generate {

struct __fn {

  template <input_or_output_iterator _OutIter, sentinel_for<_OutIter> _Sent, copy_constructible _Func>
  requires invocable<_Func&> && indirectly_writable<_OutIter, invoke_result_t<_Func&>>
  _LIBCPP_HIDE_FROM_ABI constexpr
  _OutIter operator()(_OutIter __first, _Sent __last, _Func __gen) const {
    // TODO: implement
    (void)__first; (void)__last; (void)__gen;
    return {};
  }

  template <class _Range, copy_constructible _Func>
  requires invocable<_Func&> && output_range<_Range, invoke_result_t<_Func&>>
  _LIBCPP_HIDE_FROM_ABI constexpr
  borrowed_iterator_t<_Range> operator()(_Range&& __range, _Func __gen) const {
    // TODO: implement
    (void)__range; (void)__gen;
    return {};
  }

};

} // namespace __generate

inline namespace __cpo {
  inline constexpr auto generate = __generate::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER > 17 && !defined(_LIBCPP_HAS_NO_INCOMPLETE_RANGES)

#endif // _LIBCPP___ALGORITHM_RANGES_GENERATE_H
