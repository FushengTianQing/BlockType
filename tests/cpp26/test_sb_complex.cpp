// Test structured binding in complex contexts
// 1. Nested structured binding
// 2. Structured binding in if condition
// 3. Structured binding in loop

void test_nested_binding() {
    int outer[2][2] = {{1, 2}, {3, 4}};
    
    // Nested: bind first row
    auto [row0_0, row0_1] = outer[0];
    
    // Nested: bind second row
    auto [row1_0, row1_1] = outer[1];
}

void test_if_binding() {
    int arr[2] = {10, 20};
    
    // Structured binding in if condition (P0963R3)
    if (auto [x, y] = arr) {
        int sum = x + y;
    }
}

void test_loop_binding() {
    int matrix[3][2] = {{1, 2}, {3, 4}, {5, 6}};
    
    // Structured binding in for loop
    for (int i = 0; i < 3; ++i) {
        auto [first, second] = matrix[i];
        int sum = first + second;
    }
}

int main() {
    test_nested_binding();
    test_if_binding();
    test_loop_binding();
    return 0;
}
