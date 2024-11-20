// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only

#pragma clang SafeCXX
int &maybe_thread(int &x);
void consume(int &);

void f() {
  int x = 0;
  for (int i = 0; i < 10; ++i) {
    int &r = maybe_thread(x); // expected-warning {{non constant borrow r overlapped with other borrow}}
                              // expected-note@-1 {{the previous borrow starts from}}
    consume(r);
  }
}

class S {
    int value;
public:
    int &get();
};

void f2() {
    S s;
    for (int i = 0; i < 10; i++) {
        int &r = s.get(); // expected-warning {{non constant borrow r overlapped with other borrow}}
        consume(r);       // expected-note@-1 {{the previous borrow starts from}}
    }
}

// Same with S except `get()` is marked const. So that the const borrowing is allowed to be overlapped.
class S2 {
    int value;
public:
    int &get() const;
};

void f3() {
    S2 s;
    for (int i = 0; i < 10; i++) {
        int &r = s.get();
        consume(r);
    }
}