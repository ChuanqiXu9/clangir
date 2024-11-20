// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only -Wno-return-stack-address
#pragma clang SafeCXX
int &func() {
    int i;
    return i; // expected-warning {{return during borrowing for i may produce dangling reference}}
              // expected-note@-1 {{the previous borrow starts from}}
}

const int &func2() {
    int i;
    return i; // expected-warning {{return during borrowing for i may produce dangling reference}}
              // expected-note@-1 {{the previous borrow starts from}}
}

struct S {
    int *x;
    int *y;
};

S getS(const int &x, const int &y);
S test() {
    int x, y;
    return getS(x, y); // expected-warning {{return during borrowing for x may produce dangling reference}}
                       // expected-warning@-1 {{return during borrowing for y may produce dangling reference}}
                       // expected-note@-2 + {{the previous borrow starts from}}
}
