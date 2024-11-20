//===- BorrowChecker.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the borrow checking algorithm for C++ based on clangir.
// The algorithm refers https://rust-lang.github.io/rfcs/2094-nll.html.
// The implementation can be split into two parts: building borrows and
// check uses and borrows.
//
// The key data structure is `Borrower`. The entry point for building borrows is
// `Checker::initBorrowContexts()`. The entry point for checking is
// `Checker::checkBorrows()`.
//
//===----------------------------------------------------------------------===//

#include "CFG.h"
#include "SafeCXXUtils.h"
#include "Utils.h"

#include "../Transforms/PassDetail.h"
#include "clang/Basic/DiagnosticSafeCXX.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"

#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/Operation.h"

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"

#include <queue>

#include <set>

using namespace cir;
using namespace clang;

namespace {

/// A lifetime is a set of program points. We can check if a use if valid
/// by checking if the point of the use is in the set.
class Lifetime {
  llvm::DenseSet<mlir::Operation *> ops;

public:
  void addProgramPoint(mlir::Operation *op) { ops.insert(op); }

  bool isOpInLifetime(mlir::Operation *op) const { return ops.count(op); }

  bool isOverlappedWith(const Lifetime &other) const {
    return llvm::any_of(ops,
                        [&](auto *op) { return other.isOpInLifetime(op); });
  }

  void dump(unsigned indentLevel = 0) const;
};

void Lifetime::dump(unsigned indentLevel) const {
  auto ident = [](unsigned indentLevel) {
    for (unsigned i = 0; i < indentLevel; i++)
      llvm::errs() << " ";
  };
  ident(indentLevel);
  llvm::errs() << "Lifetime: {\n";
  for (auto *op : ops) {
    ident(indentLevel + 2);
    llvm::errs() << *op << "\n";
  }
  ident(indentLevel);
  llvm::errs() << "}\n";
}

/// We use borrower to repsent a (or multiple) borrow relationships.
/// Like Users, a user can have multiple uses. And each use is
/// an edge between two operations. Here we model the borrowing
/// relationship to different AllocaOps.
///
/// FIXME: This is not precise. We should be able to track the
/// the members of the allocas. e.g., we should be able to understand,
/// a.x and a.y should refer to different address.
class Borrower {
public:
  // Map borrowed alloca to the constantness.
  using BorroweeType = llvm::SmallMapVector<AllocaOp, bool, 8>;

private:
  BorroweeType borrowees;
  AllocaOp borrower;
  mlir::Operation *borrowingPoint;
  // Lifetime lifetime;
  Lifetime lifetime;

  static void search(const ASTContext &ctx, Borrower &borrower,
                     mlir::Operation *searchPoint,
                     ReachableAnalyzer &reachableAnalyzer);

public:
  Borrower(BorroweeType &&borrowees, AllocaOp borrower,
           mlir::Operation *borrowingPoint)
      : borrowees(std::move(borrowees)), borrower(borrower),
        borrowingPoint(borrowingPoint) {
    lifetime.addProgramPoint(borrowingPoint);
  }

  Borrower(Borrower &&other) = default;
  Borrower(const Borrower &other) = default;
  Borrower &operator=(Borrower &&other) = default;
  Borrower &operator=(const Borrower &other) = default;

  static void calculateLifetime(const ASTContext &ctx, Borrower &borrower,
                                ReachableAnalyzer &);

  AllocaOp getBorrower() const { return borrower; }
  StringRef getBorrowerName() const {
    return borrower ? const_cast<AllocaOp&>(borrower).getName() : llvm::StringRef("unnamed borrow");
  }

  std::optional<bool> isConstBorrow(AllocaOp alloca) const {
    auto iter = borrowees.find(alloca);
    if (iter == borrowees.end())
      return std::nullopt;

    return iter->second;
  }

  const BorroweeType &getBorrowees() const { return borrowees; }

  bool isInLifetime(mlir::Operation *op) const {
    return lifetime.isOpInLifetime(op);
  }

  bool isOverlappedWith(const Borrower &other) const {
    return lifetime.isOverlappedWith(other.lifetime);
  }

  mlir::Operation *getBorrowingPoint() const { return borrowingPoint; }

