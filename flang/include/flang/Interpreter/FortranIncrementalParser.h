//===--- FortranIncrementalParser.h - Fortran Incremental Parser ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the FortranIncrementalParser class which handles parsing
// of Fortran code and generation of LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FLANG_INTERPRETER_FORTRANINCREMENTALPARSER_H
#define LLVM_FLANG_INTERPRETER_FORTRANINCREMENTALPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "flang/Parser/provenance.h"
#include <list>
#include <memory>

namespace flang {

struct FortranPartialTranslationUnit;

/// Handles incremental parsing of Fortran code and generation of LLVM IR.
class FortranIncrementalParser {
public:
  FortranIncrementalParser() = default;
  
  /// Parse Fortran code and create a partial translation unit.
  llvm::Expected<FortranPartialTranslationUnit>
  parse(llvm::StringRef Code);
  
  /// Generate LLVM IR for a parsed partial translation unit.
  llvm::Error generateLLVMIR(FortranPartialTranslationUnit &FPTU);

private:
  // Store AllSources and AllCookedSources to keep them alive for the lifetime of the parser
  std::unique_ptr<Fortran::parser::AllSources> AllSources;
  std::unique_ptr<Fortran::parser::AllCookedSources> AllCookedSources;
  
  // Counter for input line names (similar to Clang's IncrementalParser)
  unsigned InputCount = 0;
};

} // namespace flang

#endif // LLVM_FLANG_INTERPRETER_FORTRANINCREMENTALPARSER_H
