// Test template specialization variable declaration
template<class T1, class T2>
struct MyPair {
    T1 first;
    T2 second;
};

void test_template_var_decl() {
    // This should parse: MyPair<int, double> p{42, 3.14};
    MyPair<int, double> p{42, 3.14};
}

int main() {
    test_template_var_decl();
    return 0;
}