  mlir::Location getBorrowingPointLocation() const {
    return borrowingPoint->getLoc();
  }

  void dump() const ;
};

void Borrower::dump() const {
  llvm::errs() << "Lifetime of borrow: "
               << "{\n";
  if (borrower)
    llvm::errs() << "  borrower: " << *borrower << "\n";
  llvm::errs() << "  borrowing point: " << *borrowingPoint << "\n";
  llvm::errs() << "  Borrow from: ";
  for (auto [borrowee, constness] : borrowees) {
    llvm::errs() << "  (" << borrowee << " : "
                 << (constness ? "const" : "mutable") << ")\n";
  }
  llvm::errs() << "\n";

  lifetime.dump(2);
  llvm::errs() << "}\n";
}

void Borrower::search(const ASTContext &ctx, Borrower &borrower,
                      mlir::Operation *searchPoint,
                      ReachableAnalyzer &reachableAnalyzer) {
  if (searchPoint == borrower.borrowingPoint) {
    if (llvm::any_of(borrower.borrowees, [](auto allocaAndConstness) {
          return !allocaAndConstness.second;
        })) {
      Diag(ctx, searchPoint->getLoc(), diag::warn_multiple_non_const_borrow)
          << borrower.borrower.getName();
      Diag(ctx, searchPoint->getLoc(), diag::note_previous_borrow_starts_from);
    }

    return;
  }

  if (borrower.lifetime.isOpInLifetime(searchPoint))
    return;

  // If the borrower are overwritten, the lifetime should end.
  if (isWrite(searchPoint, borrower.borrower))
    return;

  // We shouldn't search beyond the region of the borrower.
  if (!borrower.borrower->getParentOp()
           ->getParentRegion()
           ->findAncestorOpInRegion(*searchPoint))
    return;

  // If the search point is not reachable to any borrower's user, we can stop.
  if (!llvm::any_of(borrower.borrower->getUsers(), [&](mlir::Operation *user) {
        return reachableAnalyzer.isReachable(searchPoint, user);
      }))
    return;

  borrower.lifetime.addProgramPoint(searchPoint);

  for (auto *succ : getSuccessors(searchPoint))
    search(ctx, borrower, succ, reachableAnalyzer);
}

void Borrower::calculateLifetime(const ASTContext &ctx, Borrower &borrower,
                                 ReachableAnalyzer &reachableAnalyzer) {
  if (!borrower.borrower)
    return;

  for (auto *succ : getSuccessors(borrower.borrowingPoint))
    search(ctx, borrower, succ, reachableAnalyzer);
}

std::optional<Borrower> getBorrower(StoreOp op) {
  std::optional<AllocaOp> borrower = getAllocaForAddr(op.getOperand(1));
  std::optional<AllocaOp> borrowee = getAllocaForValue(op.getOperand(0));

  if (!borrower || !borrowee)
    return std::nullopt;

  Borrower::BorroweeType borrowees;
  borrowees.insert({*borrowee, borrower->getConstOrReferencingConst()});
  return Borrower(std::move(borrowees), *borrower, op);
}

std::optional<AllocaOp> getAllocaForReturn(CallOp call, StoreOp &store) {
  if (call->getNumResults() == 0)
    return std::nullopt;

  // Finds the alloca for the return value of the given call.
  llvm::SmallVector<AllocaOp> candidates;
  for (auto &use : call->getResult(0).getUses()) {
    if (auto storeOp = mlir::dyn_cast<StoreOp>(use.getOwner())) {
      if (use.getOperandNumber() == 0) {
        std::optional<AllocaOp> alloca =
            mlir::dyn_cast<AllocaOp>(storeOp.getOperand(1).getDefiningOp());
        if (!alloca)
          continue;

        store = storeOp;
        candidates.push_back(*alloca);
      }
    }
  }

  if (candidates.size() == 1)
    return candidates[0];

  // Unexpected case. Are we going to generate non canonical CIR?
  return std::nullopt;
}

using BorrowersType = llvm::SmallVector<Borrower, 16>;

BorrowersType getBorrower(const ASTContext &ctx, CallOp op) {
  if (op.getArgOps().empty())
    return {};

  BorrowersType borrows;

  auto callAst = op.getAst();

  // The index of arguments in CIR and AST may differ due to the object
  // argument.
  unsigned diffForCall = getArgIdxDiffForCall(op);
  unsigned diffForFunc = getArgIdxDiffForDecl(op);

  const clang::Expr *objectArgument = getAstObjArg(op);
  unsigned argIdx = 0;
  // increment diff so that we can always increate argIdx in loop.
  diffForCall += 1;
  diffForFunc += 1;

  Borrower::BorroweeType borrowees;

  // If we have 'this' pointer, assume `this` can borrow other arguments.
  // So we can prepare the borrowing infomation for `this`.
  Borrower::BorroweeType borroweesForObjArg;
  std::optional<AllocaOp> borroweeForObjArg;

  for (auto arg : op.getArgOps()) {
    argIdx++;

    if (!isa<cir::PointerType>(arg.getType()))
      continue;

    // argIdx may only be less than diffForFunc if we have the object argument
    // pointer.
    if (argIdx < diffForFunc) {
      if (isThisNoBorrowToRet(op))
        continue;
    } else if (argIdx >= diffForFunc &&
               isArgNoBorrowToRet(op, argIdx - diffForFunc)) {
      continue;
    }

    std::optional<AllocaOp> borrowee = getAllocaForValue(arg);
    if (!borrowee)
      continue;

    // If we don't have ast information, assuming it is mutable conservatively.
    bool isConst = false;
    if (callAst) {
      if (objectArgument && argIdx == 1) {
        isConst = isConstOrRefergConst(objectArgument->getType());
        borroweeForObjArg = *borrowee;
      } else {
        assert(argIdx >= diffForCall);
        isConst =
            isConstOrRefergConst(callAst->getArgType(argIdx - diffForCall));

        if (objectArgument)
          borroweesForObjArg.insert({*borrowee, isConst});
      }
    }

    {
      auto iter = borrowees.find(*borrowee);
      // If we have duplicated borrowees, and any of them is non constant,
      // then we should diagnose for the
      if (iter != borrowees.end() && (!iter->second || !isConst)) {
        Diag(ctx, op.getLoc(), diag::warn_multiple_non_const_borrow)
            << borrowee->getName();
      }
    }

    borrowees.insert({*borrowee, isConst});
  }

  if (borrowees.empty())
    return {};

  // If we have 'this' pointer and we found the argument. Make a borrow for
  // 'this' if 'this' might contain references to the borrowees.
  if (!borroweesForObjArg.empty() && borroweeForObjArg) {
    llvm::SmallVector<mlir::Type> borroweeTypes;
    for (auto [borrowee, _] : borroweesForObjArg)
      borroweeTypes.push_back(borrowee.getAllocaType());
    if (doTypeContainReferencesTo(borroweeForObjArg->getAllocaType(),
                                  borroweeTypes)) {
      borrows.push_back(
          Borrower(std::move(borroweesForObjArg), *borroweeForObjArg, op));
    }
  }

  StoreOp store;
  std::optional<AllocaOp> borrower = getAllocaForReturn(op, store);
  // If the return type doesn't contain any references of these alloca, we can
  // assume it won't reference any borrowees.
  if (borrower) {
    llvm::SmallVector<mlir::Type> borroweeTypes;
    for (auto [borrowee, _] : borrowees)
      borroweeTypes.push_back(borrowee.getAllocaType());
    if (!doTypeContainReferencesTo(borrower->getAllocaType(), borroweeTypes))
      borrower = std::nullopt;
  }

  if (!borrower) {
    // If borrows are not empty, it must be the borrow to the object argument.
    // Let's avoid create them to the same borrowing point to give false
    // positive duplicated borrows.
    if (borrows.empty())
      borrows.push_back(Borrower(std::move(borrowees), nullptr, op));
    else {
      assert(borroweeForObjArg);
      // Now let's model that we're borrowing the `this` too. The other
      // arguments are borrowed by the `this` pointer.
      Borrower::BorroweeType borroweesForReturn;
      borroweesForReturn.insert(
          {*borroweeForObjArg, borrowees[*borroweeForObjArg]});
      borrows.push_back(Borrower(std::move(borroweesForReturn), nullptr, op));
    }
    return borrows;
  }

  assert(store);
  borrows.push_back(Borrower(std::move(borrowees), *borrower, store));
  return borrows;
}

class BorrowContext {
  // All borrows, collected for owning lifetimes.
  BorrowersType borrows;
  // Map from allocas to these borrows which borrowed from the alloca.
  llvm::DenseMap<AllocaOp, llvm::SetVector<Borrower *>> borrowedBys;
  // Map from allocas to these borrows which borrows entities from other
  // allocas.
  llvm::DenseMap<AllocaOp, llvm::SetVector<Borrower *>> borrowing;

