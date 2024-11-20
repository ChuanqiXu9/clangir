// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only

namespace std {
using nullptr_t = decltype(nullptr);
template <class T>
class unique_ptr {
    T *ptr;

public:
    unique_ptr() : ptr(nullptr) {}
    unique_ptr(std::nullptr_t) : ptr(nullptr) {}

    unique_ptr(unique_ptr &&other) : ptr(other.ptr) {
        other.ptr = nullptr;
    }
    unique_ptr &operator=(unique_ptr &&other) {
        if (ptr)
            delete ptr;

        ptr = other.ptr;
        other.ptr = nullptr;
    }
    T &operator*() const { return *ptr; }
    T *get() const { return ptr; }
    T *release() { return ptr; }
    void reset(T *p = nullptr) { ptr = p; }
    ~unique_ptr() { delete ptr; }
};
}

#pragma clang SafeCXX
int foo() {
    std::unique_ptr<int> p(nullptr); // expected-warning {{call to std::unique_ptr<int>::unique_ptr(std::nullptr_t) is deprecated in safe C++}}
    return *p;
}

int foo2() {
    std::unique_ptr<int> p; // expected-warning {{call to std::unique_ptr<int>::unique_ptr() is deprecated in safe C++}}
    return *p;
}

int foo3(std::unique_ptr<int> p) {
    p.reset(); // expected-warning {{call to std::unique_ptr<int>::reset(int*) is deprecated in safe C++}}
    return *p;
}

