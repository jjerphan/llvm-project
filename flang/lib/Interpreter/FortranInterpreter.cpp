//===--- FortranInterpreter.cpp - Incremental Fortran Execution ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "flang/Interpreter/FortranInterpreter.h"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/Support/Error.h"

using namespace flang;

static llvm::Expected<std::unique_ptr<llvm::orc::LLJITBuilder>>
createDefaultJITBuilder() {
  auto JB = std::make_unique<llvm::orc::LLJITBuilder>();
  return std::move(JB);
}

FortranInterpreter::FortranInterpreter(llvm::Error &Err,
                                       std::unique_ptr<llvm::orc::LLJITBuilder> JB,
                                       JITConfig /*Config*/)
    : JITBuilder(std::move(JB)) {
  (void)Err;
  Parser = std::make_unique<FortranIncrementalParser>();
}

FortranInterpreter::~FortranInterpreter() {
  Parser.reset();
  if (Executor) {
    if (llvm::Error E = Executor->cleanUp())
      llvm::consumeError(std::move(E));
  }
}

llvm::Expected<std::unique_ptr<FortranInterpreter>>
FortranInterpreter::create(JITConfig Config) {
  llvm::Error Err = llvm::Error::success();
  auto JBOrErr = createDefaultJITBuilder();
  if (!JBOrErr)
    return JBOrErr.takeError();
  auto I = std::unique_ptr<FortranInterpreter>(
      new FortranInterpreter(Err, std::move(*JBOrErr), Config));
  if (Err)
    return std::move(Err);
  I->markUserCodeStart();
  return std::move(I);
}

llvm::Expected<llvm::orc::LLJIT &> FortranInterpreter::getExecutionEngine() {
  if (!Executor) {
    if (auto Err = CreateExecutor())
      return std::move(Err);
  }
  return Executor->GetExecutionEngine();
}

llvm::Error FortranInterpreter::CreateExecutor(JITConfig /*Config*/) {
  if (Executor)
    return llvm::make_error<llvm::StringError>("Executor exists", std::error_code());
  auto JBOrErr = createDefaultJITBuilder();
  if (!JBOrErr)
    return JBOrErr.takeError();
  auto JITOrErr = std::move(*JBOrErr)->create();
  if (!JITOrErr)
    return JITOrErr.takeError();
  Executor = std::make_unique<FortranIncrementalExecutor>(std::move(*JITOrErr));
  return llvm::Error::success();
}

void FortranInterpreter::ResetExecutor() { Executor.reset(); }

void FortranInterpreter::markUserCodeStart() { InitFPTUSize = FPTUs.size(); }

size_t FortranInterpreter::getEffectiveFPTUSize() const {
  return FPTUs.size() - InitFPTUSize;
}

llvm::Expected<FortranPartialTranslationUnit &>
FortranInterpreter::Parse(llvm::StringRef Code) {
  auto parseResult = Parser->parse(Code);
  if (!parseResult) {
    return parseResult.takeError();
  }
  
  // Generate LLVM IR for the parsed program
  if (auto err = Parser->generateLLVMIR(*parseResult)) {
    return std::move(err);
  }
  
  // Add the FPTU to the persistent list
  FPTUs.push_back(std::move(*parseResult));
  return FPTUs.back();
}

llvm::Error FortranInterpreter::Execute(FortranPartialTranslationUnit &FPTU) {
  if (!Executor) {
    if (auto Err = CreateExecutor())
      return Err;
  }
  if (auto Err = Executor->addModule(FPTU))
    return Err;
  if (auto Err = Executor->runCtors())
    return Err;
  return llvm::Error::success();
}

llvm::Error FortranInterpreter::ParseAndExecute(llvm::StringRef Code) {
  auto FPTU = Parse(Code);
  if (!FPTU)
    return FPTU.takeError();
  return Execute(*FPTU);
}

llvm::Error FortranInterpreter::Undo(unsigned N) {
  if (getEffectiveFPTUSize() == 0) {
    return llvm::make_error<llvm::StringError>("Operation failed. "
                                               "No input left to undo",
                                               std::error_code());
  } else if (N > getEffectiveFPTUSize()) {
    return llvm::make_error<llvm::StringError>(
        llvm::formatv(
            "Operation failed. Wanted to undo {0} inputs, only have {1}.", N,
            getEffectiveFPTUSize()),
        std::error_code());
  }

  for (unsigned I = 0; I < N; I++) {
    if (Executor) {
      if (llvm::Error Err = Executor->removeModule(FPTUs.back()))
        return Err;
    }
    FPTUs.pop_back();
  }
  return llvm::Error::success();
}

llvm::Expected<llvm::orc::ExecutorAddr> FortranInterpreter::getSymbolAddress(llvm::StringRef Name) const {
  if (!Executor)
    return llvm::make_error<llvm::StringError>("Operation failed. "
                                               "No execution engine",
                                               std::error_code());
  return Executor->getSymbolAddress(Name);
}

uint32_t FortranInterpreter::getOutOfProcessExecutorPID() const {
  if (Executor)
    return Executor->getOutOfProcessChildPid();
  return 0;
}