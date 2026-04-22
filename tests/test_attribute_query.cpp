// Test unified attribute query interface
// Compile: clang++ -std=c++20 test_attribute_query.cpp -o test_attr

#include <iostream>

// Test 1: Function attributes
[[deprecated("Use newFunc instead")]]
void oldFunc() {
    std::cout << "Old function" << std::endl;
}

[[nodiscard]]
int getValue() {
    return 42;
}

__attribute__((weak))
void weakFunc() {
    std::cout << "Weak function" << std::endl;
}

__attribute__((visibility("hidden")))
void hiddenFunc() {
    std::cout << "Hidden function" << std::endl;
}

// Test 2: Variable attributes
__attribute__((used))
int usedVar = 100;

[[deprecated]]
int oldVar = 200;

// Test 3: Class attributes
class [[deprecated("Use NewClass instead")]] OldClass {
public:
    int value;
};

class __attribute__((visibility("default"))) VisibleClass {
public:
    int data;
};

int main() {
    std::cout << "Attribute Query Test" << std::endl;
    
    // Call functions to ensure they're referenced
    oldFunc();
    int val = getValue();
    (void)val;
    weakFunc();
    hiddenFunc();
    
    // Use variables
    std::cout << "usedVar = " << usedVar << std::endl;
    std::cout << "oldVar = " << oldVar << std::endl;
    
    // Use classes
    OldClass oc;
    oc.value = 10;
    
    VisibleClass vc;
    vc.data = 20;
    
    std::cout << "Test completed successfully!" << std::endl;
    return 0;
}
