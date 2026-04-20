// Test aggregate initialization for class template specialization
template<class T1, class T2>
struct MyPair {
    T1 first;
    T2 second;
};

void test_aggregate_init() {
    // Test aggregate initialization with braced-init-list
    MyPair<int, double> p{42, 3.14};
}

int main() {
    test_aggregate_init();
    return 0;
}