  void insert(AllocaOp borrowee, Borrower *borrower, bool isBorrowedBy) {
    auto &map = isBorrowedBy ? borrowedBys : borrowing;
    auto iter = map.find(borrowee);
    if (iter == map.end()) {
      map.insert({borrowee, llvm::SetVector<Borrower *>()})
          .first->second.insert(borrower);
    } else {
      iter->second.insert(borrower);
    }
  }

  void insertBorrowedBy(AllocaOp borrowee, Borrower *borrower) {
    return insert(borrowee, borrower, /*isBorrowedBy=*/true);
  }

  void insertBorrowing(AllocaOp borrower, Borrower *borrowing) {
    return insert(borrower, borrowing, /*isBorrowedBy=*/false);
  }

public:
  llvm::ArrayRef<Borrower *> getBorrowedBy(AllocaOp op) const {
    auto iter = borrowedBys.find(op);
    if (iter == borrowedBys.end())
      return {};

    return iter->second.getArrayRef();
  }

  llvm::ArrayRef<Borrower *> getBorrowing(AllocaOp op) const {
    auto iter = borrowing.find(op);
    if (iter == borrowing.end())
      return {};

    return iter->second.getArrayRef();
  }

  // Get all the borrower that the alloca borrows recursively.
  llvm::SmallVector<Borrower *> collectBorrowing(AllocaOp op) const;

