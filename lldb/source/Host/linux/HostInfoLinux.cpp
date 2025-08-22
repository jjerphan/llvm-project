//===-- HostInfoLinux.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/linux/HostInfoLinux.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/Support/Threading.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <sys/utsname.h>
#include <unistd.h>
#include <sys/stat.h>

#include <algorithm>
#include <mutex>
#include <optional>

using namespace lldb_private;

namespace {
struct HostInfoLinuxFields {
  llvm::once_flag m_distribution_once_flag;
  std::string m_distribution_id;
  llvm::once_flag m_os_version_once_flag;
  llvm::VersionTuple m_os_version;
};
} // namespace

static HostInfoLinuxFields *g_fields = nullptr;

void HostInfoLinux::Initialize(SharedLibraryDirectoryHelper *helper) {
  HostInfoPosix::Initialize(helper);

  g_fields = new HostInfoLinuxFields();
}

void HostInfoLinux::Terminate() {
  assert(g_fields && "Missing call to Initialize?");
  delete g_fields;
  g_fields = nullptr;
  HostInfoBase::Terminate();
}

std::vector<std::string> HostInfoLinux::GetSwiftLibrarySearchPaths() {
  std::vector<std::string> paths;

  // Add printf-style logging that will definitely show up
  printf("[HOSTINFO_LINUX] ========================================\n");
  printf("[HOSTINFO_LINUX] GetSwiftLibrarySearchPaths called\n");

  // Add logging to see what paths we're providing
  printf("[HOSTINFO_LINUX] GetSwiftLibrarySearchPaths called\n");
  
  // Add paths from the user's specific build directory
  // These paths are based on the user's build setup
  paths.push_back("/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/swift-linux-x86_64/lib/swift/linux");
  paths.push_back("/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/swift-linux-x86_64/lib/swift/linux/x86_64");
  paths.push_back("/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/lldb-linux-x86_64/lib/lldb/swift/linux");
  paths.push_back("/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/lldb-linux-x86_64/lib/lldb/swift/linux/x86_64");
  
  // Add Foundation and other framework library paths
  paths.push_back("/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/foundation-linux-x86_64/lib");
  paths.push_back("/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/libdispatch-linux-x86_64");
  paths.push_back("/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/xctest-linux-x86_64");
  
  // Add Swift compiler library paths that may contain additional runtime components
  paths.push_back("/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/swift-linux-x86_64/lib");

  // Add common Linux Swift library paths
  paths.push_back("/usr/lib/swift");
  paths.push_back("/usr/lib/swift/linux");
  
  // Add x86_64 specific paths
  paths.push_back("/usr/lib/swift/linux/x86_64");
  
  // Add paths from environment variables if they exist
  const char *swift_library_path = getenv("SWIFT_LIBRARY_PATH");
  if (swift_library_path) {
    paths.push_back(swift_library_path);
    printf("[HOSTINFO_LINUX] Added SWIFT_LIBRARY_PATH: %s\n", swift_library_path);
  }
  
  const char *swift_root = getenv("SWIFT_ROOT");
  if (swift_root) {
    std::string swift_lib_path = std::string(swift_root) + "/lib/swift/linux";
    paths.push_back(swift_lib_path);
    printf("[HOSTINFO_LINUX] Added SWIFT_ROOT derived path: %s\n", swift_lib_path.c_str());
  }
  
  const char *swift_framework_path = getenv("SWIFT_FRAMEWORK_PATH");
  if (swift_framework_path) {
    paths.push_back(swift_framework_path);
    printf("[HOSTINFO_LINUX] Added SWIFT_FRAMEWORK_PATH: %s\n", swift_framework_path);
  }
  
  const char *ld_library_path = getenv("LD_LIBRARY_PATH");
  if (ld_library_path) {
    // Split LD_LIBRARY_PATH by colon and add each path
    std::string ld_paths(ld_library_path);
    size_t pos = 0;
    while ((pos = ld_paths.find(':')) != std::string::npos) {
      std::string path = ld_paths.substr(0, pos);
      if (!path.empty()) {
        paths.push_back(path);
        printf("[HOSTINFO_LINUX] Added LD_LIBRARY_PATH component: %s\n", path.c_str());
      }
      ld_paths.erase(0, pos + 1);
    }
    if (!ld_paths.empty()) {
      paths.push_back(ld_paths);
      printf("[HOSTINFO_LINUX] Added LD_LIBRARY_PATH component: %s\n", ld_paths.c_str());
    }
  }
  
  printf("[HOSTINFO_LINUX] GetSwiftLibrarySearchPaths returning %zu paths\n", paths.size());
  for (size_t i = 0; i < paths.size(); i++) {
    const std::string &path = paths[i];
    printf("[HOSTINFO_LINUX]   Path %zu: %s\n", i, path.c_str());

    // Check if the path exists and log what's in it
    if (access(path.c_str(), F_OK) == 0) {
      printf("[HOSTINFO_LINUX]     Path exists: %s\n", path.c_str());

      // Check for common Swift library files
      std::vector<std::string> swift_libs = {"libswiftCore.so", "libswiftRuntime.so", "libswiftSwiftOnoneSupport.so"};
      for (const std::string &lib : swift_libs) {
        std::string lib_path = path + "/" + lib;
        if (access(lib_path.c_str(), F_OK) == 0) {
          printf("[HOSTINFO_LINUX]       Found Swift library: %s\n", lib.c_str());
        }
      }
    } else {
      printf("[HOSTINFO_LINUX]     Path does not exist: %s\n", path.c_str());
    }
  }

  printf("[HOSTINFO_LINUX] ========================================\n");

  return paths;
}

