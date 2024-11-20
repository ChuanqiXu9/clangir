// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only
// expected-no-diagnostics
struct S {
  int *x;
  int *y;  
};

#pragma clang SafeCXX

[[clang::NoBorrowToRet]]
S getS(const int &x, const int &y);
S test() {
    int x, y;
    return getS(x, y);
}
