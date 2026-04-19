void test_for_loop_binding() {
    int matrix[3][2] = {{1, 2}, {3, 4}, {5, 6}};
    
    int total = 0;
    for (int i = 0; i < 3; ++i) {
        auto [first, second] = matrix[i];
        total += first + second;
    }
}

int main() {
    test_for_loop_binding();
    return 0;
}
