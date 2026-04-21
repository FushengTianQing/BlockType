// Test 1: Nested structured binding
void test_nested() {
    int outer[2][2] = {{1, 2}, {3, 4}};
    auto [row0_0, row0_1] = outer[0];
}

int main() {
    test_nested();
    return 0;
}
