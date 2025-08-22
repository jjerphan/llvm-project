//===-- ClangUtilityFunction.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangUtilityFunction.h"
#include "ClangExpressionDeclMap.h"
#include "ClangExpressionParser.h"
#include "ClangExpressionSourceCode.h"
#include "ClangPersistentVariables.h"

#include <cstdio>
#include <sys/types.h>


#include "lldb/Core/Module.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Host/Host.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb_private;

char ClangUtilityFunction::ID;

ClangUtilityFunction::ClangUtilityFunction(ExecutionContextScope &exe_scope,
                                           std::string text, std::string name,
                                           bool enable_debugging)
    : UtilityFunction(
          exe_scope,
          std::string(ClangExpressionSourceCode::g_expression_prefix) + text +
              std::string(ClangExpressionSourceCode::g_expression_suffix),
          std::move(name), enable_debugging) {
  printf("[UTILITY_FUNCTION] ClangUtilityFunction constructor called for: %s\n", name.c_str());
  printf("[UTILITY_FUNCTION] Text length: %zu, enable_debugging: %s\n", text.length(), enable_debugging ? "true" : "false");
  
  // Write the source code to a file so that LLDB's source manager can display
  // it when debugging the code.
  if (enable_debugging) {
    printf("[UTILITY_FUNCTION] Creating temporary source file for debugging...\n");
    int temp_fd = -1;
    llvm::SmallString<128> result_path;
    llvm::sys::fs::createTemporaryFile("lldb", "expr", temp_fd, result_path);
    if (temp_fd != -1) {
      printf("[UTILITY_FUNCTION] Temporary file created: %s\n", result_path.c_str());
      lldb_private::NativeFile file(temp_fd, File::eOpenOptionWriteOnly, true);
      text = "#line 1 \"" + std::string(result_path) + "\"\n" + text;
      size_t bytes_written = text.size();
      file.Write(text.c_str(), bytes_written);
      if (bytes_written == text.size()) {
        // If we successfully wrote the source to a temporary file, replace the
        // function text with the next text containing the line directive.
        m_function_text =
            std::string(ClangExpressionSourceCode::g_expression_prefix) + text +
            std::string(ClangExpressionSourceCode::g_expression_suffix);
        printf("[UTILITY_FUNCTION] SUCCESS: Source file written and function text updated\n");
      } else {
        printf("[UTILITY_FUNCTION] WARNING: Failed to write complete source file\n");
      }
      file.Close();
    } else {
      printf("[UTILITY_FUNCTION] WARNING: Failed to create temporary file\n");
    }
  } else {
    printf("[UTILITY_FUNCTION] Debugging disabled, no temporary file created\n");
  }
  printf("[UTILITY_FUNCTION] Constructor completed for: %s\n", name.c_str());
}

ClangUtilityFunction::~ClangUtilityFunction() {
  printf("[UTILITY_FUNCTION] ClangUtilityFunction destructor called for: %s\n", FunctionName());
  if (m_jit_start_addr != LLDB_INVALID_ADDRESS) {
    printf("[UTILITY_FUNCTION] JIT address was: 0x%llx\n", (unsigned long long)m_jit_start_addr);
  }
  printf("[UTILITY_FUNCTION] Destructor completed\n");
}