llvm::VersionTuple HostInfoLinux::GetOSVersion() {
  assert(g_fields && "Missing call to Initialize?");
  llvm::call_once(g_fields->m_os_version_once_flag, []() {
    struct utsname un;
    if (uname(&un) != 0)
      return;

    llvm::StringRef release = un.release;
    // The kernel release string can include a lot of stuff (e.g.
    // 4.9.0-6-amd64). We're only interested in the numbered prefix.
    release = release.substr(0, release.find_first_not_of("0123456789."));
    g_fields->m_os_version.tryParse(release);
  });

  return g_fields->m_os_version;
}

std::optional<std::string> HostInfoLinux::GetOSBuildString() {
  struct utsname un;
  ::memset(&un, 0, sizeof(utsname));

  if (uname(&un) < 0)
    return std::nullopt;

  return std::string(un.release);
}

llvm::StringRef HostInfoLinux::GetDistributionId() {
  assert(g_fields && "Missing call to Initialize?");
  // Try to run 'lbs_release -i', and use that response for the distribution
  // id.
  llvm::call_once(g_fields->m_distribution_once_flag, []() {
    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOGF(log, "attempting to determine Linux distribution...");

    // check if the lsb_release command exists at one of the following paths
    const char *const exe_paths[] = {"/bin/lsb_release",
                                     "/usr/bin/lsb_release"};

    for (size_t exe_index = 0;
         exe_index < sizeof(exe_paths) / sizeof(exe_paths[0]); ++exe_index) {
      const char *const get_distribution_info_exe = exe_paths[exe_index];
      if (access(get_distribution_info_exe, F_OK)) {
        // this exe doesn't exist, move on to next exe
        LLDB_LOGF(log, "executable doesn't exist: %s",
                  get_distribution_info_exe);
        continue;
      }

      // execute the distribution-retrieval command, read output
      std::string get_distribution_id_command(get_distribution_info_exe);
      get_distribution_id_command += " -i";

      FILE *file = popen(get_distribution_id_command.c_str(), "r");
      if (!file) {
        LLDB_LOGF(log,
                  "failed to run command: \"%s\", cannot retrieve "
                  "platform information",
                  get_distribution_id_command.c_str());
        break;
      }

      // retrieve the distribution id string.
      char distribution_id[256] = {'\0'};
      if (fgets(distribution_id, sizeof(distribution_id) - 1, file) !=
          nullptr) {
        LLDB_LOGF(log, "distribution id command returned \"%s\"",
                  distribution_id);

        const char *const distributor_id_key = "Distributor ID:\t";
        if (strstr(distribution_id, distributor_id_key)) {
          // strip newlines
          std::string id_string(distribution_id + strlen(distributor_id_key));
          llvm::erase(id_string, '\n');

          // lower case it and convert whitespace to underscores
          std::transform(
              id_string.begin(), id_string.end(), id_string.begin(),
              [](char ch) { return tolower(isspace(ch) ? '_' : ch); });

          g_fields->m_distribution_id = id_string;
          LLDB_LOGF(log, "distribution id set to \"%s\"",
                    g_fields->m_distribution_id.c_str());
        } else {
          LLDB_LOGF(log, "failed to find \"%s\" field in \"%s\"",
                    distributor_id_key, distribution_id);
        }
      } else {
        LLDB_LOGF(log,
                  "failed to retrieve distribution id, \"%s\" returned no"
                  " lines",
                  get_distribution_id_command.c_str());
      }

      // clean up the file
      pclose(file);
    }
  });

  return g_fields->m_distribution_id;
}

