//===--- SafeCXXASTConsumer.cpp - Consumer for Safe C++ -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/CIRGenerator.h"
#include "clang/CIR/CIRToCIRPasses.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/FrontendAction/CIRGenConsumer.h"
#include "clang/CIR/LowerToLLVM.h"
#include "clang/CIR/Passes.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"

#include "llvm/Support/Signals.h"
#include "llvm/Support/TimeProfiler.h"

using namespace cir;
using namespace clang;

// FIXME: Frontend can't dependent on clangCIRFrontendAction since
// clangCIRFrontendAction depends on Frontend.

void CIRGenConsumerBase::anchor() {}
CIRGenConsumerBase::CIRGenConsumerBase(
    DiagnosticsEngine &diagnosticsEngine,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
    const FrontendOptions &feOptions, const CodeGenOptions &codeGenOptions)
    : diagnosticsEngine(diagnosticsEngine), feOptions(feOptions),
      codeGenOptions(codeGenOptions), FS(VFS) {}

void CIRGenConsumerBase::Initialize(ASTContext &ctx) {
  assert(!astContext && "initialized multiple times");

  astContext = &ctx;

  gen->Initialize(ctx);
}

bool CIRGenConsumerBase::HandleTopLevelDecl(DeclGroupRef D) {
  PrettyStackTraceDecl CrashInfo(*D.begin(), SourceLocation(),
                                 astContext->getSourceManager(),
                                 "LLVM IR generation of declaration");
  gen->HandleTopLevelDecl(D);
  return true;
}

void CIRGenConsumerBase::HandleCXXStaticMemberVarInstantiation(
    clang::VarDecl *VD) {
  gen->HandleCXXStaticMemberVarInstantiation(VD);
}

void CIRGenConsumerBase::HandleInlineFunctionDefinition(FunctionDecl *D) {
  gen->HandleInlineFunctionDefinition(D);
}

void CIRGenConsumerBase::HandleInterestingDecl(DeclGroupRef D) {
  llvm_unreachable("NYI");
}

void CIRGenConsumerBase::HandleTagDeclDefinition(TagDecl *D) {
  PrettyStackTraceDecl CrashInfo(D, SourceLocation(),
                                 astContext->getSourceManager(),
                                 "CIR generation of declaration");
  gen->HandleTagDeclDefinition(D);
}

void CIRGenConsumerBase::HandleTagDeclRequiredDefinition(const TagDecl *D) {
  gen->HandleTagDeclRequiredDefinition(D);
}

void CIRGenConsumerBase::CompleteTentativeDefinition(VarDecl *D) {
  gen->CompleteTentativeDefinition(D);
}

void CIRGenConsumerBase::CompleteExternalDeclaration(DeclaratorDecl *D) {
  llvm_unreachable("NYI");
}

void CIRGenConsumerBase::AssignInheritanceModel(CXXRecordDecl *RD) {
  llvm_unreachable("NYI");
}

void CIRGenConsumerBase::HandleVTable(CXXRecordDecl *RD) {
  gen->HandleVTable(RD);
}

void CIRGenConsumerBase::runCIRPasses(ASTContext &C,
                                      mlir::MLIRContext *mlirCtx) {
  if (feOptions.ClangIRDisablePasses)
    return;

  // Handle source manager properly given that lifetime analysis
  // might emit warnings and remarks.
  auto &clangSourceMgr = C.getSourceManager();
  FileID MainFileID = clangSourceMgr.getMainFileID();

  std::unique_ptr<llvm::MemoryBuffer> FileBuf =
      llvm::MemoryBuffer::getMemBuffer(
          clangSourceMgr.getBufferOrFake(MainFileID));

  llvm::SourceMgr mlirSourceMgr;
  mlirSourceMgr.AddNewSourceBuffer(std::move(FileBuf), llvm::SMLoc());

  if (feOptions.ClangIRVerifyDiags) {
    mlir::SourceMgrDiagnosticVerifierHandler sourceMgrHandler(mlirSourceMgr,
                                                              mlirCtx);
    mlirCtx->printOpOnDiagnostic(false);
    setupCIRPipelineAndExecute(C, mlirCtx);

    // Verify the diagnostic handler to make sure that each of the
    // diagnostics matched.
    if (sourceMgrHandler.verify().failed()) {
      // FIXME: we fail ungracefully, there's probably a better way
      // to communicate non-zero return so tests can actually fail.
      llvm::sys::RunInterruptHandlers();
      exit(1);
    }
  } else {
    mlir::SourceMgrDiagnosticHandler sourceMgrHandler(mlirSourceMgr, mlirCtx);
    setupCIRPipelineAndExecute(C, mlirCtx);
  }
}