/// Install the utility function into a process
///
/// \param[in] diagnostic_manager
///     A diagnostic manager to report errors and warnings to.
///
/// \param[in] exe_ctx
///     The execution context to install the utility function to.
///
/// \return
///     True on success (no errors); false otherwise.
bool ClangUtilityFunction::Install(DiagnosticManager &diagnostic_manager,
                                   ExecutionContext &exe_ctx) {
  printf("[UTILITY_FUNCTION] ClangUtilityFunction::Install called for: %s\n", FunctionName());
  
  if (m_jit_start_addr != LLDB_INVALID_ADDRESS) {
    printf("[UTILITY_FUNCTION] FAILED: Already installed at address 0x%llx\n", (unsigned long long)m_jit_start_addr);
    diagnostic_manager.PutString(lldb::eSeverityWarning, "already installed");
    return false;
  }
  printf("[UTILITY_FUNCTION] Not previously installed, proceeding with installation\n");

  ////////////////////////////////////
  // Set up the target and compiler
  //
  printf("[UTILITY_FUNCTION] Setting up target and compiler...\n");

  Target *target = exe_ctx.GetTargetPtr();

  if (!target) {
    printf("[UTILITY_FUNCTION] FAILED: Invalid target\n");
    diagnostic_manager.PutString(lldb::eSeverityError, "invalid target");
    return false;
  }
  printf("[UTILITY_FUNCTION] Target is valid\n");

  Process *process = exe_ctx.GetProcessPtr();

  if (!process) {
    printf("[UTILITY_FUNCTION] FAILED: Invalid process\n");
    diagnostic_manager.PutString(lldb::eSeverityError, "invalid process");
    return false;
  }
  printf("[UTILITY_FUNCTION] Process is valid (ID: %lu)\n", (unsigned long)process->GetID());

  // Since we might need to call allocate memory and maybe call code to make
  // the caller, we need to be stopped.
  printf("[UTILITY_FUNCTION] Checking process state...\n");
  if (process->GetState() != lldb::eStateStopped) {
    printf("[UTILITY_FUNCTION] FAILED: Process is running (state: %d)\n", process->GetState());
    diagnostic_manager.PutString(lldb::eSeverityError, "process running");
    return false;
  }
  printf("[UTILITY_FUNCTION] Process is stopped, proceeding\n");
  //////////////////////////
  // Parse the expression
  //
  printf("[UTILITY_FUNCTION] Starting expression parsing...\n");

  bool keep_result_in_memory = false;

  printf("[UTILITY_FUNCTION] Resetting declaration map...\n");
  ResetDeclMap(exe_ctx, keep_result_in_memory);

  if (!DeclMap()->WillParse(exe_ctx, nullptr)) {
    printf("[UTILITY_FUNCTION] FAILED: Current process state unsuitable for expression parsing\n");
    diagnostic_manager.PutString(
        lldb::eSeverityError,
        "current process state is unsuitable for expression parsing");
    return false;
  }
  printf("[UTILITY_FUNCTION] Declaration map ready for parsing\n");

  const bool generate_debug_info = true;
  printf("[UTILITY_FUNCTION] Creating Clang expression parser (debug info: %s)...\n", generate_debug_info ? "enabled" : "disabled");
  ClangExpressionParser parser(exe_ctx.GetBestExecutionContextScope(), *this,
                               generate_debug_info);

  printf("[UTILITY_FUNCTION] Parsing expression...\n");
  unsigned num_errors = parser.Parse(diagnostic_manager);

  if (num_errors) {
    printf("[UTILITY_FUNCTION] FAILED: Parse failed with %u errors\n", num_errors);
    ResetDeclMap();

    return false;
  }
  printf("[UTILITY_FUNCTION] SUCCESS: Expression parsed successfully\n");

  //////////////////////////////////
  // JIT the output of the parser
  //
  printf("[UTILITY_FUNCTION] Starting JIT compilation...\n");

  bool can_interpret = false; // should stay that way
  printf("[UTILITY_FUNCTION] Can interpret: %s\n", can_interpret ? "true" : "false");

  printf("[UTILITY_FUNCTION] Preparing for execution...\n");
  Status jit_error = parser.PrepareForExecution(
      m_jit_start_addr, m_jit_end_addr, m_execution_unit_sp, exe_ctx,
      can_interpret, eExecutionPolicyAlways);

  if (m_jit_start_addr != LLDB_INVALID_ADDRESS) {
    printf("[UTILITY_FUNCTION] SUCCESS: JIT compilation succeeded, start address: 0x%llx\n", (unsigned long long)m_jit_start_addr);
    m_jit_process_wp = process->shared_from_this();
    if (parser.GetGenerateDebugInfo()) {
      printf("[UTILITY_FUNCTION] Setting up JIT module with debug info...\n");
      lldb::ModuleSP jit_module_sp(m_execution_unit_sp->GetJITModule());

      if (jit_module_sp) {
        ConstString const_func_name(FunctionName());
        FileSpec jit_file;
        jit_file.SetFilename(const_func_name);
        jit_module_sp->SetFileSpecAndObjectName(jit_file, ConstString());
        m_jit_module_wp = jit_module_sp;
        target->GetImages().Append(jit_module_sp);
        printf("[UTILITY_FUNCTION] JIT module added to target images\n");
      } else {
        printf("[UTILITY_FUNCTION] WARNING: No JIT module available\n");
      }
    } else {
      printf("[UTILITY_FUNCTION] No debug info generated\n");
    }
  } else {
    printf("[UTILITY_FUNCTION] WARNING: JIT start address is invalid\n");
  }

  printf("[UTILITY_FUNCTION] Finalizing installation...\n");
  DeclMap()->DidParse();

  ResetDeclMap();

  if (jit_error.Success()) {
    printf("[UTILITY_FUNCTION] SUCCESS: Installation completed successfully\n");
    return true;
  } else {
    const char *error_cstr = jit_error.AsCString();
    if (error_cstr && error_cstr[0]) {
      printf("[UTILITY_FUNCTION] FAILED: JIT error: %s\n", error_cstr);
      diagnostic_manager.Printf(lldb::eSeverityError, "%s", error_cstr);
    } else {
      printf("[UTILITY_FUNCTION] FAILED: Expression can't be interpreted or run\n");
      diagnostic_manager.PutString(lldb::eSeverityError,
                                   "expression can't be interpreted or run");
    }
    return false;
  }
}

char ClangUtilityFunction::ClangUtilityFunctionHelper::ID;

void ClangUtilityFunction::ClangUtilityFunctionHelper::ResetDeclMap(
    ExecutionContext &exe_ctx, bool keep_result_in_memory) {
  printf("[UTILITY_FUNCTION] ResetDeclMap called (keep_result_in_memory: %s)\n", keep_result_in_memory ? "true" : "false");
  
  std::shared_ptr<ClangASTImporter> ast_importer;
  printf("[UTILITY_FUNCTION] Getting persistent expression state for C language...\n");
  auto *state = exe_ctx.GetTargetSP()->GetPersistentExpressionStateForLanguage(
      lldb::eLanguageTypeC);
  if (state) {
    printf("[UTILITY_FUNCTION] Got persistent state, extracting AST importer...\n");
    auto *persistent_vars = llvm::cast<ClangPersistentVariables>(state);
    ast_importer = persistent_vars->GetClangASTImporter();
    printf("[UTILITY_FUNCTION] AST importer extracted successfully\n");
  } else {
    printf("[UTILITY_FUNCTION] WARNING: No persistent state available for C language\n");
  }
  
  printf("[UTILITY_FUNCTION] Creating new ClangExpressionDeclMap...\n");
  m_expr_decl_map_up = std::make_unique<ClangExpressionDeclMap>(
      keep_result_in_memory, nullptr, exe_ctx.GetTargetSP(), ast_importer,
      nullptr);
  printf("[UTILITY_FUNCTION] SUCCESS: Declaration map reset completed\n");
}