  void addBorrows(BorrowersType &&borrowers) {
    assert(borrows.empty() && "We shouldn't add borrows twice!");

    borrows.swap(borrowers);

    for (auto &borrow : borrows) {
      for (auto [alloca, _] : borrow.getBorrowees())
        insertBorrowedBy(alloca, &borrow);

      if (borrow.getBorrower())
        insertBorrowing(borrow.getBorrower(), &borrow);
    }
  }

  llvm::ArrayRef<Borrower> getBorrows() const {
    return borrows;
  }
};

llvm::SmallVector<Borrower *>
BorrowContext::collectBorrowing(AllocaOp op) const {
  llvm::SmallVector<Borrower *> ret(getBorrowing(op));

  if (ret.empty())
    return ret;

  llvm::DenseSet<Borrower *> visited;
  std::queue<Borrower *> worklist;

  for (auto *borrow : ret) {
    worklist.push(borrow);
    visited.insert(borrow);
  }

  while (!worklist.empty()) {
    Borrower *borrow = worklist.front();
    worklist.pop();

    for (auto [alloca, _] : borrow->getBorrowees()) {
      for (Borrower *borrow : getBorrowing(alloca)) {
        if (visited.insert(borrow).second) {
          worklist.push(borrow);
          ret.push_back(borrow);
        }
      }
    }
  }

  return ret;
}

struct Checker {
  FuncOp func;

  ReachableAnalyzer reachableAnalyzer;

  // If return type doesn't contain references, we can avoid checking if
  // the returns leak references.
  bool doReturnTypeContainingReferences;

  const ASTContext &ctx;

  BorrowContext borrowContexts;

  void initBorrowContexts();

  llvm::SmallVector<AllocaOp> collectAllocs();

  // The set of allocas that are expected to live longer than
  // the function scope.
  // They shall come from the parameters which are expected to
  // to live longer than the function.
  llvm::SmallSet<AllocaOp, 8> longliveAllocas;

  void initLongliveAllocas();

  bool isAllocaLiveLongerThan(AllocaOp alloca, mlir::Operation *op);

public:
  Checker(FuncOp func, const ASTContext &ctx)
      : func(func), reachableAnalyzer(func), ctx(ctx) {
    doReturnTypeContainingReferences =
        doTypeContainReferences(func.getFunctionType().getReturnType());

    initBorrowContexts();

    initLongliveAllocas();
  }

