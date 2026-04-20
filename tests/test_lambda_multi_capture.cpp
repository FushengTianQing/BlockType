int main() {
    int intValue = 42;
    double doubleValue = 3.14;
    
    // Test multiple captures with different types
    auto lambda = [intValue, doubleValue]() {
        return intValue + (int)doubleValue;
    };
    
    return 0;
}
