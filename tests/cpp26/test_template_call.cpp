// Test template function call after P0 fix
template<typename T>
T add(T a, T b) {
    return a + b;
}

int main() {
    int result = add(1, 2);
    return result;
}
