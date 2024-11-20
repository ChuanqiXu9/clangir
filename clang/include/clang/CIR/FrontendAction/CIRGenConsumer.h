//===---- CIRGenConsumerBase.h - CIR Code Generation Consumer --*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclGroup.h"
#include "llvm/Support/VirtualFileSystem.h"

namespace mlir {
class MLIRContext;
}

namespace clang {
class FrontendOptions;
class CodeGenOptions;
} // namespace clang

namespace cir {

class CIRGenerator;

class CIRGenConsumerBase : public clang::ASTConsumer {
protected:
  virtual void anchor();

  clang::DiagnosticsEngine &diagnosticsEngine;

  const clang::FrontendOptions &feOptions;
  const clang::CodeGenOptions &codeGenOptions;

  clang::ASTContext *astContext{nullptr};
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;
  std::unique_ptr<CIRGenerator> gen;

public:
  CIRGenConsumerBase(clang::DiagnosticsEngine &diagnosticsEngine,
                     llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
                     const clang::FrontendOptions &feOptions,
                     const clang::CodeGenOptions &codeGenOptions);

  void Initialize(clang::ASTContext &ctx) override;

  bool HandleTopLevelDecl(clang::DeclGroupRef D) override;

  void HandleCXXStaticMemberVarInstantiation(clang::VarDecl *VD) override;

  void HandleInlineFunctionDefinition(clang::FunctionDecl *D) override;

  void HandleInterestingDecl(clang::DeclGroupRef D) override;

  void HandleTagDeclDefinition(clang::TagDecl *D) override;

  void HandleTagDeclRequiredDefinition(const clang::TagDecl *D) override;

  void CompleteTentativeDefinition(clang::VarDecl *D) override;

  void CompleteExternalDeclaration(clang::DeclaratorDecl *D) override;

  void AssignInheritanceModel(clang::CXXRecordDecl *RD) override;

  void HandleVTable(clang::CXXRecordDecl *RD) override;

  virtual void setupCIRPipelineAndExecute(clang::ASTContext &C,
                                          mlir::MLIRContext *) = 0;

  void runCIRPasses(clang::ASTContext &C, mlir::MLIRContext *);
};

inline std::string sanitizePassOptions(llvm::StringRef o) {
  if (o.empty())
    return "";
  std::string opts{o};
  // MLIR pass options are space separated, but we use ';' in clang since
  // space aren't well supported, switch it back.
  for (unsigned i = 0, e = opts.size(); i < e; ++i)
    if (opts[i] == ';')
      opts[i] = ' ';
  // If arguments are surrounded with '"', trim them off
  return llvm::StringRef(opts).trim('"').str();
}
} // namespace cir
