void test_array_subscript_binding() {
    int outer[2][2] = {{1, 2}, {3, 4}};
    auto [row0_0, row0_1] = outer[0];
}

int main() {
    test_array_subscript_binding();
    return 0;
}
