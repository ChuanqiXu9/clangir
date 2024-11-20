// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only
// expected-no-diagnostics
struct S {
    int *x;

    S();
    S(const S &other);
    S &operator=(const S &other);

    void get(int &x [[clang::NoBorrowToRet]]);
    void consume() const;
};

#pragma clang SafeCXX
void invalid(bool cond) {
  S s;
  if (cond) {
    int a = 0;
    s.get(a);
  }
  s.consume();
}
