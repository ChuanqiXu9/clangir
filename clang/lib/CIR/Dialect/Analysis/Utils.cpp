//===- CFG.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Utils.h"
#include "CFG.h"
#include "clang/AST/ExprCXX.h"

using namespace cir;

bool cir::isConstOrRefergConst(clang::QualType Ty) {
  if (Ty.isConstQualified())
    return true;

  auto *RT =
      mlir::dyn_cast_if_present<clang::ReferenceType>(Ty.getTypePtrOrNull());
  if (RT)
    return isConstOrRefergConst(RT->getPointeeType());

  auto *PT =
      mlir::dyn_cast_if_present<clang::PointerType>(Ty.getTypePtrOrNull());
  if (PT)
    return isConstOrRefergConst(PT->getPointeeType());

  return false;
}

const clang::Expr *cir::getAstObjArg(CallOp op) {
  auto callAst = op.getAst();
  const clang::Expr *objectArgument =
      callAst ? callAst->getObjectArgument() : nullptr;

  if (objectArgument)
    return objectArgument;

  if (!objectArgument && callAst && callAst->getExpr()) {
    auto *operatorCall =
        mlir::dyn_cast<clang::CXXOperatorCallExpr>(callAst->getExpr());

    if (!operatorCall)
      return nullptr;

    // If it is not a decl in class, we don't need to find the operator.
    auto *directCallee = operatorCall->getDirectCallee();
    if (!mlir::isa<clang::CXXRecordDecl>(directCallee->getDeclContext()))
      return nullptr;

    if (operatorCall->isAssignmentOp()) {
      return operatorCall->getArg(0);
    } else if (operatorCall->getOperator() == clang::OO_Call) {
      return operatorCall->getCallee();
    } else if (operatorCall->getOperator() == clang::OO_Star) {
      return operatorCall->getArg(0);
    }

    return nullptr;
  }

  return nullptr;
}

const clang::FunctionDecl *cir::getCallee(CallOp op) {
  auto callAst = op.getAst();
  if (!callAst)
    return nullptr;
  
  return callAst->getCalleeDecl();
}

bool cir::isCallNoBorrowToRet(CallOp op) {
  auto *callee = getCallee(op);
  if (!callee)
    return false;

  return llvm::any_of(callee->redecls(), [](auto *decl) {
    return decl->template hasAttr<clang::SafeCXXNoBorrowToRetAttr>();
  });  
}

bool cir::isThisNoBorrowToRet(CallOp op) {
  if (isCallNoBorrowToRet(op))
    return true;

  auto *callee = getCallee(op);
  if (!callee)
    return false;

  return llvm::any_of(callee->redecls(), [](auto *decl) {
    return decl->template hasAttr<clang::SafeCXXThisNoBorrowToRetAttr>();
  });  
}

unsigned cir::getArgIdxDiffForCall(CallOp op) {
  auto callAst = op.getAst();
  if (!callAst)
    return 0;

  unsigned diff = op.getArgOps().size() - callAst->getNumArgs();
  assert(diff <= 1 && "the number difference of arguments in CIR calls and "
                      "Call Expr should be at most 1!");
  return diff;
}
unsigned cir::getArgIdxDiffForDecl(CallOp op) {
  auto *callee = getCallee(op);
  if (!callee)
    return 0;

  unsigned diff = op.getArgOps().size() - callee->getNumParams();
  assert(diff <= 1 && "the number difference of arguments in CIR calls and "
                      "FunctionDecl should be at most 1!");
  return diff;
}

bool cir::isArgNoBorrowToRet(CallOp op, unsigned argIdx) {
  if (isCallNoBorrowToRet(op))
    return true;

  auto *callee = getCallee(op);
  if (!callee)
    return false;

  return llvm::any_of(callee->redecls(), [&](auto *decl) {
    auto *parm = decl->getParamDecl(argIdx);
    if (!parm)
      return false;

    return parm->template hasAttr<clang::SafeCXXNoBorrowToRetAttr>();
  });
}

