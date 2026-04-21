// Test structured binding in loop
void test_loop_binding() {
    int matrix[3][2] = {{1, 2}, {3, 4}, {5, 6}};
    
    // Structured binding in for loop
    for (int i = 0; i < 3; ++i) {
        auto [first, second] = matrix[i];
        int sum = first + second;
    }
}

int main() {
    test_loop_binding();
    return 0;
}
