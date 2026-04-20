// Test P1061R10: Structured binding with pack expansion
#include <tuple>

void test_pack_expansion() {
    auto t = std::make_tuple(1, 2.0, 'a', "hello");
    
    // P1061R10: Pack expansion in structured binding
    auto [first, second, ...rest] = t;
    
    // rest should expand to: rest0 (char), rest1 (const char*)
}

int main() {
    test_pack_expansion();
    return 0;
}
