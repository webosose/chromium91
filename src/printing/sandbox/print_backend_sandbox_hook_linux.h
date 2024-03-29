// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_SANDBOX_PRINT_BACKEND_SANDBOX_HOOK_LINUX_H_
#define PRINTING_SANDBOX_PRINT_BACKEND_SANDBOX_HOOK_LINUX_H_

#include "printing/printing_export.h"
#include "sandbox/policy/linux/sandbox_linux.h"

namespace printing {

// Setup allowed commands and filesystem permissions for print backend service
// sandboxed process.
PRINTING_EXPORT bool PrintBackendPreSandboxHook(
    sandbox::policy::SandboxLinux::Options options);

}  // namespace printing

#endif  // PRINTING_SANDBOX_PRINT_BACKEND_SANDBOX_HOOK_LINUX_H_
