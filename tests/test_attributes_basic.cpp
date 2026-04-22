// Test basic attribute functionality for all Decl types
// This test verifies that attributes work correctly on functions, variables, and fields

#include <iostream>

//===----------------------------------------------------------------------===//
// Test 1: Function Attributes
//===----------------------------------------------------------------------===//

[[deprecated("Use newFunction instead")]]
void oldFunction() {
    std::cout << "Old function called" << std::endl;
}

__attribute__((noreturn))
void neverReturns() {
    throw std::runtime_error("This function never returns");
}

__attribute__((weak))
void weakFunction() {
    std::cout << "Weak function" << std::endl;
}

__attribute__((visibility("hidden")))
void hiddenFunction() {
    std::cout << "Hidden function" << std::endl;
}

__attribute__((visibility("default")))
void defaultFunction() {
    std::cout << "Default function" << std::endl;
}

//===----------------------------------------------------------------------===//
// Test 2: Global Variable Attributes
//===----------------------------------------------------------------------===//

[[deprecated("Use newGlobalVar instead")]]
int oldGlobalVar = 100;

__attribute__((used))
int usedGlobalVar = 200;

__attribute__((visibility("hidden")))
int hiddenGlobalVar = 300;

__attribute__((visibility("protected")))
int protectedGlobalVar = 400;

//===----------------------------------------------------------------------===//
// Test 3: Class with Field Attributes
//===----------------------------------------------------------------------===//

class TestClass {
public:
    [[deprecated("Use newField instead")]]
    int oldField;
    
    __attribute__((visibility("hidden")))
    int hiddenField;
    
    int normalField;
    
    TestClass() : oldField(0), hiddenField(0), normalField(0) {}
    TestClass(int o, int h, int n) : oldField(o), hiddenField(h), normalField(n) {}
};

//===----------------------------------------------------------------------===//
// Test 4: Static Member Attributes
//===----------------------------------------------------------------------===//

class StaticMemberTest {
public:
    [[deprecated]]
    static int deprecatedStatic;
    
    __attribute__((used))
    static int usedStatic;
    
    __attribute__((visibility("hidden")))
    static int hiddenStatic;
};

int StaticMemberTest::deprecatedStatic = 500;
int StaticMemberTest::usedStatic = 600;
int StaticMemberTest::hiddenStatic = 700;

//===----------------------------------------------------------------------===//
// Test 5: Class Attributes
//===----------------------------------------------------------------------===//

class [[deprecated("Use NewClass instead")]] OldClass {
public:
    int value;
    OldClass(int v) : value(v) {}
};

class __attribute__((visibility("default"))) VisibleClass {
public:
    int data;
    VisibleClass(int d) : data(d) {}
};

class __attribute__((visibility("hidden"))) HiddenClass {
public:
    int info;
    HiddenClass(int i) : info(i) {}
};

//===----------------------------------------------------------------------===//
// Main Test Function
//===----------------------------------------------------------------------===//

int main() {
    std::cout << "=== Basic Attribute Functionality Test ===" << std::endl;
    std::cout << std::endl;
    
    // Test 1: Function attributes
    std::cout << "--- Function Attributes ---" << std::endl;
    oldFunction();
    hiddenFunction();
    defaultFunction();
    weakFunction();
    std::cout << std::endl;
    
    // Test 2: Global variable attributes
    std::cout << "--- Global Variable Attributes ---" << std::endl;
    std::cout << "oldGlobalVar = " << oldGlobalVar << std::endl;
    std::cout << "usedGlobalVar = " << usedGlobalVar << std::endl;
    std::cout << "hiddenGlobalVar = " << hiddenGlobalVar << std::endl;
    std::cout << "protectedGlobalVar = " << protectedGlobalVar << std::endl;
    std::cout << std::endl;
    
    // Test 3: Field attributes
    std::cout << "--- Field Attributes ---" << std::endl;
    TestClass obj(10, 20, 30);
    std::cout << "obj.oldField = " << obj.oldField << std::endl;
    std::cout << "obj.hiddenField = " << obj.hiddenField << std::endl;
    std::cout << "obj.normalField = " << obj.normalField << std::endl;
    std::cout << std::endl;
    
    // Test 4: Static member attributes
    std::cout << "--- Static Member Attributes ---" << std::endl;
    std::cout << "StaticMemberTest::deprecatedStatic = " 
              << StaticMemberTest::deprecatedStatic << std::endl;
    std::cout << "StaticMemberTest::usedStatic = " 
              << StaticMemberTest::usedStatic << std::endl;
    std::cout << "StaticMemberTest::hiddenStatic = " 
              << StaticMemberTest::hiddenStatic << std::endl;
    std::cout << std::endl;
    
    // Test 5: Class attributes
    std::cout << "--- Class Attributes ---" << std::endl;
    OldClass oldObj(42);
    std::cout << "OldClass value = " << oldObj.value << std::endl;
    
    VisibleClass visObj(99);
    std::cout << "VisibleClass data = " << visObj.data << std::endl;
    
    HiddenClass hidObj(77);
    std::cout << "HiddenClass info = " << hidObj.info << std::endl;
    std::cout << std::endl;
    
    std::cout << "=== All tests completed successfully! ===" << std::endl;
    return 0;
}
