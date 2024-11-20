// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only

struct S {
    int *x;

    S();
    S(const S &other);
    S &operator=(const S &other);

    void get(int &x);
    void consume() const;
};

#pragma clang SafeCXX
void invalid(bool cond) {
  S s;
  if (cond) {
    int a = 0;
    s.get(a); // expected-note {{the previous borrow starts from}}
              // expected-warning@-1 {{borrow a has shorter lifetime than s}}
  }
  s.consume(); // expected-warning {{use of s detected beyond lifetime of borrow a}}
}
