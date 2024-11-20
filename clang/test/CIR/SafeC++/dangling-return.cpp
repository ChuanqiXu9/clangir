// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only
struct S {
  int *x;
  int *y;  
};

#pragma clang SafeCXX

S getS(const int &x, const int &y);
S test(int x, int y) {
    return getS(x, y); // expected-warning {{return during borrowing for x may produce dangling reference}}
                       // expected-warning@-1 {{return during borrowing for y may produce dangling reference}}
                       // expected-note@-2 1+{{the previous borrow starts from}}
}
