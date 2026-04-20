int main() {
    int a = 1;
    int b = 2;
    
    // Test multiple captures
    auto lambda = [a, b]() {
        return a + b;
    };
    
    return 0;
}
