//===--- FortranPartialTranslationUnit.h - Fortran PTU Definition ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the FortranPartialTranslationUnit structure which represents
// a parsed and processed Fortran program unit.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FLANG_INTERPRETER_FORTRANPARTIALTRANSLATIONUNIT_H
#define LLVM_FLANG_INTERPRETER_FORTRANPARTIALTRANSLATIONUNIT_H

#include "llvm/ADT/DenseMap.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/BuiltinOps.h"
#include "flang/Parser/parse-tree.h"
#include "flang/Semantics/semantics.h"
#include <memory>

namespace llvm {
class Module;
} // namespace llvm

namespace Fortran {
namespace parser {
class Program;
} // namespace parser
} // namespace Fortran

namespace mlir {
class MLIRContext;
class ModuleOp;
} // namespace mlir

namespace flang {

/// Represents a partial translation unit for Fortran code.
/// This structure holds the parsed Fortran program, MLIR module (if generated),
/// and LLVM IR module (if generated).
struct FortranPartialTranslationUnit {
  std::unique_ptr<Fortran::parser::Program> TUPart; // parsed Program
  
  // The semantic analysis context (when available).
  std::unique_ptr<Fortran::semantics::SemanticsContext> SemanticsContext;
  
  // The MLIR module produced for the input (when available).
  std::unique_ptr<mlir::ModuleOp> MLIRModule;

  // The LLVM IR produced for the input (when available).
  std::unique_ptr<llvm::Module> TheModule;

  bool operator==(const FortranPartialTranslationUnit &other) const {
    return other.TUPart.get() == TUPart.get() && 
           other.MLIRModule.get() == MLIRModule.get() &&
           other.TheModule == TheModule;
  }
};

} // namespace flang

#endif // LLVM_FLANG_INTERPRETER_FORTRANPARTIALTRANSLATIONUNIT_H
