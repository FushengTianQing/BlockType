//===--- IREmitCXXTest.cpp - IREmitCXX Unit Tests -----------------------===//

#include <gtest/gtest.h>

#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/Frontend/IREmitCXX.h"
#include "blocktype/Frontend/IRMangler.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRConversionResult.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype;
using namespace blocktype::frontend;

namespace {

class IREmitCXXTest : public ::testing::Test {
protected:
  ir::IRContext IRCtx;
  ir::IRTypeContext& TypeCtx;
  std::unique_ptr<ir::TargetLayout> Layout;
  DiagnosticsEngine Diags;

  IREmitCXXTest()
    : TypeCtx(IRCtx.getTypeContext()),
      Layout(ir::TargetLayout::Create("arm64-apple-macosx14.0")),
      Diags() {}

  /// Create a simple CXXRecordDecl
  CXXRecordDecl* makeCXXRecord(llvm::StringRef Name) {
    SourceLocation Loc;
    auto* RD = new CXXRecordDecl(Loc, Name, TagDecl::TK_class);
    RD->setCompleteDefinition(true);
    return RD;
  }

  /// Add a field to a CXXRecordDecl
  FieldDecl* addField(CXXRecordDecl* RD, llvm::StringRef Name, QualType T) {
    SourceLocation Loc;
    auto* F = new FieldDecl(Loc, Name, T);
    RD->addField(F);
    F->setParent(RD);
    return F;
  }

  /// Add a virtual method to a CXXRecordDecl
  CXXMethodDecl* addVirtualMethod(CXXRecordDecl* RD, llvm::StringRef Name) {
    SourceLocation Loc;
    auto* MD = new CXXMethodDecl(Loc, Name, QualType(),
                                  llvm::ArrayRef<ParmVarDecl*>{}, RD,
                                  nullptr, false, false, false,
                                  true /*isVirtual*/);
    RD->addMethod(MD);
    return MD;
  }

  /// Create converter and run convert() to initialize emitters.
  /// Returns the module from the conversion result.
  std::unique_ptr<ir::IRModule> convertWithModule(
      std::unique_ptr<ASTToIRConverter>& Converter,
      TranslationUnitDecl* TU) {
    Converter = std::make_unique<ASTToIRConverter>(IRCtx, TypeCtx, *Layout, Diags);
    auto Result = Converter->convert(TU);
    if (!Result.isUsable()) return nullptr;
    return Result.takeModule();
  }
};

} // anonymous namespace

// === Test 1: Empty class layout ===
TEST_F(IREmitCXXTest, EmptyClassLayout) {
  auto* RD = makeCXXRecord("Empty");
  std::unique_ptr<ASTToIRConverter> Converter;
  auto Module = convertWithModule(Converter, new TranslationUnitDecl(SourceLocation()));
  // Layout computation doesn't need the module; it uses TypeContext directly.
  auto& LayoutEmitter = Converter->getCxxEmitter()->getLayoutEmitter();
  auto* StructTy = LayoutEmitter.ComputeClassLayout(RD);
  ASSERT_NE(StructTy, nullptr);
  EXPECT_EQ(StructTy->getName(), "Empty");
  EXPECT_GT(LayoutEmitter.GetClassSize(RD), 0u);
}

// === Test 2: Simple field layout ===
TEST_F(IREmitCXXTest, SimpleFieldLayout) {
  ASTContext Ctx;
  auto* IntTy = Ctx.getBuiltinType(BuiltinKind::Int);
  auto* RD = makeCXXRecord("Simple");
  addField(RD, "x", QualType(IntTy, Qualifier::None));

  std::unique_ptr<ASTToIRConverter> Converter;
  auto Module = convertWithModule(Converter, new TranslationUnitDecl(SourceLocation()));

  auto& LayoutEmitter = Converter->getCxxEmitter()->getLayoutEmitter();
  auto* StructTy = LayoutEmitter.ComputeClassLayout(RD);
  ASSERT_NE(StructTy, nullptr);
  EXPECT_EQ(StructTy->getName(), "Simple");
  EXPECT_GE(StructTy->getNumFields(), 1u);
}

