// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only
// expected-no-diagnostics
#pragma clang SafeCXX

struct S {
    int &x;

    S(int x);
    S(const S &other);
    S &operator=(const S &other);

    [[clang::ThisNoBorrowToRet]] const S& get() const;
    [[clang::ThisNoBorrowToRet]] void update(const int &x);
};

void consume(const S&);
void func(int x) {
    S s(43);
    auto &ref = s.get();
    s.update(x);
    consume(ref);
}

