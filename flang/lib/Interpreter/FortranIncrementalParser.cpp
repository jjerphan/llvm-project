//===--- FortranIncrementalParser.cpp - Fortran Incremental Parser ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "flang/Interpreter/FortranIncrementalParser.h"
#include "flang/Interpreter/FortranPartialTranslationUnit.h"

#include "llvm/Support/Error.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

// Flang lowering pipeline includes
#include "flang/Optimizer/Passes/Pipelines.h"
#include "flang/Optimizer/Dialect/Support/FIRContext.h"
#include "flang/Optimizer/Dialect/Support/KindMapping.h"
#include "flang/Optimizer/Support/DataLayout.h"
#include "flang/Optimizer/Support/InitFIR.h"
#include "flang/Optimizer/Support/Utils.h"
#include "flang/Support/default-kinds.h"
#include "flang/Tools/CrossToolHelpers.h"
#include "flang/Optimizer/Passes/Pipelines.h"

// Additional includes for semantic analysis and lowering
#include "flang/Lower/Bridge.h"
#include "flang/Semantics/semantics.h"
#include "flang/Support/LangOptions.h"
#include "flang/Support/Fortran-features.h"
#include "flang/Lower/EnvironmentDefault.h"
#include "flang/Frontend/TargetOptions.h"
#include "flang/Frontend/CodeGenOptions.h"

// Parsing includes
#include "flang/Parser/parsing.h"
#include "flang/Parser/options.h"
#include "flang/Parser/provenance.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Import.h"
#include "mlir/Target/LLVMIR/ModuleTranslation.h"
#include <cstring>

using namespace flang;


llvm::Expected<FortranPartialTranslationUnit>
FortranIncrementalParser::parse(llvm::StringRef Code) {
  // Create a new partial translation unit
  FortranPartialTranslationUnit FPTU;
  
  // Set up basic parsing options
  Fortran::parser::Options parserOptions;
  parserOptions.isFixedForm = false; // Use free-form Fortran
  parserOptions.predefinitions.emplace_back("__FLANG", "1");
  
  // Create language options
  Fortran::common::LanguageFeatureControl languageFeatures;
  
  // Create AllSources and AllCookedSources in the parser instance
  if (!AllSources) {
    AllSources = std::make_unique<Fortran::parser::AllSources>();
    AllCookedSources = std::make_unique<Fortran::parser::AllCookedSources>(*AllSources);
  }
  
  // Prepare the source content
  std::string sourceContent = Code.str();
  
  // Ensure the source ends with a newline (Fortran requirement)
  if (sourceContent.empty() || sourceContent.back() != '\n') {
    sourceContent += '\n';
  }
  
  // Create parsing context
  Fortran::parser::Parsing parsing{*AllCookedSources};

  // Use llvm::MemoryBuffer approach similar to Clang's IncrementalParser
  // Create an uninitialized memory buffer, copy code in and append "\n"
  size_t InputSize = sourceContent.size(); // don't include trailing 0
  // MemBuffer size should *not* include terminating zero
  std::unique_ptr<llvm::MemoryBuffer> MB(
      llvm::WritableMemoryBuffer::getNewUninitMemBuffer(InputSize + 1,
                                                        "input_line_" + std::to_string(InputCount++)));
  char *MBStart = const_cast<char *>(MB->getBufferStart());
  memcpy(MBStart, sourceContent.c_str(), InputSize);
  MBStart[InputSize] = '\n';
  
  // Use the MemoryBuffer content directly
  std::string bufferContent(MB->getBufferStart(), MB->getBufferSize());
  
  // Add the source content directly to AllSources using AddCompilerInsertion
  // This is the proper way to handle in-memory source content in Flang
  auto sourceRange = AllSources->AddCompilerInsertion(bufferContent);
  
  // Create a CookedSource manually
  auto &cookedSource = AllCookedSources->NewCookedSource();
  cookedSource.Put(bufferContent);
  cookedSource.PutProvenance(sourceRange);
  
  // Marshal the CookedSource
  AllCookedSources->Register(cookedSource);
  cookedSource.Marshal(*AllCookedSources);

  // Then parse the prescanned code
  parsing.Parse(llvm::outs());

  // Check for parsing errors
  if (parsing.messages().AnyFatalError()) {
    return llvm::make_error<llvm::StringError>("Parsing failed with fatal errors", std::error_code());
  }

  // Extract the parsed program
  if (parsing.parseTree()) {
    // Move the parsed program
    FPTU.TUPart = std::make_unique<Fortran::parser::Program>(std::move(*parsing.parseTree()));
    
    // Perform semantic analysis on the parsed program
    const Fortran::common::IntrinsicTypeDefaultKinds defaultKinds;
    const Fortran::common::LanguageFeatureControl languageFeatures;
    const Fortran::common::LangOptions langOptions;
    
    // AllCookedSources is already stored in the parser instance
    
    // Create SemanticsContext for semantic analysis
    FPTU.SemanticsContext = std::make_unique<Fortran::semantics::SemanticsContext>(
        defaultKinds, languageFeatures, langOptions, *AllCookedSources);
    
    // Create Semantics object and perform analysis
    Fortran::semantics::Semantics semantics(*FPTU.SemanticsContext, *FPTU.TUPart);
    if (!semantics.Perform()) {
      return llvm::make_error<llvm::StringError>("Semantic analysis failed", std::error_code());
    }
    
    // Check for semantic errors
    if (semantics.AnyFatalError()) {
      return llvm::make_error<llvm::StringError>("Semantic analysis found fatal errors", std::error_code());
    }
    
    // Perform lowering immediately while AllCookedSources is still alive
    auto loweringError = generateLLVMIR(FPTU);
    if (loweringError) {
      return loweringError;
    }
    
  } else {
    return llvm::make_error<llvm::StringError>("Parsing failed - no parse tree generated", std::error_code());
  }
  
  return FPTU;
}

