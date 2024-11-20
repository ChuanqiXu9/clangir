//===- CFG.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CFG.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/DenseSet.h"
#include <queue>

using namespace cir;

namespace {
llvm::SmallVector<mlir::Operation *> getSuccessorsForBreakOp(BreakOp breakOp) {
  mlir::Operation *op = &*breakOp;
  while ((op = op->getParentOp()))
    if (mlir::isa<cir::LoopOpInterface, cir::SwitchOp>(op))
      return {op->getNextNode()};
  return {};
}

llvm::SmallVector<mlir::Operation *>
getSuccessorsForContinueOp(ContinueOp breakOp) {
  mlir::Operation *op = &*breakOp;
  while ((op = op->getParentOp()))
    if (mlir::isa<cir::LoopOpInterface>(op))
      return getSuccessors(op);
  return {};
}

llvm::SmallVector<mlir::Operation *> getSuccessorsForYieldOp(YieldOp yieldOp) {
  mlir::Operation *op = &*yieldOp;
  while ((op = op->getParentOp())) {
    if (mlir::isa<IfOp, ScopeOp, SwitchOp, CaseOp, AwaitOp, TernaryOp, GlobalOp,
                  TryOp, ArrayCtor, ArrayDtor, CallOp>(op))
      return {op->getNextNode()};
    if (auto loop = mlir::dyn_cast<cir::LoopOpInterface>(op)) {
      llvm::SmallVector<mlir::Operation *> ret;

      if (yieldOp->getParentRegion() == &loop.getCond())
        return {&loop.getBody().front().front(), op->getNextNode()};

      if (yieldOp->getParentRegion() == &loop.getBody()) {
        auto *afterBody =
            (loop.maybeGetStep() ? loop.maybeGetStep() : &loop.getCond());
        return {&afterBody->front().front()};
      }

      if (yieldOp->getParentRegion() == loop.maybeGetStep())
        return {&loop.getCond().front().front()};
    }
  }
  return {};
}

llvm::SmallVector<mlir::Operation *>
getSuccessorsForConditionOp(ConditionOp condition) {
  mlir::Operation *op = &*condition;
  while ((op = op->getParentOp())) {
    if (auto loop = mlir::dyn_cast<cir::LoopOpInterface>(op))
      return {&loop.getBody().front().front(), op->getNextNode()};

    if (auto await = mlir::dyn_cast<AwaitOp>(op))
      return {&await.getResume().front().front(),
              &await.getSuspend().front().front()};
  }
  return {};
}

llvm::SmallVector<mlir::Operation *>
getSuccessorsForNonTerminator(mlir::Operation *op) {
  llvm::SmallVector<mlir::Operation *> ret;
  ret.push_back(op->getNextNode());

  if (auto regionBranchOp = mlir::dyn_cast<mlir::RegionBranchOpInterface>(op)) {
    llvm::SmallVector<mlir::RegionSuccessor> succs;
    regionBranchOp.getSuccessorRegions(mlir::RegionBranchPoint::parent(),
                                       succs);
    for (auto &succ : succs)
      ret.push_back(&*succ.getSuccessor()->op_begin());
  }

  return ret;
}

} // namespace

llvm::SmallVector<mlir::Operation *> cir::getSuccessors(mlir::Operation *op) {
  if (!op->hasTrait<mlir::OpTrait::IsTerminator>())
    return getSuccessorsForNonTerminator(op);

  // If the termination op doesn't have successors, it must be one of break,
  // continue, yield, condition or return. They might have special semantics.
  if (!op->hasSuccessors()) {
    if (auto ret = mlir::dyn_cast<ReturnOp>(op))
      return {};

    if (auto breakOp = mlir::dyn_cast<BreakOp>(op))
      return getSuccessorsForBreakOp(breakOp);

    if (auto yieldOp = mlir::dyn_cast<YieldOp>(op))
      return getSuccessorsForYieldOp(yieldOp);

    if (auto continueOp = mlir::dyn_cast<ContinueOp>(op))
      return getSuccessorsForContinueOp(continueOp);

    if (auto conditionOp = mlir::dyn_cast<ConditionOp>(op))
      return getSuccessorsForConditionOp(conditionOp);

#ifndef NDEBUG

    op->dump();

#endif

    llvm_unreachable("any other terminating op not listed above?");
  }

  llvm::SmallVector<mlir::Operation *> ret;
  for (auto *bb : op->getSuccessors()) {
    if (bb->empty())
      continue;

    ret.push_back(&bb->front());
  }

  return ret;
}

