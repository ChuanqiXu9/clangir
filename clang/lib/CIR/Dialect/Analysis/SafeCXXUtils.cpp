//===- SafeCXXUtils.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SafeCXXUtils.h"
#include "mlir/IR/Dialect.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/DiagnosticSafeCXX.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace cir;

namespace {
SourceLocation translate(const ASTContext &ctx, mlir::FileLineColLoc mlirLoc) {
  if (!mlirLoc.getLine() || !mlirLoc.getColumn())
    return {};

  const auto &srcMgr = ctx.getSourceManager();
  auto &fileMgr = const_cast<FileManager &>(srcMgr.getFileManager());
  llvm::Expected<FileEntryRef> errOrFile =
      fileMgr.getFileRef(mlirLoc.getFilename());
  if (errOrFile.takeError())
    return {};
  return srcMgr.translateFileLineCol(&errOrFile->getFileEntry(),
                                     mlirLoc.getLine(), mlirLoc.getColumn());
}

SourceLocation translate(const ASTContext &ctx, mlir::Location loc);
SourceLocation translate(const ASTContext &ctx, mlir::FusedLoc loc) {
  if (loc.getLocations().empty())
    return {};
  return translate(ctx, loc.getLocations()[0]);
}

SourceLocation translate(const ASTContext &ctx, mlir::Location loc) {
  if (auto flc = dyn_cast<mlir::FileLineColLoc>(loc))
    return translate(ctx, flc);

  if (auto fL = dyn_cast<mlir::FusedLoc>(loc))
    return translate(ctx, fL);

#ifndef NDEBUG
  loc.dump();
  llvm_unreachable("unhandled location type");
#endif

  return {};
}
} // namespace

DiagnosticBuilder cir::Diag(const ASTContext &ctx, mlir::Location loc,
                            unsigned diagID) {
  return ctx.getDiagnostics().Report(translate(ctx, loc), diagID);
}
bool cir::isSafeLoc(const ASTContext &ctx, mlir::Location loc) {
  return ctx.getSafeCXXState().isSafeCXXState(ctx.getSourceManager(),
                                              translate(ctx, loc));
}