  void checkBorrows();

private:
  void checkAllocaUse(AllocaOp alloca);

  void checkReadDuringNonConstBorrow(AllocaOp alloca, mlir::Operation *op,
                                     const llvm::SmallVector<Borrower *> &);

  llvm::SmallVector<Borrower *>
  checkNonMultipleNonConstBorrow(llvm::ArrayRef<Borrower *> borrows,
                                 AllocaOp alloca);

  // check the change is not overlapped with the borrows.
  void checkChange(AllocaOp alloca, mlir::Operation *change,
                   llvm::ArrayRef<Borrower *> borrows);

  using ReturnedBorrowTy =
      llvm::SmallMapVector<Borrower *, mlir::Operation *, 4>;
  /// \param returnerBorrows used to emit diagnostics to avoid duplicated
  /// diagnostic messages.
  void checkReturn(mlir::Operation *op, AllocaOp alloca,
                   const llvm::SmallVector<Borrower *> &borrowings,
                   ReturnedBorrowTy &returnerBorrows);
  void diagnoseInvalidAllocas(ReturnedBorrowTy &returnerBorrows);

  bool canAllocaLiveLongerThanFunc(AllocaOp alloca);

  void checkNoDeref(mlir::Operation *op, AllocaOp alloca);

  // Check the use of alloca are valid by checking the liveness of borrowing.
  void checkUseForBorrowings(mlir::Operation *user, AllocaOp alloca,
                             const llvm::SmallVector<Borrower *> &borrowings);

