int main() {
    int x = 10;
    double y = 3.14;
    
    // Test capture type inference
    auto lambda = [x, &y]() {
        return x + (int)y;
    };
    
    return 0;
}
