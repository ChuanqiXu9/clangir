add_clang_library(MLIRCIRAnalysis
  BorrowChecker.cpp
  CFG.cpp
  DeprecatedCallCheck.cpp
  SafeCXXUtils.cpp
  Utils.cpp

  DEPENDS
  MLIRCIRPassIncGen

  LINK_LIBS PUBLIC
  clangAST
  clangBasic
  TargetLowering

  MLIRAnalysis
  MLIRIR
  MLIRPass
  MLIRTransformUtils

  MLIRCIR
  MLIRCIRInterfaces
)
