//===- DeprecatedCallCheck.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SafeCXXUtils.h"
#include "Utils.h"

#include "../Transforms/PassDetail.h"
#include "clang/Basic/DiagnosticSafeCXX.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"

#include "llvm/Demangle/Demangle.h"

#include "llvm/Support/Regex.h"

using namespace cir;
using namespace clang;

namespace {
struct DeprecatedCallCheckPass
    : public mlir::DeprecatedCallCheckBase<DeprecatedCallCheckPass> {
  DeprecatedCallCheckPass() = default;
  DeprecatedCallCheckPass(ASTContext &Ctx) : astCtx(&Ctx) {}

  void checkNoNewDelete(CallOp op) {
    if (!op.getCallee())
      return;

    // If it is not calling new or delete, we can skip it.
    //
    // If we have the AST node, use the information.
    if (op.getAst()) {
      if (!op.getAst()->isAllocationOrDealocationCall())
        return;
    } else {
      // Or we might have to demangle the name since not every CallOp has an
      // ast part.
      std::string demangled = llvm::demangle(*op.getCallee());
      StringRef demangleRef(demangled);
      if (!demangleRef.starts_with("operator new") &&
          !demangleRef.starts_with("operator delete"))
        return;
    }

    Diag(*astCtx, op.getLoc(), diag::warn_no_new_delete_in_safe_cxx);
  }

  bool match(StringRef regex, StringRef string) {
    return llvm::Regex(regex).match(string);
  }

  void checkHardCodedPattern(CallOp op) {
    if (!op.getCallee())
      return;

    std::string demangled = llvm::demangle(*op.getCallee());
    StringRef demangleRef(demangled);

    // FIXME: Hard coded pattern for demo. We should move this to a
    // configuration file.
    if (match(R"cpp(std::unique_ptr<.*>::unique_ptr.*\(\))cpp", demangleRef) ||
        match(R"cpp(std::unique_ptr<.*>::unique_ptr.*\(std::nullptr_t\))cpp",
              demangleRef)) {
      Diag(*astCtx, op.getLoc(), diag::warn_deprecated_call) << demangled;
    }

    // Check for .reset() or .reset(nullptr) pattern.
    if (match(R"cpp(std::unique_ptr<.*>::reset\()cpp", demangleRef) &&
        op.getArgOps().size() > 1) {
      auto p =
          mlir::dyn_cast<cir::ConstantOp>(op.getArgOps()[1].getDefiningOp());
      if (!p)
        return;

      if (!isa<cir::PointerType>(p.getType()))
        return;

      auto v = dyn_cast<cir::ConstPtrAttr>(p.getValue());
      if (!v)
        return;

      if (v.getValue().getInt() == 0) {
        Diag(*astCtx, op.getLoc(), diag::warn_deprecated_call) << demangled;
      }
    }
  }

  void checkDeprecatedAttr(CallOp op) {
    if (!op.getCallee())
      return;

    auto callAst = op.getAst();
    if (!callAst)
      return;

    // FIXME: It looks like CIR didn't attach the AST decl for constructor. See
    // test/CIR/SafeC++/deprecated.cpp for example.
    auto *callee = getCallee(op);
    if (!callee)
      return;

    std::string demangled = llvm::demangle(*op.getCallee());

    if (callee->hasAttr<clang::SafeCXXDeprecatedAttr>()) {
      Diag(*astCtx, op.getLoc(), diag::warn_deprecated_call) << demangled;
    }
  }

  void runOnOperation() override {
    getOperation()->walk([&](cir::CallOp op) {
      if (!isSafeLoc(*astCtx, op.getLoc()))
        return;

      checkNoNewDelete(op);
      checkHardCodedPattern(op);
      checkDeprecatedAttr(op);
    });
  }

  ASTContext *astCtx = nullptr;
};
} // namespace

std::unique_ptr<mlir::Pass> mlir::createDeprecatedCallCheckPass() {
  return std::make_unique<DeprecatedCallCheckPass>();
}

std::unique_ptr<mlir::Pass>
mlir::createDeprecatedCallCheckPass(ASTContext &astCtx) {
  return std::make_unique<DeprecatedCallCheckPass>(astCtx);
}
