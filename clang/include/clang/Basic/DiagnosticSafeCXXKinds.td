//==--- DiagnosticSafeCXXKinds.td - safe C++ diagnostics ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//


let Component = "SafeCXX" in {
let CategoryName = "Safe C++ Issue" in {
def warn_no_new_delete_in_safe_cxx : Warning<
  "raw new or delete operator is not allowed in safe C++ mode">,
  InGroup<DiagGroup<"safe-cxx-no-new-delete">>;
}
def warn_change_during_borrowing : Warning<
  "a change to %0 is detected when borrowed by %1">,
  InGroup<DiagGroup<"safe-cxx-change-during-borrowing">>;
def note_borrow_starts_from : Note<
  "the borrow starts from">;
def warn_multiple_non_const_borrow : Warning<
  "non constant borrow %0 overlapped with other borrow">,
  InGroup<DiagGroup<"safe-cxx-multiple-non-const-borrow">>;
def note_previous_borrow_starts_from : Note<
  "the previous borrow starts from">;

def warn_read_during_non_const_borrowing : Warning<
  "read for %0 detected during non const borrow for %1">,
  InGroup<DiagGroup<"safe-cxx-read-during-non-constant-borrowing">>;

def warn_dangling_references : Warning<
  "dangling reference to %0">,
  InGroup<DiagGroup<"safe-cxx-dangling-references">>;

def warn_return_during_borrowing : Warning <
  "return during borrowing for %0 may produce dangling reference">,
  InGroup<DiagGroup<"safe-cxx-return-during-borrowing">>;

def warn_no_deref : Warning <
  "dereferencing is not allowed in safe C++; deferencing for %0 detected">,
  InGroup<DiagGroup<"safe-cxx-no-deref">>;

def warn_use_beyond_lifetime_of_borrow : Warning <
  "use of %0 detected beyond lifetime of borrow %1">,
  InGroup<DiagGroup<"safe-cxx-use-beyond-lifetime-of-borrow">>;

def warn_deprecated_call : Warning <
  "call to %0 is deprecated in safe C++">,
  InGroup<DiagGroup<"safe-cxx-deprecated-call">>;

def warn_shorted_lifetime_borrow : Warning <
  "borrow %0 has shorter lifetime than %1">,
  InGroup<DiagGroup<"safe-cxx-shorter-lifetime-borrow">>;
} // end of safe cxx component.
