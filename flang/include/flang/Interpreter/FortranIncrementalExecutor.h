//===--- FortranIncrementalExecutor.h - Fortran Incremental Executor ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the FortranIncrementalExecutor class which handles
// JIT execution of Fortran code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FLANG_INTERPRETER_FORTRANINCREMENTALEXECUTOR_H
#define LLVM_FLANG_INTERPRETER_FORTRANINCREMENTALEXECUTOR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>

namespace flang {

struct FortranPartialTranslationUnit;

/// Handles JIT execution of Fortran partial translation units.
class FortranIncrementalExecutor {
  std::unique_ptr<llvm::orc::LLJIT> J;
  
  /// Resource trackers for each FPTU to enable selective removal
  llvm::DenseMap<const FortranPartialTranslationUnit *, llvm::orc::ResourceTrackerSP> ResourceTrackers;

public:
  FortranIncrementalExecutor(std::unique_ptr<llvm::orc::LLJIT> JIT);
  
  /// Get the underlying execution engine.
  llvm::Expected<llvm::orc::LLJIT &> GetExecutionEngine();
  
  /// Add a module to the JIT for execution.
  llvm::Error addModule(FortranPartialTranslationUnit &FPTU);
  
  /// Remove a module from the JIT.
  llvm::Error removeModule(FortranPartialTranslationUnit &FPTU);
  
  /// Register Fortran runtime stub functions with the JIT.
  llvm::Error registerFortranRuntimeStubs();
  
  /// Run constructors for the added modules.
  llvm::Error runCtors();
  
  /// Clean up resources.
  llvm::Error cleanUp();
  
  /// Get symbol address by name.
  llvm::Expected<llvm::orc::ExecutorAddr> getSymbolAddress(llvm::StringRef Name) const;
  
  /// Get the out-of-process child PID (if applicable).
  uint32_t getOutOfProcessChildPid() const;
};

} // namespace flang

#endif // LLVM_FLANG_INTERPRETER_FORTRANINCREMENTALEXECUTOR_H