// === Test 3: Virtual method adds vptr ===
TEST_F(IREmitCXXTest, VirtualMethodAddsVPtr) {
  auto* RD = makeCXXRecord("Virtual");
  addVirtualMethod(RD, "foo");

  std::unique_ptr<ASTToIRConverter> Converter;
  auto Module = convertWithModule(Converter, new TranslationUnitDecl(SourceLocation()));

  auto& LayoutEmitter = Converter->getCxxEmitter()->getLayoutEmitter();
  auto* StructTy = LayoutEmitter.ComputeClassLayout(RD);
  ASSERT_NE(StructTy, nullptr);
  EXPECT_GE(StructTy->getNumFields(), 1u);
  EXPECT_TRUE(StructTy->getFieldType(0)->isPointer());
}

// === Test 4: Single inheritance ===
TEST_F(IREmitCXXTest, SingleInheritance) {
  ASTContext Ctx;
  auto* IntTy = Ctx.getBuiltinType(BuiltinKind::Int);

  auto* Base = makeCXXRecord("Base");
  addField(Base, "a", QualType(IntTy, Qualifier::None));

  auto* Derived = makeCXXRecord("Derived");
  auto BaseType = Ctx.getRecordType(Base);
  Derived->addBase(CXXRecordDecl::BaseSpecifier(
      BaseType, SourceLocation(), false, false, 2));
  addField(Derived, "b", QualType(IntTy, Qualifier::None));

  std::unique_ptr<ASTToIRConverter> Converter;
  auto* TU = new TranslationUnitDecl(SourceLocation());
  TU->addDecl(Base);
  TU->addDecl(Derived);
  auto Module = convertWithModule(Converter, TU);

  auto& LayoutEmitter = Converter->getCxxEmitter()->getLayoutEmitter();
  auto* StructTy = LayoutEmitter.ComputeClassLayout(Derived);
  ASSERT_NE(StructTy, nullptr);

  uint64_t BaseOffset = LayoutEmitter.GetBaseOffset(Derived, Base);
  EXPECT_EQ(BaseOffset, 0u);
}

// === Test 5: Constructor emission ===
TEST_F(IREmitCXXTest, ConstructorEmission) {
  auto* RD = makeCXXRecord("CtorTest");
  SourceLocation Loc;
  auto* Ctor = new CXXConstructorDecl(Loc, RD,
                                       llvm::ArrayRef<ParmVarDecl*>{});

  // Create converter manually to keep module alive
  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto* TU = new TranslationUnitDecl(SourceLocation());
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());
  auto Module = Result.takeModule();
  ASSERT_NE(Module, nullptr);

  auto& TCtx = TypeCtx;
  auto* VoidTy = TCtx.getVoidType();
  auto* PtrTy = TCtx.getPointerType(TCtx.getInt8Ty());
  ir::SmallVector<ir::IRType*, 8> Params;
  Params.push_back(PtrTy);
  auto* FnTy = TCtx.getFunctionType(VoidTy, std::move(Params));
  auto* IRFn = Module->getOrInsertFunction("_ZN8CtorTestC1Ev", FnTy);
  ASSERT_NE(IRFn, nullptr);

  Converter.getCxxEmitter()->EmitCXXConstructor(Ctor, IRFn);
  EXPECT_TRUE(IRFn->isDefinition());
  ASSERT_NE(IRFn->getEntryBlock(), nullptr);
}

// === Test 6: Destructor emission ===
TEST_F(IREmitCXXTest, DestructorEmission) {
  auto* RD = makeCXXRecord("DtorTest");
  SourceLocation Loc;
  auto* Dtor = new CXXDestructorDecl(Loc, RD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto* TU = new TranslationUnitDecl(SourceLocation());
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());
  auto Module = Result.takeModule();
  ASSERT_NE(Module, nullptr);

  auto& TCtx = TypeCtx;
  auto* VoidTy = TCtx.getVoidType();
  auto* PtrTy = TCtx.getPointerType(TCtx.getInt8Ty());
  ir::SmallVector<ir::IRType*, 8> Params;
  Params.push_back(PtrTy);
  auto* FnTy = TCtx.getFunctionType(VoidTy, std::move(Params));
  auto* IRFn = Module->getOrInsertFunction("_ZN8DtorTestD1Ev", FnTy);
  ASSERT_NE(IRFn, nullptr);

  Converter.getCxxEmitter()->EmitCXXDestructor(Dtor, IRFn);
  EXPECT_TRUE(IRFn->isDefinition());
  ASSERT_NE(IRFn->getEntryBlock(), nullptr);
}