  void checkBorrowLifetime(const Borrower *borrower);
};

bool Checker::isAllocaLiveLongerThan(AllocaOp alloca, mlir::Operation *op) {
  if (canAllocaLiveLongerThanFunc(alloca))
    return true;
  
  if (op->getNumResults() > 0) {
    auto allocaOp = getAllocaForValue(op->getResult(0));
    if (allocaOp && canAllocaLiveLongerThanFunc(*allocaOp))
      return false;
  }

  return alloca->getParentRegion()->findAncestorOpInRegion(*op);
}

void Checker::checkBorrowLifetime(const Borrower *borrow) {
  for (auto [borrowee, _] : borrow->getBorrowees()) {
    mlir::Operation *op = borrow->getBorrower();
    if (!op)
      op = borrow->getBorrowingPoint();
      
    if (!isAllocaLiveLongerThan(borrowee, op)) {
      Diag(ctx, borrow->getBorrowingPointLocation(), diag::warn_shorted_lifetime_borrow)
        << borrowee.getName() << borrow->getBorrowerName();
    }
  }
}

void Checker::initBorrowContexts() {
  // Borrows to be calculated the lifetimes
  std::queue<Borrower> borrowsWorklist;

  func->walk([&](mlir::Operation *op) {
    if (!isSafeLoc(ctx, op->getLoc()))
      return;

    if (auto store = mlir::dyn_cast<StoreOp>(op)) {
      std::optional<Borrower> borrow = getBorrower(store);
      if (!borrow)
        return;

      borrowsWorklist.push(std::move(*borrow));
      return;
    }

    if (auto call = mlir::dyn_cast<CallOp>(op)) {
      BorrowersType borrows = getBorrower(ctx, call);
      for (auto &borrow : borrows)
        borrowsWorklist.push(std::move(borrow));
      return;
    }
  });

  BorrowersType borrowers;
  borrowers.reserve(borrowsWorklist.size());

  while (!borrowsWorklist.empty()) {
    Borrower borrower = std::move(borrowsWorklist.front());
    borrowsWorklist.pop();

    Borrower::calculateLifetime(ctx, borrower, reachableAnalyzer);
    borrowers.push_back(borrower);
  }

  borrowContexts.addBorrows(std::move(borrowers));
}

llvm::SmallVector<AllocaOp> Checker::collectAllocs() {
  llvm::SmallVector<AllocaOp> allocas;

  func.walk([&](AllocaOp alloca) {
    if (!isSafeLoc(ctx, alloca.getLoc()))
      return;
    // Don't hoist allocas with dynamic alloca size.
    if (alloca.getDynAllocSize())
      return;

    allocas.push_back(alloca);
  });

  return allocas;
}

void Checker::checkBorrows() {
  llvm::SmallVector<AllocaOp> allocas = collectAllocs();

  for (auto alloca : allocas)
    checkAllocaUse(alloca);

  for (const auto &borrow : borrowContexts.getBorrows())
    checkBorrowLifetime(&borrow);
}

llvm::SmallVector<Borrower *>
Checker::checkNonMultipleNonConstBorrow(llvm::ArrayRef<Borrower *> borrows,
                                        AllocaOp alloca) {
  llvm::SmallVector<Borrower *> nonConstBorrows;

  for (auto *borrow : borrows) {
    std::optional<bool> isConstBorrow = borrow->isConstBorrow(alloca);
    if (!isConstBorrow || *isConstBorrow)
      continue;

    nonConstBorrows.push_back(borrow);
  }

  llvm::DenseSet<std::pair<Borrower *, Borrower *>> overlappedBorrows;

  for (auto *nonConstBorrow : nonConstBorrows) {
    for (auto *borrow : borrows) {
      if (nonConstBorrow == borrow)
        continue;

      if (overlappedBorrows.count(std::make_pair(nonConstBorrow, borrow)))
        continue;

      if (nonConstBorrow->isOverlappedWith(*borrow)) {
        Diag(ctx, nonConstBorrow->getBorrowingPointLocation(),
             diag::warn_multiple_non_const_borrow)
            << alloca.getName();
        Diag(ctx, borrow->getBorrowingPointLocation(),
             diag::note_previous_borrow_starts_from);

        overlappedBorrows.insert(std::make_pair(borrow, nonConstBorrow));
      }
    }
  }

  return nonConstBorrows;
}

void Checker::checkChange(AllocaOp alloca, mlir::Operation *change,
                          llvm::ArrayRef<Borrower *> borrows) {
  for (auto *borrow : borrows) {
    if (borrow->isInLifetime(change)) {
      Diag(ctx, change->getLoc(), diag::warn_change_during_borrowing)
          << alloca.getName() << borrow->getBorrowerName();
      Diag(ctx, borrow->getBorrowingPointLocation(),
           diag::note_borrow_starts_from);
    }
  }
}

void Checker::checkReadDuringNonConstBorrow(
    AllocaOp alloca, mlir::Operation *op,
    const llvm::SmallVector<Borrower *> &nonConstBorrows) {
  for (auto *borrow : nonConstBorrows)
    if (borrow->isInLifetime(op)) {
      Diag(ctx, op->getLoc(), diag::warn_read_during_non_const_borrowing)
          << alloca.getName() << borrow->getBorrowerName();
      Diag(ctx, borrow->getBorrowingPointLocation(),
           diag::note_previous_borrow_starts_from);
    }
}

void Checker::initLongliveAllocas() {
  mlir::Region &region = func.getBody();

  auto funcAst = dyn_cast<ASTFunctionDeclInterface>(func.getAstAttr());

  unsigned argIdx = 0;
  for (auto arg : region.getArguments()) {
    argIdx++;

    if (!isa<cir::PointerType>(arg.getType()))
      continue;

    if (funcAst && funcAst.hasNoBorrowAttr(argIdx - 1))
      continue;

    // Pattern match, assume the argument's use pattern to be pretty simple.
    for (auto *user : arg.getUsers())
      if (auto store = mlir::dyn_cast<StoreOp>(user)) {
        auto alloca = mlir::dyn_cast<AllocaOp>(store.getAddr().getDefiningOp());
        if (!alloca)
          continue;

        longliveAllocas.insert(alloca);
      }
  }
}

bool Checker::canAllocaLiveLongerThanFunc(AllocaOp alloca) {
  return longliveAllocas.count(alloca);
}

void Checker::checkReturn(mlir::Operation *op, AllocaOp alloca,
                          const llvm::SmallVector<Borrower *> &borrowings,
                          ReturnedBorrowTy &returnerBorrows) {
  if (!canBeReturned(op, reachableAnalyzer))
    return;

  for (auto *borrow : borrowings) {
    if (!borrow->isInLifetime(op))
      continue;

    returnerBorrows[borrow] = op;
  }
}

void Checker::diagnoseInvalidAllocas(ReturnedBorrowTy &returnerBorrows) {
  for (auto [borrow, op] : returnerBorrows) {
    for (auto [borrowee, _] : borrow->getBorrowees()) {
      if (canAllocaLiveLongerThanFunc(borrowee))
        continue;

      Diag(ctx, op->getLoc(), diag::warn_return_during_borrowing)
          << borrowee.getName();
      Diag(ctx, borrow->getBorrowingPointLocation(),
           diag::note_previous_borrow_starts_from);
    }
  }
}

void Checker::checkNoDeref(mlir::Operation *op, AllocaOp alloca) {
  if (auto load = mlir::dyn_cast<LoadOp>(op); load && load.getIsDeref()) {
    Diag(ctx, load->getLoc(), diag::warn_no_deref) << alloca.getName();
  }
}

void Checker::checkUseForBorrowings(
    mlir::Operation *user, AllocaOp alloca,
    const llvm::SmallVector<Borrower *> &borrowings) {
  // For the use of an alloca, makes sure all the borrowees are alive at the use
  // point.
  for (auto *borrow : borrowings) {
    if (!borrow->isInLifetime(user))
      continue;

    for (auto [borrowee, _] : borrow->getBorrowees()) {
      // If the user doesn't live in the same region with the alloca for the
      // borrowee, the value should be invalid since the borrowee is not alive.
      if (borrowee->getParentRegion()->findAncestorOpInRegion(*user))
        continue;

      Diag(ctx, user->getLoc(), diag::warn_use_beyond_lifetime_of_borrow)
          << borrow->getBorrowerName() << borrowee.getName();
      Diag(ctx, borrow->getBorrowingPointLocation(),
           diag::note_previous_borrow_starts_from);
    }
  }
}

void Checker::checkAllocaUse(AllocaOp alloca) {
  llvm::ArrayRef<Borrower *> borrows = borrowContexts.getBorrowedBy(alloca);
  llvm::SmallVector<Borrower *> nonConstBorrows =
      checkNonMultipleNonConstBorrow(borrows, alloca);

  llvm::SmallVector<Borrower *> borrowings =
      borrowContexts.collectBorrowing(alloca);

  ReturnedBorrowTy returnerBorrows;

  for (auto *user : alloca->getUsers()) {
    if (!isSafeLoc(ctx, user->getLoc()))
      continue;

    // Don't care about CallOp since CallOp may always produce a borrow.
    if (isWrite(user, alloca) && !mlir::isa<CallOp>(user)) {
      // If this change is detected by other borrows, let other borrows to check
      // this case to avoid diagnostic messages.
      if (llvm::all_of(borrows, [&](Borrower *borrow) {
            return borrow->getBorrowingPoint() != user;
          }))
        checkChange(alloca, user, borrows);
      continue;
    }

    checkUseForBorrowings(user, alloca, borrowings);

    if (isRead(user, alloca))
      checkReadDuringNonConstBorrow(alloca, user, nonConstBorrows);

    checkNoDeref(user, alloca);

    if (doReturnTypeContainingReferences)
      checkReturn(user, alloca, borrowings, returnerBorrows);
  }

  diagnoseInvalidAllocas(returnerBorrows);
}

struct BorrowCheckPass : public mlir::BorrowCheckBase<BorrowCheckPass> {
  BorrowCheckPass() = default;
  BorrowCheckPass(ASTContext &Ctx) : astCtx(&Ctx) {}

  void runOnOperation() override {
    llvm::SmallVector<FuncOp> funcs;
    getOperation()->walk([&](FuncOp func) {
      if (!isSafeLoc(*astCtx, func.getLoc()))
        return;

      if (func.getRegion().empty())
        return;
      funcs.push_back(func);
    });

    for (auto func : funcs) {
      Checker checker(func, *astCtx);
      checker.checkBorrows();
    }
  }

  ASTContext *astCtx = nullptr;
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::createBorrowCheckPass() {
  return std::make_unique<BorrowCheckPass>();
}

std::unique_ptr<mlir::Pass> mlir::createBorrowCheckPass(ASTContext &astCtx) {
  return std::make_unique<BorrowCheckPass>(astCtx);
}
