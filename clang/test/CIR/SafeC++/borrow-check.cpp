// RUN: %clang_cc1 -x c++ %s -verify -fsyntax-only -Wno-return-stack-address
#pragma clang SafeCXX
void consume_int(const int &);
int non_read() {
    int i;
    const int &r1 = i; // expected-note {{the borrow starts from}}
    i = 4; // expected-warning {{a change to i is detected when borrowed by r1}}
    consume_int(r1);
    return i;
}

int &consume_non_const_int(int &);
int non_multiple_write_ref() {
    int x;
    int &r = consume_non_const_int(x);  // expected-warning {{non constant borrow x overlapped with other borrow}}
    consume_non_const_int(x); // expected-note {{the previous borrow starts from}}
    consume_non_const_int(r);
    return r;
}

// It should be fine to have multiple constant borrow.
const int &consume_const_int(const int &);
int multiple_read_ref() {
    int x;
    const int &r = consume_const_int(x);
    const int &w = consume_const_int(x);
    consume_const_int(x);
    consume_const_int(r);
    return r;
}
