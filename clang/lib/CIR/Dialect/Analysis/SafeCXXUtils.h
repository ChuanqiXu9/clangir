//===- SafeCXXUtils.h -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIR_ANALYSIS_SAFEUTILS_H
#define CIR_ANALYSIS_SAFEUTILS_H

#include "mlir/IR/Location.h"
#include "clang/Basic/Diagnostic.h"

namespace clang {
class ASTContext;
}

namespace cir {
clang::DiagnosticBuilder Diag(const clang::ASTContext &ctx, mlir::Location loc,
                              unsigned diagID);
bool isSafeLoc(const clang::ASTContext &ctx, mlir::Location loc);
} // namespace cir

#endif
