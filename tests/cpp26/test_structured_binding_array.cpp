// Test structured binding with array (simpler than templates)
void test_array_binding() {
  int arr[2] = {42, 99};
  auto [x, y] = arr;
}

int main() {
  test_array_binding();
  return 0;
}
