#include "blocktype/IR/ProjectionQuery.h"
#include "blocktype/IR/QueryContext.h"
#include "blocktype/IR/RedGreenMarker.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/DependencyGraph.h"

namespace blocktype {
namespace ir {

ProjectionQuery::ProjectionQuery(QueryContext& Q) : QC(Q) {}

std::unique_ptr<IRModule> ProjectionQuery::projectFunction(const IRModule& M,
                                                            StringRef FunctionName) {
  // Create a new module with the same context for the projected function
  auto ProjModule = std::make_unique<IRModule>(
      (M.getName().str() + ".projected").c_str(),
      const_cast<IRModule&>(M).getTypeContext(),
      M.getTargetTriple());

  // Find the target function in the source module
  IRFunction* SrcFunc = M.getFunction(FunctionName);
  if (!SrcFunc)
    return nullptr;

  // Create a declaration-only copy of the function in the projected module
  IRFunctionType* FTy = SrcFunc->getFunctionType();
  IRFunction* ProjFunc = ProjModule->getOrInsertFunction(FunctionName, FTy);
  (void)ProjFunc;

  return ProjModule;
}

SmallVector<IRFunction*, 16> ProjectionQuery::getModifiedFunctions(const IRModule& M) {
  SmallVector<IRFunction*, 16> Modified;

  for (const auto& Fn : M.getFunctions()) {
    // Check if the function's fingerprint has changed
    Fingerprint FP = computeFingerprint(*Fn);
    if (QC.getDependencyGraph().hasFingerprintChanged(
            reinterpret_cast<QueryID>(Fn.get()), FP)) {
      Modified.push_back(Fn.get());
    }
  }

  return Modified;
}

bool ProjectionQuery::canReuseCompilation(const IRFunction& F) {
  // Create a RedGreenMarker instance and use tryMarkGreen to determine
  // if the function's compilation result can be reused
  RedGreenMarker Marker(QC);
  QueryID FID = reinterpret_cast<QueryID>(&F);
  MarkColor Color = Marker.tryMarkGreen(FID);
  return Color == MarkColor::Green;
}

} // namespace ir
} // namespace blocktype
