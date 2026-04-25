//===--- IREmitCXXInherit.cpp - Inheritance Emission -----------*- C++ -*-===//

#include "blocktype/Frontend/IREmitCXX.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/Frontend/IRMangler.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRTypeContext.h"

namespace blocktype {
namespace frontend {

IREmitCXXInherit::IREmitCXXInherit(ASTToIRConverter& C) : Converter_(C) {}

//===----------------------------------------------------------------------===//
// EmitCastToBase
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitCXXInherit::EmitCastToBase(ir::IRValue* Object,
                                               const CXXRecordDecl* Derived,
                                               const CXXRecordDecl* Base) {
  if (!Object || !Derived || !Base) return Object;
  if (Derived == Base) return Object;

  auto& Builder = Converter_.getBuilder();
  auto& TypeCtx = Converter_.getTypeContext();
  auto& Layout = Converter_.getCxxEmitter()->getLayoutEmitter();

  uint64_t Offset = Layout.GetBaseOffset(Derived, Base);

  if (Offset == 0) return Object;

  // Adjust pointer: this + baseOffset
  auto* OffsetVal = Builder.getInt64(Offset);
  auto* Adjusted = Builder.createAdd(
      Builder.createPtrToInt(Object, TypeCtx.getInt64Ty()),
      OffsetVal, "base.off");
  return Builder.createIntToPtr(
      Adjusted,
      TypeCtx.getPointerType(TypeCtx.getInt8Ty()),
      "base.ptr");
}

//===----------------------------------------------------------------------===//
// EmitCastToDerived
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitCXXInherit::EmitCastToDerived(ir::IRValue* Object,
                                                  const CXXRecordDecl* Base,
                                                  const CXXRecordDecl* Derived) {
  if (!Object || !Base || !Derived) return Object;
  if (Base == Derived) return Object;

  auto& Builder = Converter_.getBuilder();
  auto& TypeCtx = Converter_.getTypeContext();
  auto& Layout = Converter_.getCxxEmitter()->getLayoutEmitter();

  uint64_t Offset = Layout.GetBaseOffset(Derived, Base);

  if (Offset == 0) return Object;

  // Adjust pointer: this - baseOffset (downcast)
  auto* OffsetVal = Builder.getInt64(Offset);
  auto* Adjusted = Builder.createSub(
      Builder.createPtrToInt(Object, TypeCtx.getInt64Ty()),
      OffsetVal, "derived.off");
  return Builder.createIntToPtr(
      Adjusted,
      TypeCtx.getPointerType(TypeCtx.getInt8Ty()),
      "derived.ptr");
}

//===----------------------------------------------------------------------===//
// EmitBaseOffset
//===----------------------------------------------------------------------===//

uint64_t IREmitCXXInherit::EmitBaseOffset(const CXXRecordDecl* Derived,
                                          const CXXRecordDecl* Base) {
  if (!Derived || !Base) return 0;
  return Converter_.getCxxEmitter()->getLayoutEmitter().GetBaseOffset(Derived,
                                                                      Base);
}

//===----------------------------------------------------------------------===//
// EmitDynamicCast
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitCXXInherit::EmitDynamicCast(ir::IRValue* Object,
                                                const CXXDynamicCastExpr* DCE) {
  if (!Object || !DCE) return Object;

  auto& Builder = Converter_.getBuilder();
  auto& TypeCtx = Converter_.getTypeContext();

  // dynamic_cast in IR layer:
  // 1. Load vptr from object
  // 2. Load RTTI pointer from vtable (at index 1)
  // 3. Compare RTTI with target type's RTTI
  // 4. If match, adjust pointer by offset-to-top (at index 0)
  // 5. If no match, return null (for pointer cast)

  // Simplified placeholder: just bitcast the pointer
  auto* DestTy = TypeCtx.getPointerType(TypeCtx.getInt8Ty());
  return Builder.createBitCast(Object, DestTy, "dynamic.cast");

  // Full implementation deferred to Phase C when we have proper runtime support
}

//===----------------------------------------------------------------------===//
// EmitThunk
//===----------------------------------------------------------------------===//

void IREmitCXXInherit::EmitThunk(const CXXMethodDecl* MD) {
  if (!MD) return;

  auto* Module = Converter_.getModule();
  if (!Module) return;

  auto* Mangler = Converter_.getMangler();
  std::string ThunkName = Mangler->mangleThunk(MD);

  auto& TypeCtx = Converter_.getTypeContext();

  // Create thunk function type: same as original method
  auto* VoidPtrTy = TypeCtx.getPointerType(TypeCtx.getInt8Ty());
  ir::SmallVector<ir::IRType*, 8> ParamTys;
  ParamTys.push_back(VoidPtrTy);

  auto* FnTy = TypeCtx.getFunctionType(VoidPtrTy, std::move(ParamTys));
  auto* ThunkFn = Module->getOrInsertFunction(ThunkName, FnTy);

  if (ThunkFn && !ThunkFn->isDefinition()) {
    auto& Builder = Converter_.getBuilder();
    auto* EntryBB = ThunkFn->addBasicBlock("entry");
    Builder.setInsertPoint(EntryBB);

    // Thunk body: adjust this pointer, then call real function
    // Simplified: just return for now
    Builder.createRetVoid();

    Module->addRequiredFeature(ir::IRFeature::VirtualDispatch);
  }
}

//===----------------------------------------------------------------------===//
// EmitVTT
//===----------------------------------------------------------------------===//

void IREmitCXXInherit::EmitVTT(const CXXRecordDecl* RD) {
  if (!RD) return;

  // VTT (Virtual Table Table) is used for virtual inheritance.
  // Placeholder: defer full implementation to Phase C.
  auto* Module = Converter_.getModule();
  if (!Module) return;

  auto& TypeCtx = Converter_.getTypeContext();
  auto* OpaqueTy = TypeCtx.getOpaqueType("vtt");

  std::string VTTName = "_ZTT" + RD->getName().str();
  auto* VTTGV = Module->getOrInsertGlobal(VTTName, OpaqueTy);

  if (VTTGV) {
    Module->addRequiredFeature(ir::IRFeature::VirtualDispatch);
  }
}

} // namespace frontend
} // namespace blocktype
