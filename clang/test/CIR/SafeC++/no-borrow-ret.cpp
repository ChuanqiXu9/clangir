// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only
struct S {
  int *x;
  int *y;  
};

#pragma clang SafeCXX

S getS(const int &x, const int &y);
S testValid(const int &x, const int &y) {
    return getS(x, y);
}

// Every reference has its own lifetime. We wouldn't chase the use-def chain to found
// the final alloca to decide if its lifetime can fullfill the requirement.
S testInvalid0(const int &x, const int &y) {
    const int &a = x;
    const int &b = y;
    return getS(a, b); // expected-warning {{return during borrowing for a may produce dangling reference}}
                       // expected-warning@-1 {{return during borrowing for b may produce dangling reference}}
                       // expected-note@-2 1+{{the previous borrow starts from}}
}

[[clang::NoBorrowToRet]] 
S testInvalid(const int &x, const int &y) {
    return getS(x, y); // expected-warning {{return during borrowing for x may produce dangling reference}}
                       // expected-warning@-1 {{return during borrowing for y may produce dangling reference}}
                       // expected-note@-2 1+{{the previous borrow starts from}}
}

[[clang::NoBorrowToRet]] 
S testInvalid2(const int &x, const int &y) {
    const int &a = x;
    const int &b = y;
    return getS(a, b); // expected-warning {{return during borrowing for a may produce dangling reference}}
                       // expected-warning@-1 {{return during borrowing for b may produce dangling reference}}
                       // expected-note@-2 1+{{the previous borrow starts from}}
}