class SafeCXXASTConsumer : public CIRGenConsumerBase {
  llvm::SmallVector<DeclGroupRef> TopLevelDecls;
  llvm::SmallVector<VarDecl *> CXXStaticMemberVarInstantiations;
  llvm::SmallVector<FunctionDecl *> InlineFunctionDefinitions;
  llvm::SmallVector<DeclGroupRef> InterestingDecls;
  llvm::SmallVector<TagDecl *> TagDeclDefinition;
  llvm::SmallVector<TagDecl *> TagDeclRequiredDefinitions;
  llvm::SmallVector<VarDecl *> TentativeDefinitions;
  llvm::SmallVector<DeclaratorDecl> ExternalDeclarations;
  llvm::SmallVector<CXXRecordDecl *> InheritanceModel;
  llvm::SmallVector<CXXRecordDecl *> VTables;

  using Base = CIRGenConsumerBase;

  // We only care about safe decl.
  bool isDeclSafe(const Decl *D) const {
    if (!D)
      return false;
    return astContext->getSafeCXXState().isSafeCXXState(
        astContext->getSourceManager(), D->getLocation());
  }
  bool isDeclGroupSafe(DeclGroupRef DGR) const {
    return llvm::any_of(DGR, [this](Decl *D) { return isDeclSafe(D); });
  }

  void initCIRGenerator() {
    if (gen)
      return;

    gen = std::make_unique<CIRGenerator>(diagnosticsEngine, FS, codeGenOptions);

    if (astContext)
      gen->Initialize(*astContext);
  }

  bool ifSafeThenInit(const Decl *D) {
    bool Ret = isDeclSafe(D);
    if (Ret)
      initCIRGenerator();

    return Ret;
  }
  bool ifSafeThenInit(DeclGroupRef D) {
    bool Ret = isDeclGroupSafe(D);
    if (Ret)
      initCIRGenerator();

    return Ret;
  }

public:
  SafeCXXASTConsumer(clang::DiagnosticsEngine &diagnosticsEngine,
                     llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
                     const clang::FrontendOptions &feOptions,
                     const clang::CodeGenOptions &codeGenOptions)
      : Base(diagnosticsEngine, VFS, feOptions, codeGenOptions) {}

  void Initialize(clang::ASTContext &ctx) override {
    astContext = &ctx;
    gen.reset();
  }

  bool HandleTopLevelDecl(clang::DeclGroupRef D) override {
    if (ifSafeThenInit(D))
      return Base::HandleTopLevelDecl(D);

    return true;
  }

  void HandleCXXStaticMemberVarInstantiation(clang::VarDecl *VD) override {
    if (ifSafeThenInit(VD))
      Base::HandleCXXStaticMemberVarInstantiation(VD);
  }

  void HandleInlineFunctionDefinition(clang::FunctionDecl *D) override {
    if (ifSafeThenInit(D))
      Base::HandleInlineFunctionDefinition(D);
  }

  void HandleInterestingDecl(clang::DeclGroupRef D) override {
    if (ifSafeThenInit(D))
      Base::HandleInterestingDecl(D);
  }

