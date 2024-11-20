//===- SafeCXXState.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_SAFE_CXX_STATE_H
#define LLVM_CLANG_BASIC_SAFE_CXX_STATE_H

#include "clang/Basic/SourceLocation.h"
#include <map>

namespace clang {

class SafeCXXState {
public:
  void AddSafeCXXState(const SourceManager &SrcMgr, SourceLocation L) const {
    AddSafeStateImpl(SrcMgr, L, /*IsSafe=*/true);
  }
  void AddUnsafeCXXState(const SourceManager &SrcMgr, SourceLocation L) const {
    AddSafeStateImpl(SrcMgr, L, /*IsSafe=*/false);
  }

  bool isSafeCXXState(const SourceManager &SrcMgr, SourceLocation L) const;
  bool isUnsafeCXXState(const SourceManager &SrcMgr, SourceLocation L) const {
    return !isSafeCXXState(SrcMgr, L);
  }

  bool hasAnySafeState() const;

private:
  void AddSafeStateImpl(const SourceManager &SrcMgr, SourceLocation L,
                        bool IsSafe) const;

  struct SafeStatePoint {
    bool Safe;
    unsigned Offset;
  };

  struct FileInfo {
    llvm::SmallVector<SafeStatePoint, 4> StateTransitions;

    bool isSafe(unsigned Offset) const;
  };

  mutable std::map<FileID, FileInfo> Files;

  FileInfo &getFileInfo(FileID ID) const;
};
} // namespace clang

#endif
