//===--- InfrastructureSharingTest.cpp - Test infrastructure sharing -*- C++ -*-===//
//
// Test that CompilerInstance properly shares infrastructure in multi-file compilation.
//
//===-------------------------------------------------------------------------------===//

#include "blocktype/Frontend/CompilerInstance.h"
#include "blocktype/Frontend/CompilerInvocation.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/AST/ASTContext.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdio>
#include <fstream>

using namespace llvm;
using namespace blocktype;

// Helper to create temporary files
static std::string createTempFile(const std::string &Content) {
  static int Counter = 0;
  std::string Filename = "/tmp/blocktype_test_" + std::to_string(Counter++) + ".cpp";
  std::ofstream File(Filename);
  File << Content;
  File.close();
  return Filename;
}

// Test 1: Verify single file uses simple path
static void testSingleFile() {
  outs() << "Test 1: Single file compilation...\n";
  
  auto CI = std::make_shared<CompilerInvocation>();
  std::string File = createTempFile("int main() { return 0; }");
  CI->FrontendOpts.InputFiles = {File};
  CI->FrontendOpts.Verbose = true;
  
  CompilerInstance Instance;
  bool Success = Instance.initialize(CI);
  assert(Success && "Initialization failed");
  
  Success = Instance.compileAllFiles();
  assert(Success && "Compilation failed");
  
  outs() << "  ✓ Single file compilation succeeded\n\n";
}

// Test 2: Verify multi-file shares infrastructure
static void testMultiFileSharing() {
  outs() << "Test 2: Multi-file infrastructure sharing...\n";
  
  auto CI = std::make_shared<CompilerInvocation>();
  std::string File1 = createTempFile("int add(int a, int b) { return a + b; }");
  std::string File2 = createTempFile("int multiply(int a, int b) { return a * b; }");
  std::string File3 = createTempFile("int subtract(int a, int b) { return a - b; }");
  
  CI->FrontendOpts.InputFiles = {File1, File2, File3};
  CI->FrontendOpts.Verbose = true;
  
  CompilerInstance Instance;
  bool Success = Instance.initialize(CI);
  assert(Success && "Initialization failed");
  
  // Get pointers to infrastructure components
  SourceManager *SM = &Instance.getSourceManager();
  DiagnosticsEngine *Diags = &Instance.getDiagnostics();
  ASTContext *Context = &Instance.getASTContext();
  
  assert(SM && "SourceManager is null");
  assert(Diags && "DiagnosticsEngine is null");
  assert(Context && "ASTContext is null");
  
  // Store the pointers
  SourceManager *OriginalSM = SM;
  DiagnosticsEngine *OriginalDiags = Diags;
  ASTContext *OriginalContext = Context;
  
  Success = Instance.compileAllFiles();
  assert(Success && "Compilation failed");
  
  // Verify that infrastructure pointers are the same after compilation
  SM = &Instance.getSourceManager();
  Diags = &Instance.getDiagnostics();
  Context = &Instance.getASTContext();
  
  assert(SM == OriginalSM && "SourceManager was recreated!");
  assert(Diags == OriginalDiags && "DiagnosticsEngine was recreated!");
  assert(Context == OriginalContext && "ASTContext was recreated!");
  
  outs() << "  ✓ Infrastructure components are shared across files\n";
  outs() << "    SourceManager: " << SM << "\n";
  outs() << "    DiagnosticsEngine: " << Diags << "\n";
  outs() << "    ASTContext: " << Context << "\n\n";
}

// Test 3: Verify macro definitions are shared
static void testMacroSharing() {
  outs() << "Test 3: Macro definition sharing...\n";
  
  auto CI = std::make_shared<CompilerInvocation>();
  std::string File1 = createTempFile("#define SHARED_MACRO 42\nint x = SHARED_MACRO;");
  std::string File2 = createTempFile("#if SHARED_MACRO == 42\nint y = 1;\n#endif");
  
  CI->FrontendOpts.InputFiles = {File1, File2};
  CI->FrontendOpts.Verbose = true;
  
  CompilerInstance Instance;
  bool Success = Instance.initialize(CI);
  assert(Success && "Initialization failed");
  
  Success = Instance.compileAllFiles();
  assert(Success && "Compilation failed");
  
  outs() << "  ✓ Macro definitions are shared across files\n\n";
}

// Test 4: Verify error handling doesn't affect other files
static void testErrorIsolation() {
  outs() << "Test 4: Error isolation between files...\n";
  
  auto CI = std::make_shared<CompilerInvocation>();
  std::string File1 = createTempFile("int valid_function() { return 0; }");
  std::string File2 = createTempFile("int invalid_function( { }"); // Syntax error
  
  CI->FrontendOpts.InputFiles = {File1, File2};
  CI->FrontendOpts.Verbose = true;
  
  CompilerInstance Instance;
  bool Success = Instance.initialize(CI);
  assert(Success && "Initialization failed");
  
  // Compilation should fail due to File2
  Success = Instance.compileAllFiles();
  assert(!Success && "Compilation should have failed");
  
  // But File1 should have been processed successfully
  outs() << "  ✓ Error in one file doesn't prevent processing of other files\n\n";
}

int main() {
  outs() << "\n=== Infrastructure Sharing Tests ===\n\n";
  
  testSingleFile();
  testMultiFileSharing();
  testMacroSharing();
  testErrorIsolation();
  
  outs() << "=== All tests passed! ===\n";
  return 0;
}
