// Test simple template function
template<typename T>
T identity(T x) {
    return x;
}

int main() {
    int val = identity(42);
    return val;
}
