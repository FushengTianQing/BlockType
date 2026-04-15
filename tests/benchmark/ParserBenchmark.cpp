//===--- ParserBenchmark.cpp - Parser Performance Benchmark --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include "blocktype/Parse/Parser.h"
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/AST/ASTContext.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// Test Case Generators
//===----------------------------------------------------------------------===//

/// Generate a deeply nested expression.
std::string generateNestedExpression(unsigned Depth) {
  std::ostringstream OS;
  for (unsigned i = 0; i < Depth; ++i) {
    OS << "(";
  }
  OS << "1";
  for (unsigned i = 0; i < Depth; ++i) {
    OS << " + 2)";
  }
  return OS.str();
}

/// Generate a large compound statement.
std::string generateLargeCompoundStatement(unsigned NumStatements) {
  std::ostringstream OS;
  OS << "{\n";
  for (unsigned i = 0; i < NumStatements; ++i) {
    OS << "  int x" << i << " = " << i << ";\n";
  }
  OS << "}";
  return OS.str();
}

/// Generate a complex expression with many operators.
std::string generateComplexExpression(unsigned NumTerms) {
  std::ostringstream OS;
  for (unsigned i = 0; i < NumTerms; ++i) {
    if (i > 0) {
      OS << " + ";
    }
    OS << "(a" << i << " * b" << i << " + c" << i << " / d" << i << ")";
  }
  return OS.str();
}

/// Generate nested if-else chains.
std::string generateNestedIfElse(unsigned Depth) {
  std::ostringstream OS;
  for (unsigned i = 0; i < Depth; ++i) {
    OS << "if (cond" << i << ") {\n";
  }
  OS << "  ;\n";
  for (unsigned i = 0; i < Depth; ++i) {
    OS << "} else {\n  ;\n}\n";
  }
  return OS.str();
}

/// Generate a function with many parameters.
std::string generateFunctionWithManyParams(unsigned NumParams) {
  std::ostringstream OS;
  OS << "void func(";
  for (unsigned i = 0; i < NumParams; ++i) {
    if (i > 0) OS << ", ";
    OS << "int p" << i;
  }
  OS << ") {\n}\n";
  return OS.str();
}

//===----------------------------------------------------------------------===//
// Benchmark Runner
//===----------------------------------------------------------------------===//

struct BenchmarkResult {
  std::string Name;
  size_t InputSize;
  double TimeMs;
  double ThroughputKBps;  // KB/s
  
  void print() const {
    std::cout << std::setw(40) << std::left << Name
              << std::setw(12) << std::right << InputSize << " bytes"
              << std::setw(12) << std::fixed << std::setprecision(2) << TimeMs << " ms"
              << std::setw(16) << std::fixed << std::setprecision(0) << ThroughputKBps << " KB/s"
              << "\n";
  }
};

BenchmarkResult runBenchmark(const std::string &Name, const std::string &Code, unsigned Iterations = 10) {
  SourceManager SM;
  DiagnosticsEngine Diags;
  ASTContext Context;
  
  // Warm-up
  {
    SM.createMainFileID("test.cpp", Code);
    auto PP = std::make_unique<Preprocessor>(SM, Diags);
    PP->enterSourceFile("test.cpp", Code);
    Parser P(*PP, Context);
    P.parseTranslationUnit();
  }
  
  // Benchmark
  auto Start = std::chrono::high_resolution_clock::now();
  
  for (unsigned i = 0; i < Iterations; ++i) {
    SourceManager LSM;
    DiagnosticsEngine LDiags;
    ASTContext LContext;
    
    LSM.createMainFileID("test.cpp", Code);
    auto PP = std::make_unique<Preprocessor>(LSM, LDiags);
    PP->enterSourceFile("test.cpp", Code);
    Parser P(*PP, LContext);
    P.parseTranslationUnit();
  }
  
  auto End = std::chrono::high_resolution_clock::now();
  auto Duration = std::chrono::duration_cast<std::chrono::microseconds>(End - Start);
  
  double TimeMs = Duration.count() / 1000.0 / Iterations;
  double ThroughputKBps = (Code.size() / 1024.0) / (TimeMs / 1000.0);
  
  return {Name, Code.size(), TimeMs, ThroughputKBps};
}

//===----------------------------------------------------------------------===//
// Main
//===----------------------------------------------------------------------===//

int main() {
  std::cout << "\n";
  std::cout << "=== Parser Performance Benchmark ===\n";
  std::cout << "\n";
  
  std::cout << std::setw(40) << std::left << "Test Case"
            << std::setw(12) << std::right << "Input Size"
            << std::setw(12) << "Time"
            << std::setw(16) << "Throughput"
            << "\n";
  std::cout << std::string(80, '-') << "\n";
  
  std::vector<BenchmarkResult> Results;
  
  // 1. Deeply nested expressions
  Results.push_back(runBenchmark("Nested Expression (depth=10)", generateNestedExpression(10)));
  Results.push_back(runBenchmark("Nested Expression (depth=50)", generateNestedExpression(50)));
  Results.push_back(runBenchmark("Nested Expression (depth=100)", generateNestedExpression(100)));
  
  // 2. Large compound statements
  Results.push_back(runBenchmark("Compound Statement (100 stmts)", generateLargeCompoundStatement(100)));
  Results.push_back(runBenchmark("Compound Statement (500 stmts)", generateLargeCompoundStatement(500)));
  Results.push_back(runBenchmark("Compound Statement (1000 stmts)", generateLargeCompoundStatement(1000)));
  
  // 3. Complex expressions
  Results.push_back(runBenchmark("Complex Expression (10 terms)", generateComplexExpression(10)));
  Results.push_back(runBenchmark("Complex Expression (50 terms)", generateComplexExpression(50)));
  Results.push_back(runBenchmark("Complex Expression (100 terms)", generateComplexExpression(100)));
  
  // 4. Nested if-else
  Results.push_back(runBenchmark("Nested If-Else (depth=10)", generateNestedIfElse(10)));
  Results.push_back(runBenchmark("Nested If-Else (depth=50)", generateNestedIfElse(50)));
  
  // 5. Functions with many parameters
  Results.push_back(runBenchmark("Function (10 params)", generateFunctionWithManyParams(10)));
  Results.push_back(runBenchmark("Function (50 params)", generateFunctionWithManyParams(50)));
  Results.push_back(runBenchmark("Function (100 params)", generateFunctionWithManyParams(100)));
  
  // Print results
  for (const auto &R : Results) {
    R.print();
  }
  
  std::cout << "\n";
  std::cout << "=== Summary ===\n";
  
  double TotalTime = 0;
  double TotalThroughput = 0;
  for (const auto &R : Results) {
    TotalTime += R.TimeMs;
    TotalThroughput += R.ThroughputKBps;
  }
  
  std::cout << "Average parse time: " << std::fixed << std::setprecision(2)
            << (TotalTime / Results.size()) << " ms\n";
  std::cout << "Average throughput: " << std::fixed << std::setprecision(0)
            << (TotalThroughput / Results.size()) << " KB/s\n";
  std::cout << "\n";
  
  return 0;
}