unsigned ReachableAnalyzer::getOpDepth(mlir::Operation *op) {
  unsigned depth = 0;
  while (op && !mlir::isa<FuncOp>(op)) {
    depth++;
    op = op->getParentOp();
  }
  assert(op && "We shouldn't calculate depth of a non-func op");
  return depth;
}

bool ReachableAnalyzer::isReachableInSameBlock(mlir::Operation *from,
                                               mlir::Operation *to) {
  while (!from->hasTrait<mlir::OpTrait::IsTerminator>()) {
    if (from == to)
      return true;

    from = from->getNextNode();
  }

  return false;
}

bool ReachableAnalyzer::isReachable(mlir::Operation *fromOp,
                                    mlir::Operation *toOp) {
  assert(fromOp && toOp);

  if (&fromOp == &toOp)
    return true;

  if (fromOp->getBlock() == toOp->getBlock() &&
      isReachableInSameBlock(fromOp, toOp))
    return true;

  if (fromOp->getParentOp() != toOp->getParentOp()) {
    unsigned fromDepth = getOpDepth(fromOp);
    unsigned toDepth = getOpDepth(toOp);

    while (fromDepth != toDepth) {
      if (fromDepth > toDepth) {
        if (canBBReachParentRegion(fromOp->getBlock())) {
          fromDepth--;
          fromOp = fromOp->getParentOp();
          continue;
        }

        return false;
      }

      // Reach the toBlock if it can be reached by the entry of its containing
      // region. Assuming that every region inside an op can be reached.
      if (canBBReachedByRegionEntry(toOp->getBlock())) {
        toDepth--;
        toOp = toOp->getParentOp();
        continue;
      }

      return false;
    }

    while (fromOp->getParentOp() != toOp->getParentOp()) {
      if (mlir::isa<FuncOp>(fromOp->getParentOp()) ||
          mlir::isa<FuncOp>(toOp->getParentOp()))
        return false;

      if (canBBReachParentRegion(fromOp->getBlock())) {
        fromOp = fromOp->getParentOp();
      } else
        return false;

      if (canBBReachedByRegionEntry(toOp->getBlock())) {
        toOp = toOp->getParentOp();
      } else
        return false;
    }

    if (fromOp->getBlock() == toOp->getBlock() &&
        isReachableInSameBlock(fromOp, toOp))
      return true;
  }

  return isReachableInSameOp(fromOp->getBlock(), toOp->getBlock());
}

bool ReachableAnalyzer::canBBReachParentRegion(mlir::Block *from) {
  mlir::Region *parentRegion = from->getParentOp()->getParentRegion();

  std::queue<mlir::Block *> worklist;
  worklist.push(from);
  llvm::DenseSet<mlir::Block *> visited;
  visited.insert(from);

  while (!worklist.empty()) {
    mlir::Block *cur = worklist.front();
    worklist.pop();

    mlir::Operation *term = cur->getTerminator();
    if (!term)
      continue;

    auto succs = getSuccessors(term);
    for (auto *succ : succs) {
      if (succ->getParentRegion() == parentRegion)
        return true;

      auto *succBB = succ->getBlock();
      if (visited.insert(succBB).second)
        worklist.push(succBB);
    }
  }

  return false;
}

// Analyze if to can be reached by the entry of the parent region.
bool ReachableAnalyzer::canBBReachedByRegionEntry(mlir::Block *to) {
  mlir::Region *region = to->getParent();
  mlir::Block *entry = region->op_begin()->getBlock();

  return isReachableInSameOp(entry, to);
}

bool ReachableAnalyzer::isReachableInSameOp(mlir::Block *from,
                                            mlir::Block *to) {
  assert(from->getParentOp() == to->getParentOp());
  auto *parentOp = from->getParentOp();

  std::queue<mlir::Block *> worklist;
  worklist.push(from);
  llvm::DenseSet<mlir::Block *> visited;
  visited.insert(from);

  while (!worklist.empty()) {
    mlir::Block *cur = worklist.front();
    worklist.pop();

    mlir::Operation *term = cur->getTerminator();
    if (!term)
      continue;

    auto succs = getSuccessors(term);
    for (auto *succ : succs) {
      auto *succBB = succ->getBlock();

      if (succBB == to)
        return true;

      // We don't need to search beyond the parent Op.
      if (succBB->getParentOp() != parentOp)
        continue;

      if (visited.insert(succBB).second)
        worklist.push(succBB);
    }
  }

  return false;
}
