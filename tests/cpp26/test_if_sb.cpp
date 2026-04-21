// Test structured binding in if condition (P0963R3)
void test_if_binding() {
    int arr[2] = {10, 20};
    
    // Structured binding in if condition
    if (auto [x, y] = arr) {
        int sum = x + y;
    }
}

int main() {
    test_if_binding();
    return 0;
}