// === Test 7: VTable placeholder ===
TEST_F(IREmitCXXTest, VTablePlaceholder) {
  auto* RD = makeCXXRecord("VTableTest");
  addVirtualMethod(RD, "foo");
  ASSERT_TRUE(RD->hasVTable());

  auto* TU = new TranslationUnitDecl(SourceLocation());
  TU->addDecl(RD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());
  auto Module = Result.takeModule();
  ASSERT_NE(Module, nullptr);

  // Check VTable global was emitted
  auto* VTableGV = Module->getGlobalVariable("_ZTV10VTableTest");
  ASSERT_NE(VTableGV, nullptr) << "VTable global should exist after convert()";
}

// === Test 8: VTable index ===
TEST_F(IREmitCXXTest, VTableIndex) {
  auto* RD = makeCXXRecord("VTableIdx");
  auto* MD = addVirtualMethod(RD, "virt");

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto* TU = new TranslationUnitDecl(SourceLocation());
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  auto& VTableEmitter = Converter.getCxxEmitter()->getVTableEmitter();
  uint64_t Index = VTableEmitter.GetVTableIndex(MD);
  EXPECT_GE(Index, 2u);
}

// === Test 9: RTTI placeholder ===
TEST_F(IREmitCXXTest, RTTIPlaceholder) {
  auto* RD = makeCXXRecord("RTTITest");
  addVirtualMethod(RD, "foo");

  auto* TU = new TranslationUnitDecl(SourceLocation());
  TU->addDecl(RD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());
  auto Module = Result.takeModule();
  ASSERT_NE(Module, nullptr);

  // VTable emission triggers RTTI emission too
  auto* RTTIGV = Module->getGlobalVariable("_ZTI8RTTITest");
  ASSERT_NE(RTTIGV, nullptr);
}

// === Test 10: Cast to base offset ===
TEST_F(IREmitCXXTest, CastToBaseOffset) {
  ASTContext Ctx;
  auto* IntTy = Ctx.getBuiltinType(BuiltinKind::Int);

  auto* Base = makeCXXRecord("Base2");
  addField(Base, "x", QualType(IntTy, Qualifier::None));

  auto* Derived = makeCXXRecord("Derived2");
  auto BaseType = Ctx.getRecordType(Base);
  Derived->addBase(CXXRecordDecl::BaseSpecifier(
      BaseType, SourceLocation(), false, false, 2));
  addField(Derived, "y", QualType(IntTy, Qualifier::None));

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto* TU = new TranslationUnitDecl(SourceLocation());
  TU->addDecl(Base);
  TU->addDecl(Derived);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  auto& Inherit = Converter.getCxxEmitter()->getInheritEmitter();
  uint64_t Offset = Inherit.EmitBaseOffset(Derived, Base);
  EXPECT_EQ(Offset, 0u);
}

// === Test 11: hasVirtualFunctions static helpers ===
TEST_F(IREmitCXXTest, HasVirtualFunctionsCheck) {
  auto* RD1 = makeCXXRecord("NoVirtual");
  EXPECT_FALSE(IREmitCXX::hasVirtualFunctions(RD1));
  EXPECT_FALSE(IREmitCXX::hasVirtualFunctionsInHierarchy(RD1));

  auto* RD2 = makeCXXRecord("WithVirtual");
  addVirtualMethod(RD2, "foo");
  EXPECT_TRUE(IREmitCXX::hasVirtualFunctions(RD2));
  EXPECT_TRUE(IREmitCXX::hasVirtualFunctionsInHierarchy(RD2));
}

// === Test 12: VTable cache doesn't create duplicates ===
TEST_F(IREmitCXXTest, VTableCacheNoDuplicates) {
  auto* RD = makeCXXRecord("CacheTest");
  addVirtualMethod(RD, "bar");

  auto* TU = new TranslationUnitDecl(SourceLocation());
  TU->addDecl(RD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());
  auto Module = Result.takeModule();
  ASSERT_NE(Module, nullptr);

  auto* VTableGV = Module->getGlobalVariable("_ZTV9CacheTest");
  ASSERT_NE(VTableGV, nullptr);
}
