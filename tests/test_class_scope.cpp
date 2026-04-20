class MyClass {
public:
    int x;
    int y;
    
    void method() {
        int local = x + y;
    }
};

struct MyStruct {
    int a;
    int b;
    
    void structMethod() {
        int result = a + b;
    }
};

int main() {
    MyClass obj1;
    MyStruct obj2;
    return 0;
}
