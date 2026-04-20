int main() {
    int value = 10;
    
    // Test simple capture
    auto lambda = [value]() {
        return value;
    };
    
    return 0;
}
