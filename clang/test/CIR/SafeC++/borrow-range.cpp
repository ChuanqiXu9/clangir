// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only
#pragma clang SafeCXX
void consume(const int &);
int f() {
    int a;
    const int &ref = a;
    consume(ref);
    a = 43;
    return a;
}

int g() {
    int a;
    const int &ref = a; // expected-note {{the borrow starts from}}
    a = 43; // expected-warning {{a change to a is detected when borrowed by ref}}
    consume(ref);
    return a;
}

