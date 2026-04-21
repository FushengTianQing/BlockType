// Test: Auto return type deduction
// Tests that non-template functions with auto return type are properly deduced

auto simpleReturn() {
  return 42;  // Should deduce as int
}

auto returnDouble() {
  return 3.14;  // Should deduce as double
}

auto returnString() {
  return "hello";  // Should deduce as const char*
}

auto noReturn() {
  // No return statement - should deduce as void
}

auto multipleReturns() {
  if (true) {
    return 1;
  }
  return 2;  // Both return int, should deduce as int
}

// Test with reference stripping
auto returnRef() {
  int x = 10;
  return x;  // Should deduce as int (not int&)
}

// Test with const
auto returnConst() {
  const int x = 42;
  return x;  // Should deduce as int (const is stripped for auto)
}

int main() {
  auto a = simpleReturn();
  auto b = returnDouble();
  auto c = returnString();
  // noReturn();  // void function
  auto d = multipleReturns();
  auto e = returnRef();
  auto f = returnConst();
  
  return 0;
}