FileSpec HostInfoLinux::GetProgramFileSpec() {
  static FileSpec g_program_filespec;

  if (!g_program_filespec) {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
      exe_path[len] = 0;
      g_program_filespec.SetFile(exe_path, FileSpec::Style::native);
    }
  }

  return g_program_filespec;
}

bool HostInfoLinux::ComputeSupportExeDirectory(FileSpec &file_spec) {
  if (HostInfoPosix::ComputeSupportExeDirectory(file_spec) &&
      file_spec.IsAbsolute() && FileSystem::Instance().Exists(file_spec))
    return true;
  file_spec.SetDirectory(GetProgramFileSpec().GetDirectory());
  return !file_spec.GetDirectory().IsEmpty();
}

bool HostInfoLinux::ComputeSystemPluginsDirectory(FileSpec &file_spec) {
  FileSpec temp_file("/usr/" LLDB_INSTALL_LIBDIR_BASENAME "/lldb/plugins");
  FileSystem::Instance().Resolve(temp_file);
  file_spec.SetDirectory(temp_file.GetPath());
  return true;
}

bool HostInfoLinux::ComputeUserPluginsDirectory(FileSpec &file_spec) {
  // XDG Base Directory Specification
  // http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html If
  // XDG_DATA_HOME exists, use that, otherwise use ~/.local/share/lldb.
  const char *xdg_data_home = getenv("XDG_DATA_HOME");
  if (xdg_data_home && xdg_data_home[0]) {
    std::string user_plugin_dir(xdg_data_home);
    user_plugin_dir += "/lldb";
    file_spec.SetDirectory(user_plugin_dir.c_str());
  } else
    file_spec.SetDirectory("~/.local/share/lldb");
  return true;
}

void HostInfoLinux::ComputeHostArchitectureSupport(ArchSpec &arch_32,
                                                   ArchSpec &arch_64) {
  HostInfoPosix::ComputeHostArchitectureSupport(arch_32, arch_64);

  // On Linux, "unknown" in the vendor slot isn't what we want for the default
  // triple.  It's probably an artifact of config.guess.
  if (arch_32.IsValid()) {
    if (arch_32.GetTriple().getVendor() == llvm::Triple::UnknownVendor)
      arch_32.GetTriple().setVendorName(llvm::StringRef());
  }
  if (arch_64.IsValid()) {
    if (arch_64.GetTriple().getVendor() == llvm::Triple::UnknownVendor)
      arch_64.GetTriple().setVendorName(llvm::StringRef());
  }
}
