// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only

#pragma clang SafeCXX
struct S {
    int &x;

    S(const int &x);
    S(const S &other);
    S &operator=(const S &other);

    void get(int &x);
    void consume() const;
};
S getS(int &x);
void consume(const S&);
void invalid(bool cond) {
  S s(43);
  if (cond) {
    int a = 0;
    s = getS(a); // expected-note {{the previous borrow starts from}}
                 // expected-warning@-1 {{borrow ref.tmp1 has shorter lifetime than s}}
  }
  consume(s); // expected-warning {{use of s detected beyond lifetime of borrow}}
}

void invalid2(bool cond) {
  S s(43);
  int a = 0;
  // There is a temporary.
  if (cond)
    s = getS(a); // expected-note {{the previous borrow starts from}}
                 // expected-warning@-1 {{borrow ref.tmp1 has shorter lifetime than s}}

  consume(s); // expected-warning {{use of s detected beyond lifetime of borrow}}
}

void invalid3(bool cond) {
  S s(43);
  if (cond) {
    int a = 0;
    s.get(a); // expected-note {{the previous borrow starts from}}
              // expected-warning@-1 {{borrow a has shorter lifetime than s}}
  }
  s.consume(); // expected-warning {{use of s detected beyond lifetime of borrow a}}
}

