// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only
// expected-no-diagnostics
#pragma clang SafeCXX
struct S {
    int &x;

    S(const int &x);
    S(const S &other [[clang::NoBorrowToRet]]);
    S &operator=(const S &other [[clang::NoBorrowToRet]]);
};
S getS(int &x);
void consume(const S&);

void valid(bool cond) {
  S s(43);
  int a = 0;
  // There is a temporary.
  if (cond)
    s = getS(a);

  consume(s);
}

struct S2 {
    int &x;

    S2(const int &x);
    [[clang::NoBorrowToRet]] S2(const S2 &other);
    [[clang::NoBorrowToRet]] S2 &operator=(const S2 &other);
};
S2 getS2(int &x);
void consume(const S2&);

void valid2(bool cond) {
  S2 s(43);
  int a = 0;
  // There is a temporary.
  if (cond)
    s = getS2(a);

  consume(s);
}