llvm::Error FortranIncrementalParser::generateLLVMIR(FortranPartialTranslationUnit &FPTU) {
  if (!FPTU.TUPart) {
    return llvm::make_error<llvm::StringError>("No parsed program available", std::error_code());
  }

    // Initialize LLVM target infrastructure
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();
    
    // Create MLIR context and load required dialects
    auto mlirContext = std::make_unique<mlir::MLIRContext>();
    fir::support::loadDialects(*mlirContext);
    fir::support::registerLLVMTranslation(*mlirContext);
    
    // Register additional dialects needed for LLVM IR conversion
    mlirContext->getOrLoadDialect<mlir::func::FuncDialect>();
    mlirContext->getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    
    // Set up basic target information (simplified for interpreter)
    llvm::Triple targetTriple(llvm::sys::getDefaultTargetTriple());
    
    // Set up data layout
    std::string error;
    const auto *target = llvm::TargetRegistry::lookupTarget(targetTriple.str(), error);
    if (!target) {
      return llvm::make_error<llvm::StringError>("Could not find target: " + error, std::error_code());
    }
    
    llvm::TargetOptions targetOptions;
    auto *targetMachine = target->createTargetMachine(targetTriple, "generic", "", targetOptions, std::nullopt);
    if (!targetMachine) {
      return llvm::make_error<llvm::StringError>("Could not create target machine", std::error_code());
    }
    
    // Create kind mapping for Fortran types
    const Fortran::common::IntrinsicTypeDefaultKinds defaultKinds;
    fir::KindMapping kindMap(mlirContext.get(), 
                            llvm::ArrayRef<fir::KindTy>{fir::fromDefaultKinds(defaultKinds)});
    
    // Declare mlirModule outside the if block
    mlir::ModuleOp mlirModule;
    
    // Convert the parsed Fortran program to FIR using LoweringBridge
    if (!(FPTU.TUPart && !FPTU.TUPart->v.empty() && FPTU.SemanticsContext && AllCookedSources)) {
      return llvm::make_error<llvm::StringError>("No parsed program available", std::error_code());
    }
    // Use the SemanticsContext and AllCookedSources from parsing
    auto &semanticsContext = *FPTU.SemanticsContext;
    auto &allCooked = *AllCookedSources;
    
    // Get language features from the semantics context
    const auto &languageFeatures = semanticsContext.languageFeatures();
    
    // Create LoweringBridge with all required parameters
    fir::KindMapping loweringKindMap(mlirContext.get(), llvm::ArrayRef<fir::KindTy>{
                                                fir::fromDefaultKinds(defaultKinds)});
        
    // Create LoweringBridge
    auto loweringBridge = Fortran::lower::LoweringBridge::create(
        *mlirContext, semanticsContext, defaultKinds,
        semanticsContext.intrinsics(),
        semanticsContext.targetCharacteristics(), allCooked,
        targetTriple.str(), loweringKindMap,
        Fortran::lower::LoweringOptions{}, // Default lowering options
        std::vector<Fortran::lower::EnvironmentDefault>{}, // Empty environment defaults
        languageFeatures, *targetMachine,
        Fortran::frontend::TargetOptions{}, // Default target options
        Fortran::frontend::CodeGenOptions{}); // Default codegen options
    
    // Lower the parsed program to FIR
    loweringBridge.lower(*FPTU.TUPart, semanticsContext);
    
    // Get the FIR module from the lowering bridge
    mlirModule = loweringBridge.getModule();
    
    // Check if the MLIR module is valid
    if (!mlirModule) {
      return llvm::make_error<llvm::StringError>("No MLIR module generated", std::error_code());
    }
    
    // Check if the MLIR module has any content
    if (mlirModule.getBody()->empty()) {
      return llvm::make_error<llvm::StringError>("MLIR module is empty", std::error_code());
    }
    
    // Store the MLIR module in the FPTU
    FPTU.MLIRModule = std::make_unique<mlir::ModuleOp>(mlirModule);
    
    // Set up the MLIR to LLVM IR pass pipeline
    mlir::PassManager pm(mlirModule->getName(), mlir::OpPassManager::Nesting::Implicit);
    MLIRToLLVMPassPipelineConfig config(llvm::OptimizationLevel::O0);
    config.DebugInfo = llvm::codegenoptions::NoDebugInfo;
    config.AliasAnalysis = false;
    config.EnableOpenMP = false;
    config.EnableOpenMPSimd = false;
    
    // Note: The "Unhandled parameter attribute 'fir.bindc_name'" warning is expected
    // and harmless. This attribute is used for C interoperability in Fortran but
    // is not essential for basic functionality. The MLIR to LLVM conversion doesn't
    // know how to handle this FIR-specific attribute, so it issues a warning.
    
    fir::createMLIRToLLVMPassPipeline(pm, config, "fortran_module");
    if (mlir::failed(pm.run(mlirModule))) {
      return llvm::make_error<llvm::StringError>("MLIR pass pipeline failed", std::error_code());
    }
    
    // Convert MLIR module to LLVM IR
    static llvm::LLVMContext llvmContext;
    auto llvmModule = mlir::translateModuleToLLVMIR(mlirModule, llvmContext, "fortran_module");
    
    if (!llvmModule) {
      return llvm::make_error<llvm::StringError>("Failed to convert MLIR to LLVM IR", std::error_code());
    }
    
    // Debug: Print the LLVM module to see what functions are available
    // llvm::errs() << "LLVM Module:\n";
    // llvmModule->print(llvm::errs(), nullptr, false, false);
    // llvm::errs() << "\n";
    
    // Store the LLVM module in the FPTU
    FPTU.TheModule = std::move(llvmModule);
    
    return llvm::Error::success();
}

