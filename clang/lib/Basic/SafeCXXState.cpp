//===- SafeCXXState.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/SafeCXXState.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;

void SafeCXXState::AddSafeStateImpl(const SourceManager &SrcMgr,
                                    SourceLocation L, bool IsSafe) const {
  std::pair<FileID, unsigned> Decomp = SrcMgr.getDecomposedLoc(L);
  unsigned Offset = Decomp.second;

  FileInfo &F = getFileInfo(Decomp.first);
  F.StateTransitions.push_back({IsSafe, Offset});
}

bool SafeCXXState::isSafeCXXState(const SourceManager &SrcMgr,
                                  SourceLocation L) const {
  if (Files.empty())
    return false;

  std::pair<FileID, unsigned> Decomp = SrcMgr.getDecomposedLoc(L);
  const FileInfo &F = getFileInfo(Decomp.first);
  return F.isSafe(Decomp.second);
}

bool SafeCXXState::FileInfo::isSafe(unsigned Offset) const {
  if (StateTransitions.empty())
    return false;

  // Get the first point which is behind of the offset and get the safety
  // property from the previous point.
  auto Point =
      llvm::partition_point(StateTransitions, [=](const SafeStatePoint &P) {
        return P.Offset <= Offset;
      });

  if (Point == StateTransitions.begin())
    return false;

  if (Point == StateTransitions.end())
    return StateTransitions.back().Safe;

  return Point[-1].Safe;
}

bool SafeCXXState::hasAnySafeState() const {
  if (Files.empty())
    return false;

  return llvm::any_of(Files, [](const auto &F) {
    return llvm::any_of(F.second.StateTransitions,
                        [](const SafeStatePoint &P) { return P.Safe; });
  });
}

SafeCXXState::FileInfo &SafeCXXState::getFileInfo(FileID ID) const {
  auto Iter = Files.find(ID);
  if (Iter != Files.end())
    return Iter->second;

  return Files.insert({ID, FileInfo{}}).first->second;
}
