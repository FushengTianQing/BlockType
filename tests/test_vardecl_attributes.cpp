// Test VarDecl attribute support
// This test verifies that variable declarations can have attributes

#include <iostream>

// Test 1: Variable with deprecated attribute
[[deprecated("Use newVar instead")]]
int oldVar = 100;

// Test 2: Variable with used attribute
__attribute__((used))
int usedVar = 200;

// Test 3: Variable with visibility attribute
__attribute__((visibility("hidden")))
int hiddenVar = 300;

// Test 4: Static variable with attributes
class MyClass {
public:
    [[deprecated]]
    static int deprecatedStatic;
    
    __attribute__((used))
    static int usedStatic;
};

int MyClass::deprecatedStatic = 400;
int MyClass::usedStatic = 500;

int main() {
    std::cout << "VarDecl Attribute Test" << std::endl;
    
    // Use variables to avoid warnings
    std::cout << "oldVar = " << oldVar << std::endl;
    std::cout << "usedVar = " << usedVar << std::endl;
    std::cout << "hiddenVar = " << hiddenVar << std::endl;
    
    std::cout << "MyClass::deprecatedStatic = " << MyClass::deprecatedStatic << std::endl;
    std::cout << "MyClass::usedStatic = " << MyClass::usedStatic << std::endl;
    
    std::cout << "Test completed successfully!" << std::endl;
    return 0;
}
