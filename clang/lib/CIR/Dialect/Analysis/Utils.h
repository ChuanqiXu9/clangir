//===- Utils.h
//--------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIR_ANALYSIS_UTILS_H
#define CIR_ANALYSIS_UTILS_H

#include "CFG.h"
#include "clang/AST/Type.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"

namespace cir {
bool isConstOrRefergConst(clang::QualType Ty);

const clang::Expr *getAstObjArg(CallOp op);

const clang::FunctionDecl *getCallee(CallOp op);

bool isCallNoBorrowToRet(CallOp op);
bool isThisNoBorrowToRet(CallOp op);
bool isArgNoBorrowToRet(CallOp op, unsigned argIdxInCIR);

/// Due to the existence of object argument, the argument index in CIR and AST
/// may differ. And the argument index in function and call expr may differ too.
unsigned getArgIdxDiffForCall(CallOp op);
unsigned getArgIdxDiffForDecl(CallOp op);

/// Given a maybe address value, try to find an alloca to own the address.
std::optional<AllocaOp> getAllocaForAddr(const mlir::Value &v);
/// Given a value, try to find an alloca to provide the value. The value
/// may be the address of the alloca.
std::optional<AllocaOp> getAllocaForValue(const mlir::Value &v);

bool isWrite(mlir::Operation *op, AllocaOp alloca);
bool isRead(mlir::Operation *op, AllocaOp alloca);
/// Whether or not the operation leads a return operation.
/// This won't chase the use-def chain so we can think this checks for direct
/// return.
bool canBeReturned(mlir::Operation *op, ReachableAnalyzer &);

/// If the type contains any reference types, if not, we can assume the variable
/// of such types won't borrow any thing.
bool doTypeContainReferences(mlir::Type type);

/// If the \param type contains any reference types to the type of \param target
/// or sub types of \param target. If not, we can assume the variable of \param
/// type won't contain any reference to another variable of \param target.
bool doTypeContainReferencesTo(mlir::Type type, mlir::Type target);

/// If the \param type contains any reference types to the type of \param target
/// or sub types of \param target. If not, we can assume the variable of \param
/// type won't contain any reference to another variable of \param target.
inline bool doTypeContainReferencesTo(mlir::Type type,
                                      llvm::SmallVector<mlir::Type> targets) {
  return llvm::any_of(targets, [&](mlir::Type target) {
    return doTypeContainReferencesTo(type, target);
  });
}
} // namespace cir

#endif
