// Test aggregate initialization and template argument deduction
// This tests the complete tuple support with real usage patterns

// Simple pair-like structure
template<class T1, class T2>
struct MyPair {
    T1 first;
    T2 second;
};

// Simple tuple-like structure
template<class... Types>
struct MyTuple;

template<class T1, class T2, class T3>
struct MyTuple<T1, T2, T3> {
    T1 elem0;
    T2 elem1;
    T3 elem2;
};

// Factory function for pair (mimics std::make_pair)
template<class T1, class T2>
MyPair<T1, T2> make_my_pair(T1 a, T2 b) {
    return MyPair<T1, T2>{a, b};
}

void test_aggregate_init() {
    // Test aggregate initialization with explicit types
    MyPair<int, double> p1{42, 3.14};
    auto [x1, y1] = p1;
    
    // Test with different types
    MyPair<char, int> p2{'A', 100};
    auto [x2, y2] = p2;
}

void test_factory_function() {
    // Test factory function with template argument deduction
    auto p1 = make_my_pair(42, 3.14);
    auto [x1, y1] = p1;
    
    // Test with different types
    auto p2 = make_my_pair('A', 100);
    auto [x2, y2] = p2;
}

void test_tuple_aggregate() {
    // Test tuple aggregate initialization
    MyTuple<int, double, char> t1{1, 2.0, 'c'};
    auto [a1, b1, c1] = t1;
}

int main() {
    test_aggregate_init();
    test_factory_function();
    test_tuple_aggregate();
    return 0;
}
