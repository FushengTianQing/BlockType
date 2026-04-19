// Minimal test for structured binding
void test_binding() {
  int arr[2];
  arr[0] = 1;
  arr[1] = 2;
  auto [x, y] = arr;
}

int main() {
  test_binding();
  return 0;
}
