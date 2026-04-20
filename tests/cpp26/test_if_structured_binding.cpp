// Test P0963R3: Structured bindings in if condition
// Syntax: if (auto [x, y] = expr) { ... }

void test_if_structured_binding_basic() {
    int arr[2] = {42, 100};
    
    // C++23: structured binding in if condition
    if (auto [a, b] = arr) {
        // Bindings a and b are in scope here
        // Condition is true if first element (a) is non-zero
        if (a == 42 && b == 100) {
            return; // Success
        }
    }
}

void test_if_structured_binding_with_else() {
    int arr[2] = {0, 100};
    
    if (auto [x, y] = arr) {
        // This branch should NOT execute because x == 0 (false)
        return; // Error - shouldn't reach here
    } else {
        // This branch should execute
        if (y == 100) {
            return; // Success
        }
    }
}

void test_if_structured_binding_nested() {
    int outer[2][2] = {{1, 2}, {3, 4}};
    
    // Nested if with structured bindings
    if (auto [row0_0, row0_1] = outer[0]) {
        if (auto [row1_0, row1_1] = outer[1]) {
            if (row0_0 == 1 && row0_1 == 2 && 
                row1_0 == 3 && row1_1 == 4) {
                return; // Success
            }
        }
    }
}

void test_if_structured_binding_multiple() {
    int data1[2] = {5, 10};
    int data2[3] = {15, 25, 35};
    
    // Multiple if statements with structured bindings
    if (auto [a, b] = data1) {
        if (a == 5 && b == 10) {
            if (auto [x, y, z] = data2) {
                if (x == 15 && y == 25 && z == 35) {
                    return; // Success
                }
            }
        }
    }
}

void test_if_structured_binding_scope() {
    int arr[2] = {100, 200};
    
    if (auto [first, second] = arr) {
        // first and second are in scope here
        if (first != 100 || second != 200) {
            return; // Error
        }
    }
    // first and second are NOT in scope here (correct behavior)
    
    // Can use same names again in another if
    int arr2[2] = {300, 400};
    if (auto [first, second] = arr2) {
        if (first == 300 && second == 400) {
            return; // Success
        }
    }
}
