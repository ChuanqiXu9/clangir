//===- CFG.h --------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIR_ANALYSIS_CFG_H
#define CIR_ANALYSIS_CFG_H

#include "clang/CIR/Dialect/IR/CIRDialect.h"

namespace cir {
llvm::SmallVector<mlir::Operation *> getSuccessors(mlir::Operation *op);

// FIXME: We shall implement some cache mechanism for this.
class ReachableAnalyzer {
public:
  ReachableAnalyzer(FuncOp) {}

  bool isReachable(mlir::Operation &from, mlir::Operation &to) {
    return isReachable(&from, &to);
  }
  bool isReachable(mlir::Operation *from, mlir::Operation *to);

  bool isReachable(mlir::Block &from, mlir::Block &to) {
    assert(!from.empty() && !to.empty());
    return isReachable(from.front(), to.front());
  }
  bool isReachable(mlir::Block *from, mlir::Block *to) {
    assert(from);
    assert(to);
    return isReachable(*from, *to);
  }

private:
  bool isReachableInSameBlock(mlir::Operation *from, mlir::Operation *to);
  bool isReachableInSameOp(mlir::Block *from, mlir::Block *to);

  // get the depth from the BB to the funcOP.
  unsigned getOpDepth(mlir::Operation *op);

  // If a fromBB can reach the parent region, we can hoist the BB to the region
  // block during the analysis.
  bool canBBReachParentRegion(mlir::Block *bb);
  // If the toBB can be reached by the entry of the region, we can hoist the BB
  // to the region block during the analysis.
  bool canBBReachedByRegionEntry(mlir::Block *bb);
};
} // namespace cir

#endif
