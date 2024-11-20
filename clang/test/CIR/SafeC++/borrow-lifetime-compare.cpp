// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only

struct S {
    int *x;

    S();
    S(const S &other);
    S &operator=(const S &other);

    void get(int &v [[clang::NoBorrowToRet]]);
    void get2(int &v);
};

#pragma clang SafeCXX
void S::get(int &v) {
    x = &v; // expected-warning {{borrow v has shorter lifetime than this}}
}
void S::get2(int &v) {
    x = &v;
}
