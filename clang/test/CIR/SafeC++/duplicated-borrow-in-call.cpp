// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only
#pragma clang SafeCXX
void consume(int &x, int &y);
void func() {
    int x;
    consume(x, x); // expected-warning {{non constant borrow x overlapped with other borrow}}
}
