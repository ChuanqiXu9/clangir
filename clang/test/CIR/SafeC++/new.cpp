// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only

int *f0() {
    return new int(43);
}

void d0(int *v) {
    delete v;
}

int *func0();
int *n0() {
    return func0();
}

#pragma clang SafeCXX
int *f() { // expected-warning {{raw new or delete operator is not allowed in safe C++ mode}}
    return new int(43);
}

void d(int *v) { // expected-warning {{raw new or delete operator is not allowed in safe C++ mode}}
    delete v;
}

int *func();
int *n() {
    return func();
}

#pragma clang UnsafeCXX
int *f1() {
    return new int(43);
}

void d1(int *v) {
    delete v;
}

int *func2();
int *n2() {
    return func2();
}
