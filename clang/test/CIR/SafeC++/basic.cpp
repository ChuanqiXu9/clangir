// RUN: rm -rf %t
// RUN: mkdir %t
// RUN: split-file %s %t
//
// RUN: %clang_cc1 -x c++ %t/safe.cc -verify -fsyntax-only
// RUN: %clang_cc1 -x c++ %t/unsafe.cc -verify -fsyntax-only

//--- unsafe.h
void d0(int *v) {
    delete v;
}

//--- safe.cc
// Test that we won't emit errors in included headers which are not marked as safe.
// expected-no-diagnostics
#pragma clang SafeCXX
#include "unsafe.h"

//--- safe.h
#pragma clang SafeCXX
void d0(int *v) { // expected-warning {{raw new or delete operator is not allowed in safe C++ mode}}
    delete v;
}

//--- unsafe.cc
#pragma clang UnsafeCXX
#include "safe.h"
