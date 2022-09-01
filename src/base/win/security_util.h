// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_WIN_SECURITY_UTIL_H_
#define BASE_WIN_SECURITY_UTIL_H_

#include <vector>

#include "base/base_export.h"
#include "base/win/sid.h"
#include "base/win/windows_types.h"

namespace base {

class FilePath;

namespace win {

// Adds allowed ACE entries to a file or directory |path| from a list of SIDs
// with allowed |access_mask| and |inheritance| flags. If |path| is a directory
// and |recursive| is true then any inheritable ACEs granted will be propagated
// to its children.
BASE_EXPORT bool GrantAccessToPath(const FilePath& path,
                                   const std::vector<Sid>& sids,
                                   DWORD access_mask,
                                   DWORD inheritance,
                                   bool recursive = true);

// Adds deny ACE entries to a file or directory |path| from a list of SIDs with
// allowed |access_mask| and |inheritance| flags. If |path| is a directory and
// |recursive| is true then any inheritable ACEs granted will be propagated to
// its children.
BASE_EXPORT bool DenyAccessToPath(const FilePath& path,
                                  const std::vector<Sid>& sids,
                                  DWORD access_mask,
                                  DWORD inheritance,
                                  bool recursive = true);

// Clone a vector of Sids.
BASE_EXPORT std::vector<Sid> CloneSidVector(const std::vector<Sid>& sids);

// Append a vector of Sids to an existing vector.
BASE_EXPORT void AppendSidVector(std::vector<Sid>& base_sids,
                                 const std::vector<Sid>& append_sids);

}  // namespace win
}  // namespace base

#endif  // BASE_WIN_SECURITY_UTIL_H_
