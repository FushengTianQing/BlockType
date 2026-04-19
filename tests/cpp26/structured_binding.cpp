// Test structured binding with std::pair and std::tuple
// Expected: auto [x, y] = pair should extract types correctly

#include <utility>
#include <tuple>

void test_pair_binding() {
    std::pair<int, double> p{42, 3.14};
    auto [x, y] = p;  // x should be int, y should be double
    
    // Verify types (compile-time check)
    static_assert(sizeof(x) == sizeof(int), "x should be int");
    static_assert(sizeof(y) == sizeof(double), "y should be double");
}

void test_tuple_binding() {
    std::tuple<int, double, char> t{1, 2.5, 'a'};
    auto [a, b, c] = t;  // a=int, b=double, c=char
    
    // Verify types
    static_assert(sizeof(a) == sizeof(int), "a should be int");
    static_assert(sizeof(b) == sizeof(double), "b should be double");
    static_assert(sizeof(c) == sizeof(char), "c should be char");
}

int main() {
    test_pair_binding();
    test_tuple_binding();
    return 0;
}
