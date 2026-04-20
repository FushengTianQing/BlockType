class Outer {
    int outerVar;
    
    class Inner {
        int innerVar;
        
        void innerMethod() {
            int x = innerVar;
        }
    };
    
    void outerMethod() {
        Inner obj;
        int y = outerVar;
    }
};

int main() {
    Outer obj;
    return 0;
}
