// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only  -Wno-return-stack-address
// RUN: 
#pragma clang SafeCXX
auto getLambda() {
    int x = 43;
    int y = 43;
    return [&]() { return x + y; }; // expected-warning {{return during borrowing for x may produce dangling reference}}
                                    // expected-warning@-1 {{return during borrowing for y may produce dangling reference}}
                                    // expected-note@-2 1+{{the previous borrow starts from}}
}
