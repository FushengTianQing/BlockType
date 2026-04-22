// Simple attribute test for BlockType compiler
// This test uses basic C++ features that BlockType should support

[[deprecated]]
int oldVar = 100;

__attribute__((used))
int usedVar = 200;

__attribute__((visibility("hidden")))
int hiddenVar = 300;

[[deprecated("Use newFunc instead")]]
void oldFunction() {
}

__attribute__((weak))
void weakFunction() {
}

class TestClass {
public:
    [[deprecated]]
    int oldField;
    
    int normalField;
};

int main() {
    return 0;
}
