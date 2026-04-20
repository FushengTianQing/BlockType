int main() {
    int x = 42;
    
    auto lambda = [x]() -> int {
        return x;
    };
    
    int result = lambda();
    return result;
}