std::optional<AllocaOp> cir::getAllocaForAddr(const mlir::Value &v) {
  auto *op = v.getDefiningOp();
  if (auto alloca = mlir::dyn_cast_if_present<AllocaOp>(op))
    return alloca;

  if (auto getMem = mlir::dyn_cast_if_present<GetMemberOp>(op))
    return getAllocaForValue(getMem.getAddr());

  // We can try to chasing the use-def chains to find the alloca if necessary.
  return std::nullopt;
}
std::optional<AllocaOp> cir::getAllocaForValue(const mlir::Value &v) {
  if (auto alloca = getAllocaForAddr(v))
    return alloca;

  auto *op = v.getDefiningOp();
  // Use getAllocaForAddr instead of getAllocaForValue to not chasing
  // the chain too long.
  if (auto load = mlir::dyn_cast_if_present<LoadOp>(op))
    return getAllocaForAddr(load.getAddr());

  // We can try to chasing the use-def chains to find the alloca if necessary.
  return std::nullopt;
}
bool cir::isWrite(mlir::Operation *op, AllocaOp alloca) {
  if (auto store = mlir::dyn_cast<StoreOp>(op);
      store && store.getAddr() == alloca)
    return true;

  if (auto call = mlir::dyn_cast<CallOp>(op)) {
    auto callAst = call.getAst();
    if (!callAst)
      return false;

    clang::Expr *objectArgument =
        callAst ? callAst->getObjectArgument() : nullptr;
    unsigned argIdx = 0;
    unsigned diff = 1 + (objectArgument ? 1 : 0);

    for (auto arg : call.getArgOps()) {
      argIdx++;

      if (!mlir::isa<cir::PointerType>(arg.getType()))
        continue;

      std::optional<AllocaOp> argAlloca = getAllocaForAddr(arg);
      if (!argAlloca)
        continue;

      if (*argAlloca != alloca)
        continue;

      if (argIdx < diff) {
        assert(objectArgument);
        if (!isConstOrRefergConst(objectArgument->getType()))
          return true;
      } else {
        if (!isConstOrRefergConst(callAst->getArgType(argIdx - diff)))
          return true;
      }
    }
  }

  return false;
}

bool cir::isRead(mlir::Operation *op, AllocaOp alloca) {
  if (auto load = mlir::dyn_cast<LoadOp>(op); load && load.getAddr() == alloca)
    return true;

  return false;
}

static bool isStoreToRet(mlir::Operation *op,
                         ReachableAnalyzer &reachableAnalyzer) {
  auto store = mlir::dyn_cast<StoreOp>(op);
  if (!store)
    return false;

  auto alloca = mlir::dyn_cast<AllocaOp>(store.getAddr().getDefiningOp());
  if (!alloca)
    return false;

  for (auto *user : alloca->getUsers())
    if (auto load = mlir::dyn_cast<LoadOp>(user)) {
      if (!reachableAnalyzer.isReachable(store, load))
        continue;

      for (auto *u : load->getUsers())
        if (mlir::isa<ReturnOp>(u))
          return true;
    }

  return false;
}

bool cir::canBeReturned(mlir::Operation *op,
                        ReachableAnalyzer &reachableAnalyzer) {
  if (auto load = mlir::dyn_cast<LoadOp>(op))
    for (auto *u : load->getUsers()) {
      if (isStoreToRet(u, reachableAnalyzer))
        return true;

      if (mlir::isa<ReturnOp>(u))
        return true;
    }

  return isStoreToRet(op, reachableAnalyzer);
}

bool cir::doTypeContainReferences(mlir::Type type) {
  if (mlir::isa<PointerType>(type))
    return true;

  if (auto structTy = mlir::dyn_cast<StructType>(type)) {
    if (llvm::any_of(structTy.getMembers(), [&](mlir::Type memTy) {
          return doTypeContainReferences(memTy);
        }))
      return true;
  }

  return false;
}

static bool mayBeSubTypeOf(mlir::Type type, mlir::Type target) {
  if (type == target)
    return true;

  if (auto structTy = mlir::dyn_cast<StructType>(target)) {
    if (llvm::any_of(structTy.getMembers(), [&](mlir::Type memberTy) {
          return mayBeSubTypeOf(type, memberTy);
        }))
      return true;
  }

  return false;
}

bool cir::doTypeContainReferencesTo(mlir::Type type, mlir::Type target) {
  if (auto pointerTy = mlir::dyn_cast<PointerType>(type)) {
    if (mayBeSubTypeOf(pointerTy, target))
      return true;

    auto pointeeType = pointerTy.getPointee();
    // void * can refer to anything.
    if (mlir::isa<VoidType>(pointeeType))
      return true;

    if (mayBeSubTypeOf(pointeeType, target))
      return true;

    type = pointeeType;
  }

  if (auto structTy = mlir::dyn_cast<StructType>(type)) {
    if (llvm::any_of(structTy.getMembers(), [&](mlir::Type memTy) {
          return doTypeContainReferencesTo(memTy, target);
        }))
      return true;
  }

  return false;
}
