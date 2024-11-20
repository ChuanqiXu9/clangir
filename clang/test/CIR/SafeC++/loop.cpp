// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only
// mimic https://users.rust-lang.org/t/calling-a-mut-objects-method-in-a-loop-mutable-borrow-starts-here-in-previous-iteration-of-loop/35393
namespace std {
class foo {
    int x;
public:
    int &get();
};
template <class T>
class vector {
    T* data;
    unsigned size;
public:
    void push_back(const T & elem [[clang::NoBorrowToRet]]);
};
}
#pragma clang SafeCXX
void consume(const std::vector<int> &);
void func() {
  std::foo f;
  std::vector<int> vec;
  for (int i = 0; i < 10; ++i) {
    auto &l = f.get(); // expected-warning {{non constant borrow l overlapped with other borrow}}
                       // expected-note@-1 {{the previous borrow starts from}}
    vec.push_back(l);
  }
  consume(vec);
}
