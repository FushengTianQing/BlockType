#include "blocktype/IR/ProjectionQuery.h"
#include "blocktype/IR/QueryContext.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/DependencyGraph.h"

#include <fstream>
#include <sstream>
#include <string>

namespace blocktype {
namespace frontend {

using ir::StringRef;
using ir::SmallVector;

/// Write a .d dependency file in Make format.
/// Format: target.o: source.cpp dep1.h dep2.h ...
static bool writeMakeDepFile(StringRef OutputPath,
                              StringRef Target,
                              const SmallVector<std::string, 32>& Deps) {
  std::ofstream Out(OutputPath.str());
  if (!Out.is_open())
    return false;

  Out << Target.str() << ".o:";
  for (const auto& D : Deps) {
    Out << " " << D;
  }
  Out << "\n";
  Out.close();
  return Out.good();
}

/// Write a .d dependency file in Ninja format.
/// Format: build target.o: compile source.cpp | dep1.h dep2.h
static bool writeNinjaDepFile(StringRef OutputPath,
                               StringRef Target,
                               StringRef Source,
                               const SmallVector<std::string, 32>& Deps) {
  std::ofstream Out(OutputPath.str());
  if (!Out.is_open())
    return false;

  Out << "build " << Target.str() << ".o: compile " << Source.str();
  if (!Deps.empty()) {
    Out << " |";
    for (const auto& D : Deps) {
      Out << " " << D;
    }
  }
  Out << "\n";
  Out.close();
  return Out.good();
}

/// Collect all source dependencies for a given IRModule by traversing
/// its dependency graph.
static SmallVector<std::string, 32> collectModuleDependencies(
    const ir::IRModule& M,
    ir::QueryContext& QC) {
  SmallVector<std::string, 32> Deps;

  // Add the module name itself as a dependency source
  Deps.push_back(M.getName().str() + ".cpp");

  // Collect dependencies from each function in the module
  ir::ProjectionQuery PQ(QC);
  auto Modified = PQ.getModifiedFunctions(M);

  // For each function, record its source file as a dependency
  for (auto* Fn : Modified) {
    std::string DepName = Fn->getName().str();
    Deps.push_back(DepName + ".h");
  }

  return Deps;
}

/// Incremental build integration: generate .d dependency file for the
/// given module, supporting both Make and Ninja formats.
///
/// @param M       The IR module to generate dependencies for
/// @param QC      Query context for dependency tracking
/// @param OutputPath  Output path for the .d file
/// @param Target  Target object file name (without extension)
/// @param Source  Source file path
/// @param UseNinja  If true, output Ninja format; otherwise Make format
bool generateDependencyFile(ir::IRModule& M,
                             ir::QueryContext& QC,
                             ir::StringRef OutputPath,
                             ir::StringRef Target,
                             ir::StringRef Source,
                             bool UseNinja) {
  auto Deps = collectModuleDependencies(M, QC);

  if (UseNinja) {
    return writeNinjaDepFile(OutputPath, Target, Source, Deps);
  } else {
    return writeMakeDepFile(OutputPath, Target, Deps);
  }
}

/// Perform incremental compilation: only recompile modified functions.
///
/// @param M       The IR module
/// @param QC      Query context
/// @return  Number of functions that need recompilation
size_t incrementalCompile(ir::IRModule& M, ir::QueryContext& QC) {
  ir::ProjectionQuery PQ(QC);
  auto Modified = PQ.getModifiedFunctions(M);
  return Modified.size();
}

} // namespace frontend
} // namespace blocktype
