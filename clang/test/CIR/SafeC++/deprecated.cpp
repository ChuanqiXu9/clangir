// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only

namespace std {
using nullptr_t = decltype(nullptr);
}

template <class T>
class MyUniquePtr {
    T *ptr;

public:
    [[clang::SafeCXXDeprecated]] MyUniquePtr() : ptr(nullptr) {}
    [[clang::SafeCXXDeprecated]] MyUniquePtr(std::nullptr_t) : ptr(nullptr) {}

    MyUniquePtr(MyUniquePtr &&other) : ptr(other.ptr) {
        other.ptr = nullptr;
    }
    MyUniquePtr &operator=(MyUniquePtr &&other) {
        if (ptr)
            delete ptr;

        ptr = other.ptr;
        other.ptr = nullptr;
    }
    T &operator*() const { return *ptr; }
    T *get() const { return ptr; }
    T *release() { return ptr; }
    [[clang::SafeCXXDeprecated]] void reset() { ptr = nullptr; }
    void reset(T* p) { ptr = p; }
    ~MyUniquePtr() { delete ptr; }
};

#pragma clang SafeCXX

// FIXME: CIR didn't attach the ast decl for constructor. See 
// the comments in clang/lib/CIR/Dialect/Analysis/DeprecatedCallCheck.cpp
int foo() {
    MyUniquePtr<int> p(nullptr); // xexpected-warning {{is deprecated in safe C++}}
    return *p;
}

int foo2() {
    MyUniquePtr<int> p; // xexpected-warning {{is deprecated in safe C++}}
    return *p;
}

int foo3(MyUniquePtr<int> p) {
    p.reset(); // expected-warning {{call to MyUniquePtr<int>::reset() is deprecated in safe C++}}
    return *p;
}
