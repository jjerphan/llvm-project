//===--- FortranInterpreter.h - Incremental Fortran Execution ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the FortranInterpreter class which orchestrates the
// incremental execution of Fortran code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FLANG_INTERPRETER_FORTRANINTERPRETER_H
#define LLVM_FLANG_INTERPRETER_FORTRANINTERPRETER_H

#include "flang/Interpreter/FortranPartialTranslationUnit.h"
#include "flang/Interpreter/FortranIncrementalParser.h"
#include "flang/Interpreter/FortranIncrementalExecutor.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace llvm {
namespace orc {
class LLJIT;
class LLJITBuilder;
class ThreadSafeContext;
class ExecutorProcessControl;
} // namespace orc
} // namespace llvm

namespace flang {

/// Main interpreter class for incremental Fortran execution.
class FortranInterpreter {
  std::unique_ptr<llvm::orc::ThreadSafeContext> TSCtx;
  std::unique_ptr<FortranIncrementalParser> Parser;
  std::unique_ptr<FortranIncrementalExecutor> Executor;

  std::list<FortranPartialTranslationUnit> FPTUs;
  unsigned InitFPTUSize = 0;

  std::unique_ptr<llvm::orc::LLJITBuilder> JITBuilder;

public:
  struct JITConfig {
    bool IsOutOfProcess;
    std::string OOPExecutor;
    std::string OOPExecutorConnect;
    bool UseSharedMemory;
    unsigned SlabAllocateSize;
    std::string OrcRuntimePath;
    uint32_t ExecutorPID;
    std::function<void()> CustomizeFork;
    
    JITConfig() : IsOutOfProcess(false), OOPExecutor(""), OOPExecutorConnect(""), 
                  UseSharedMemory(false), SlabAllocateSize(0), OrcRuntimePath(""), 
                  ExecutorPID(0), CustomizeFork(nullptr) {}
  };

  static llvm::Expected<std::unique_ptr<FortranInterpreter>>
  create(JITConfig Config = JITConfig{});

  ~FortranInterpreter();

  llvm::Expected<FortranPartialTranslationUnit &>
  Parse(llvm::StringRef Code);

  llvm::Error Execute(FortranPartialTranslationUnit &FPTU);

  llvm::Error ParseAndExecute(llvm::StringRef Code);

  /// Undo N previous incremental inputs.
  llvm::Error Undo(unsigned N = 1);

  /// Get symbol address by name.
  llvm::Expected<llvm::orc::ExecutorAddr> getSymbolAddress(llvm::StringRef Name) const;

  /// Get the effective number of FPTUs (excluding runtime code).
  size_t getEffectiveFPTUSize() const;

  llvm::Expected<llvm::orc::LLJIT &> getExecutionEngine();

  uint32_t getOutOfProcessExecutorPID() const;

private:
  FortranInterpreter(llvm::Error &Err,
                     std::unique_ptr<llvm::orc::LLJITBuilder> JB,
                     JITConfig Config);

  llvm::Error CreateExecutor(JITConfig Config = JITConfig{});
  void ResetExecutor();
  void markUserCodeStart();
};

} // namespace flang

#endif // LLVM_FLANG_INTERPRETER_FORTRANINTERPRETER_H