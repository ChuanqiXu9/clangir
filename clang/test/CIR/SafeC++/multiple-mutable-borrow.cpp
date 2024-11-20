// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only

#pragma clang SafeCXX
void consume(int &);
int f() {
    int a;
    int &ref1 = a; // expected-warning {{non constant borrow a overlapped with other borrow}}
    int &ref2 = a; // expected-note {{the previous borrow starts from}}
    consume(ref1);
    consume(ref2);
    return a;
}

void consume2(int &, int &);
void g() {
    int a;
    consume2(a, a); // expected-warning {{non constant borrow a overlapped with other borrow}}
}

void consume(const int &);
int h() {
    int a;
    const int &ref1 = a; // expected-note {{the previous borrow starts from}}
    int &ref2 = a; // expected-warning {{non constant borrow a overlapped with other borrow}}
    consume(ref1);
    consume(ref2);
    return a;
}
