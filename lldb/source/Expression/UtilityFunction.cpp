//===-- UtilityFunction.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <sys/types.h>

#include "lldb/Core/Module.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Host/Host.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"

using namespace lldb_private;
using namespace lldb;

char UtilityFunction::ID;

/// Constructor
///
/// \param[in] text
///     The text of the function.  Must be a full translation unit.
///
/// \param[in] name
///     The name of the function, as used in the text.
UtilityFunction::UtilityFunction(ExecutionContextScope &exe_scope,
                                 std::string text, std::string name,
                                 bool enable_debugging)
    : Expression(exe_scope), m_execution_unit_sp(), m_jit_module_wp(),
      m_function_text(std::move(text)), m_function_name(std::move(name)) {
  printf("[UTILITY_FUNCTION_BASE] UtilityFunction constructor called for: %s\n", name.c_str());
  printf("[UTILITY_FUNCTION_BASE] Text length: %zu, enable_debugging: %s\n", text.length(), enable_debugging ? "true" : "false");
  printf("[UTILITY_FUNCTION_BASE] Constructor completed\n");
}

UtilityFunction::~UtilityFunction() {
  printf("[UTILITY_FUNCTION_BASE] UtilityFunction destructor called for: %s\n", m_function_name.c_str());
  lldb::ProcessSP process_sp(m_jit_process_wp.lock());
  if (process_sp) {
    lldb::ModuleSP jit_module_sp(m_jit_module_wp.lock());
    if (jit_module_sp) {
      printf("[UTILITY_FUNCTION_BASE] Removing JIT module from target images\n");
      process_sp->GetTarget().GetImages().Remove(jit_module_sp);
    }
  }
  printf("[UTILITY_FUNCTION_BASE] Destructor completed\n");
}

// FIXME: We should check that every time this is called it is called with the
// same return type & arguments...

FunctionCaller *UtilityFunction::MakeFunctionCaller(
    const CompilerType &return_type, const ValueList &arg_value_list,
    lldb::ThreadSP thread_to_use_sp, Status &error) {
  printf("[UTILITY_FUNCTION_BASE] MakeFunctionCaller called for: %s\n", m_function_name.c_str());
  
  if (m_caller_up) {
    printf("[UTILITY_FUNCTION_BASE] Function caller already exists, returning existing one\n");
    return m_caller_up.get();
  }

  printf("[UTILITY_FUNCTION_BASE] Creating new function caller...\n");
  ProcessSP process_sp = m_jit_process_wp.lock();
  if (!process_sp) {
    printf("[UTILITY_FUNCTION_BASE] FAILED: No process available\n");
    error = Status::FromErrorString(
        "Can't make a function caller without a process.");
    return nullptr;
  }
  printf("[UTILITY_FUNCTION_BASE] Process available (ID: %lu)\n", (unsigned long)process_sp->GetID());
  // Since we might need to allocate memory and maybe call code to make
  // the caller, we need to be stopped.
  printf("[UTILITY_FUNCTION_BASE] Checking process state...\n");
  if (process_sp->GetState() != lldb::eStateStopped) {
    printf("[UTILITY_FUNCTION_BASE] FAILED: Process is running (state: %d)\n", process_sp->GetState());
    error = Status::FromErrorStringWithFormatv(
        "Can't make a function caller while the process is {0}: the process "
        "must be stopped to allocate memory.",
        StateAsCString(process_sp->GetState()));
    return nullptr;
  }
  printf("[UTILITY_FUNCTION_BASE] Process is stopped, proceeding\n");

  printf("[UTILITY_FUNCTION_BASE] Setting up function caller address...\n");
  Address impl_code_address;
  impl_code_address.SetOffset(StartAddress());
  std::string name(m_function_name);
  name.append("-caller");
  printf("[UTILITY_FUNCTION_BASE] Function caller name: %s\n", name.c_str());

  printf("[UTILITY_FUNCTION_BASE] Getting function caller for language...\n");
  m_caller_up.reset(process_sp->GetTarget().GetFunctionCallerForLanguage(
      Language().AsLanguageType(), return_type, impl_code_address,
      arg_value_list, name.c_str(), error));
  if (error.Fail()) {
    printf("[UTILITY_FUNCTION_BASE] FAILED: Could not get function caller: %s\n", error.AsCString());
    return nullptr;
  }
  printf("[UTILITY_FUNCTION_BASE] Function caller obtained successfully\n");

  if (m_caller_up) {
    printf("[UTILITY_FUNCTION_BASE] Compiling function caller...\n");
    DiagnosticManager diagnostics;

    unsigned num_errors =
        m_caller_up->CompileFunction(thread_to_use_sp, diagnostics);
    if (num_errors) {
      printf("[UTILITY_FUNCTION_BASE] FAILED: Function caller compilation failed with %u errors\n", num_errors);
      error = Status::FromError(diagnostics.GetAsError(
          lldb::eExpressionParseError,
          "Error compiling " + m_function_name + " caller function:"));

      m_caller_up.reset();
      return nullptr;
    }
    printf("[UTILITY_FUNCTION_BASE] SUCCESS: Function caller compiled successfully\n");

    printf("[UTILITY_FUNCTION_BASE] Writing function wrapper...\n");
    diagnostics.Clear();
    ExecutionContext exe_ctx(process_sp);

    if (!m_caller_up->WriteFunctionWrapper(exe_ctx, diagnostics)) {
      printf("[UTILITY_FUNCTION_BASE] FAILED: Could not write function wrapper\n");
      error = Status::FromError(diagnostics.GetAsError(
          lldb::eExpressionSetupError,
          "Error inserting " + m_function_name + " caller function:"));
      m_caller_up.reset();
      return nullptr;
    }
    printf("[UTILITY_FUNCTION_BASE] SUCCESS: Function wrapper written successfully\n");
  } else {
    printf("[UTILITY_FUNCTION_BASE] WARNING: No function caller available\n");
  }
  
  printf("[UTILITY_FUNCTION_BASE] SUCCESS: MakeFunctionCaller completed for: %s\n", m_function_name.c_str());
  return m_caller_up.get();
}
