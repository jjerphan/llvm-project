//===-- SwiftHost.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"
#include "lldb/Host/common/HostInfoSwift.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include <string>
#include <cstdlib>

using namespace lldb_private;

bool HostInfoPosix::ComputeSwiftResourceDirectory(FileSpec &lldb_shlib_spec,
                                                  FileSpec &file_spec,
                                                  bool verify) {
  return DefaultComputeSwiftResourceDirectory(lldb_shlib_spec, file_spec,
                                              verify);
}

FileSpec HostInfoPosix::GetSwiftResourceDir() {
  static std::once_flag g_once_flag;
  static FileSpec g_swift_resource_dir;
  std::call_once(g_once_flag, []() {
    // First check if a custom Swift resource directory is specified via environment variable
    const char *custom_swift_dir = getenv("SWIFT_RESOURCE_DIR");
    if (custom_swift_dir && custom_swift_dir[0] != '\0') {
      // Use the custom Swift resource directory
      g_swift_resource_dir.SetDirectory(custom_swift_dir);
      FileSystem::Instance().Resolve(g_swift_resource_dir);
      
      Log *log = GetLog(LLDBLog::Host);
      if (log) {
        LLDB_LOG(log, "Using custom Swift resource directory: '{0}'", g_swift_resource_dir);
      }
      return;
    }
    
    // Fall back to the default computation using LLDB's own directory
    FileSpec lldb_file_spec = HostInfoPosix::GetShlibDir();
    HostInfoPosix::ComputeSwiftResourceDirectory(lldb_file_spec,
                                                 g_swift_resource_dir, true);
    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOG(log, "swift dir -> '{0}'", g_swift_resource_dir);
  });
  return g_swift_resource_dir;
}

std::string
HostInfoPosix::GetSwiftResourceDir(llvm::Triple triple,
                                   llvm::StringRef platform_sdk_path) {
  return GetSwiftResourceDir().GetPath();
}