  void HandleTagDeclDefinition(clang::TagDecl *D) override {
    if (ifSafeThenInit(D))
      Base::HandleTagDeclDefinition(D);
  }

  void HandleTagDeclRequiredDefinition(const clang::TagDecl *D) override {
    if (ifSafeThenInit(D))
      Base::HandleTagDeclRequiredDefinition(D);
  }

  void CompleteTentativeDefinition(clang::VarDecl *D) override {
    if (ifSafeThenInit(D))
      Base::CompleteTentativeDefinition(D);
  }

  void CompleteExternalDeclaration(clang::DeclaratorDecl *D) override {
    if (ifSafeThenInit(D))
      Base::CompleteExternalDeclaration(D);
  }

  void AssignInheritanceModel(clang::CXXRecordDecl *RD) override {
    if (ifSafeThenInit(RD))
      Base::AssignInheritanceModel(RD);
  }

  void HandleVTable(clang::CXXRecordDecl *RD) override {
    if (ifSafeThenInit(RD))
      Base::HandleVTable(RD);
  }

  void setupCIRPipelineAndExecute(clang::ASTContext &C,
                                  mlir::MLIRContext *mlirCtx) override;

  void HandleTranslationUnit(clang::ASTContext &C) override;
};

void SafeCXXASTConsumer::setupCIRPipelineAndExecute(
    ASTContext &C, mlir::MLIRContext *mlirCtx) {
  auto mlirMod = gen->getModule();

  // Sanitize passes options. MLIR uses spaces between pass options
  // and since that's hard to fly in clang, we currently use ';'.
  std::string lifetimeOpts, idiomRecognizerOpts, libOptOpts;
  if (feOptions.ClangIRLifetimeCheck)
    lifetimeOpts = sanitizePassOptions(feOptions.ClangIRLifetimeCheckOpts);
  if (feOptions.ClangIRIdiomRecognizer)
    idiomRecognizerOpts =
        sanitizePassOptions(feOptions.ClangIRIdiomRecognizerOpts);

  // Setup and run CIR pipeline.
  std::string passOptParsingFailure;

  if (feOptions.SafeCXXDump)
    mlirMod->dump();

  if (runSafeCXXPasses(mlirMod, mlirCtx, C, lifetimeOpts, idiomRecognizerOpts,
                       !feOptions.ClangIRDisableCIRVerifier,
                       passOptParsingFailure)
          .failed()) {
    if (!passOptParsingFailure.empty())
      diagnosticsEngine.Report(diag::err_drv_cir_pass_opt_parsing)
          << feOptions.ClangIRLifetimeCheckOpts;
    else
      llvm::report_fatal_error("CIR codegen: MLIR pass manager fails "
                               "when running CIR passes!");
  }
}

void SafeCXXASTConsumer::HandleTranslationUnit(ASTContext &C) {
  if (!C.getSafeCXXState().hasAnySafeState())
    return;

  llvm::TimeTraceScope scope("Safe C++ gen");

  // Note that this method is called after `HandleTopLevelDecl` has already
  // ran all over the top level decls. Here clang mostly wraps defered and
  // global codegen, followed by running CIR passes.
  initCIRGenerator();
  gen->HandleTranslationUnit(C);

  if (!feOptions.ClangIRDisableCIRVerifier)
    if (!gen->verifyModule()) {
      llvm::report_fatal_error(
          "CIR codegen: module verification error before running CIR passes");
      return;
    }

  auto mlirCtx = gen->takeContext();
  runCIRPasses(C, mlirCtx.get());
}

std::unique_ptr<ASTConsumer>
clang::CreateSafeCXXConsumer(clang::CompilerInstance &ci) {
  return std::make_unique<SafeCXXASTConsumer>(
      ci.getDiagnostics(), &ci.getVirtualFileSystem(), ci.getFrontendOpts(),
      ci.getCodeGenOpts());
}
