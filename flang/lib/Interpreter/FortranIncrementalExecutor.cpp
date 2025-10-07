//===--- FortranIncrementalExecutor.cpp - Fortran Incremental Executor -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "flang/Interpreter/FortranIncrementalExecutor.h"
#include "flang/Interpreter/FortranPartialTranslationUnit.h"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/LLVMContext.h"
#include <cstdio>

using namespace flang;

// Stub implementations for Fortran runtime functions
extern "C" {
  void _FortranAProgramStart(int argc, char** argv, char** envp, void* unused) {
    // Stub implementation - do nothing
  }
  
  void _FortranAProgramEndStatement() {
    // Stub implementation - do nothing
  }
  
  void _FortranACUFInit() {
    // Stub implementation - do nothing
  }
  
  void _QMprifPprif_init(void* unused) {
    // Stub implementation - do nothing
  }
}

FortranIncrementalExecutor::FortranIncrementalExecutor(std::unique_ptr<llvm::orc::LLJIT> JIT)
    : J(std::move(JIT)) {}

llvm::Expected<llvm::orc::LLJIT &> FortranIncrementalExecutor::GetExecutionEngine() {
  return *J;
}

llvm::Error FortranIncrementalExecutor::addModule(FortranPartialTranslationUnit &FPTU) {
  if (!FPTU.TheModule) {
    return llvm::make_error<llvm::StringError>("No LLVM module available in FPTU", std::error_code());
  }
  
  // Initialize LLVM targets if not already done
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();
  
  // Register Fortran runtime stub functions with the JIT
  if (auto Err = registerFortranRuntimeStubs()) {
    return Err;
  }
  
  // Create a resource tracker for this FPTU
  llvm::orc::ResourceTrackerSP RT = J->getMainJITDylib().createResourceTracker();
  ResourceTrackers[&FPTU] = RT;
  
  // Create a ThreadSafeModule from the LLVM module
  auto TSM = llvm::orc::ThreadSafeModule(std::move(FPTU.TheModule), 
                                         std::make_unique<llvm::LLVMContext>());
  
  // Add the module to the JIT with the resource tracker
  if (auto Err = J->addIRModule(RT, std::move(TSM))) {
    return Err;
  }
  
  return llvm::Error::success();
}

llvm::Error FortranIncrementalExecutor::removeModule(FortranPartialTranslationUnit &FPTU) {
  llvm::orc::ResourceTrackerSP RT = std::move(ResourceTrackers[&FPTU]);
  if (!RT)
    return llvm::Error::success();

  ResourceTrackers.erase(&FPTU);
  if (llvm::Error Err = RT->remove())
    return Err;
  return llvm::Error::success();
}

llvm::Error FortranIncrementalExecutor::registerFortranRuntimeStubs() {
  // Get the main JITDylib to register symbols
  auto &MainJD = J->getMainJITDylib();
  
  // Create a symbol map for the Fortran runtime stubs
  llvm::orc::SymbolMap RuntimeSymbols;
  
  // Register _FortranAProgramStart
  auto ProgramStartAddr = llvm::pointerToJITTargetAddress(_FortranAProgramStart);
  RuntimeSymbols[J->mangleAndIntern("_FortranAProgramStart")] = 
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr(ProgramStartAddr), llvm::JITSymbolFlags::Exported);
  
  // Register _FortranAProgramEndStatement
  auto ProgramEndAddr = llvm::pointerToJITTargetAddress(_FortranAProgramEndStatement);
  RuntimeSymbols[J->mangleAndIntern("_FortranAProgramEndStatement")] = 
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr(ProgramEndAddr), llvm::JITSymbolFlags::Exported);
  
  // Register _FortranACUFInit
  auto CUFInitAddr = llvm::pointerToJITTargetAddress(_FortranACUFInit);
  RuntimeSymbols[J->mangleAndIntern("_FortranACUFInit")] = 
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr(CUFInitAddr), llvm::JITSymbolFlags::Exported);
  
  // Register _QMprifPprif_init
  auto PrifInitAddr = llvm::pointerToJITTargetAddress(_QMprifPprif_init);
  RuntimeSymbols[J->mangleAndIntern("_QMprifPprif_init")] = 
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr(PrifInitAddr), llvm::JITSymbolFlags::Exported);
  
  // Define the symbols in the main JITDylib
  // If symbols already exist, this will fail, but we can ignore that error
  if (auto Err = MainJD.define(llvm::orc::absoluteSymbols(RuntimeSymbols))) {
    // Check if the error is due to duplicate symbols - if so, ignore it
    std::string errorMsg = llvm::toString(std::move(Err));
    if (errorMsg.find("duplicate definition") == std::string::npos) {
      return llvm::make_error<llvm::StringError>(errorMsg, std::error_code());
    }
    // If it's a duplicate definition error, we can safely ignore it
  }
  
  return llvm::Error::success();
}

llvm::Error FortranIncrementalExecutor::runCtors() {
  // Look for and call the Fortran main function if it exists
  // Try the mangled name first (_QQmain), then fall back to fortran_main
  auto MainSymbol = J->lookup("_QQmain");
  if (!MainSymbol) {
    // Try the old name for backward compatibility
    MainSymbol = J->lookup("fortran_main");
    if (!MainSymbol) {
      // If neither exists, that's okay - some modules might not have a main function
      return llvm::Error::success();
    }
  }
  
  // Cast the symbol to a function pointer and call it
  auto MainFunc = MainSymbol->toPtr<void()>();
  if (!MainFunc) {
    return llvm::make_error<llvm::StringError>("Fortran main function is not callable", std::error_code());
  }
  
  // Call the main function
  MainFunc();
  
  return llvm::Error::success();
}

llvm::Error FortranIncrementalExecutor::cleanUp() {
  // Don't destroy the JIT instance - let global destructors handle it
  // This avoids hanging during program termination
  return llvm::Error::success();
}

llvm::Expected<llvm::orc::ExecutorAddr> FortranIncrementalExecutor::getSymbolAddress(llvm::StringRef Name) const {
  using namespace llvm::orc;
  auto SO = makeJITDylibSearchOrder({&J->getMainJITDylib(),
                                     J->getPlatformJITDylib().get(),
                                     J->getProcessSymbolsJITDylib().get()});

  ExecutionSession &ES = J->getExecutionSession();

  auto SymOrErr = ES.lookup(SO, J->mangleAndIntern(Name));
  if (auto Err = SymOrErr.takeError())
    return std::move(Err);
  return SymOrErr->getAddress();
}

uint32_t FortranIncrementalExecutor::getOutOfProcessChildPid() const { 
  return 0; 
}
