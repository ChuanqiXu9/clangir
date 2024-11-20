// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only

#pragma clang SafeCXX

struct S {
    int &x;

    S(int x);
    S(const S &other);
    S &operator=(const S &other);

    const S& get() const;
    void update(const int &x);
};

void consume(const S&);
void func(int x) {
    S s(43);
    auto &ref = s.get(); // expected-note {{the previous borrow starts from}}
    s.update(x); // expected-warning {{non constant borrow s overlapped with other borrow}}
    consume(ref);
}

