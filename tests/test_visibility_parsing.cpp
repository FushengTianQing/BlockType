// Test visibility attribute parameter parsing
// This test verifies that visibility attributes with parameters are correctly parsed

#include <iostream>

// Test 1: Function with visibility("hidden")
__attribute__((visibility("hidden")))
void hiddenFunction() {
    std::cout << "Hidden function" << std::endl;
}

// Test 2: Function with visibility("default")
__attribute__((visibility("default")))
void defaultFunction() {
    std::cout << "Default function" << std::endl;
}

// Test 3: Variable with visibility("hidden")
__attribute__((visibility("hidden")))
int hiddenVar = 100;

// Test 4: Variable with visibility("protected")
__attribute__((visibility("protected")))
int protectedVar = 200;

// Test 5: Class with visibility attribute
class __attribute__((visibility("default"))) VisibleClass {
public:
    int data;
    VisibleClass(int d) : data(d) {}
};

// Test 6: Class with hidden visibility
class __attribute__((visibility("hidden"))) HiddenClass {
public:
    int value;
    HiddenClass(int v) : value(v) {}
};

int main() {
    std::cout << "Visibility Attribute Parameter Parsing Test" << std::endl;
    
    // Call functions
    hiddenFunction();
    defaultFunction();
    
    // Use variables
    std::cout << "hiddenVar = " << hiddenVar << std::endl;
    std::cout << "protectedVar = " << protectedVar << std::endl;
    
    // Use classes
    VisibleClass vc(42);
    std::cout << "VisibleClass data = " << vc.data << std::endl;
    
    HiddenClass hc(99);
    std::cout << "HiddenClass value = " << hc.value << std::endl;
    
    std::cout << "Test completed successfully!" << std::endl;
    return 0;
}
