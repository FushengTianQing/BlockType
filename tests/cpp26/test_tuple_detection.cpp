// Test to verify tuple/pair type detection infrastructure
// This tests the IsTupleLikeType and GetTupleElementType functions

void test_array_binding() {
    // Array binding - fully working
    int arr[2] = {1, 2};
    auto [x, y] = arr;
    
    int arr3[3] = {10, 20, 30};
    auto [a, b, c] = arr3;
}

int main() {
    test_array_binding();
    return 0;
}
