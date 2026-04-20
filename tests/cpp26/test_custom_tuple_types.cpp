// Test structured bindings with custom pair-like and tuple-like types
// This verifies that the type detection and element extraction work correctly

// Simple pair-like structure (mimics std::pair)
template<class T1, class T2>
struct MyPair {
    T1 first;
    T2 second;
};

// Simple tuple-like structure (mimics std::tuple)
template<class... Types>
struct MyTuple;

template<class T1, class T2, class T3>
struct MyTuple<T1, T2, T3> {
    T1 elem0;
    T2 elem1;
    T3 elem2;
};

void test_pair_like() {
    MyPair<int, double> p{42, 3.14};
    auto [x, y] = p;
}

void test_tuple_like() {
    MyTuple<int, double, char> t{1, 2.0, 'c'};
    auto [a, b, c] = t;
}

int main() {
    test_pair_like();
    test_tuple_like();
    return 0;
}
