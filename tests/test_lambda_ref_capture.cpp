int main() {
    int value = 42;
    
    // Test reference capture
    auto lambda = [&value]() {
        return value;
    };
    
    return 0;
}
