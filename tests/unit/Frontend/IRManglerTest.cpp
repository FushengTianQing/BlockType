//===--- IRManglerTest.cpp - IRMangler Unit Tests -------------------------===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>

#include "blocktype/Frontend/IRMangler.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/DeclContext.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype;
using namespace blocktype::frontend;

namespace {

class IRManglerTest : public ::testing::Test {
protected:
  ASTContext Ctx;
  std::unique_ptr<ir::TargetLayout> Layout;
  std::unique_ptr<IRMangler> M;

  IRManglerTest()
    : Layout(ir::TargetLayout::Create("arm64-apple-macosx14.0")),
      M(std::make_unique<IRMangler>(*Layout)) {}

  // Helper: allocate a BuiltinType via the ASTContext allocator
  BuiltinType* makeBuiltin(BuiltinKind K) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(BuiltinType), alignof(BuiltinType));
    return new (Mem) BuiltinType(K);
  }

  // Helper: allocate a PointerType
  PointerType* makePointer(const Type* Pointee) {
    return Ctx.getPointerType(Pointee);
  }

  // Helper: allocate a LValueReferenceType
  LValueReferenceType* makeLRef(const Type* T) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(LValueReferenceType), alignof(LValueReferenceType));
    return new (Mem) LValueReferenceType(T);
  }

  // Helper: allocate a RValueReferenceType
  RValueReferenceType* makeRRef(const Type* T) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(RValueReferenceType), alignof(RValueReferenceType));
    return new (Mem) RValueReferenceType(T);
  }

  // Helper: allocate a ConstantArrayType
  ConstantArrayType* makeConstArray(const Type* Elem, uint64_t Size) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(ConstantArrayType), alignof(ConstantArrayType));
    return new (Mem) ConstantArrayType(Elem, nullptr, llvm::APInt(64, Size));
  }

  // Helper: allocate a FunctionType
  FunctionType* makeFuncType(const Type* Ret, llvm::ArrayRef<const Type*> Params,
                              bool Variadic = false) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(FunctionType), alignof(FunctionType));
    return new (Mem) FunctionType(Ret, Params, Variadic);
  }

  // Helper: allocate a ParmVarDecl
  ParmVarDecl* makeParam(llvm::StringRef Name, QualType T, unsigned Idx) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(ParmVarDecl), alignof(ParmVarDecl));
    return new (Mem) ParmVarDecl(SourceLocation(), Name, T, Idx);
  }

  // Helper: allocate a FunctionDecl
  FunctionDecl* makeFuncDecl(llvm::StringRef Name, QualType FTy,
                              llvm::ArrayRef<ParmVarDecl*> Params) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(FunctionDecl), alignof(FunctionDecl));
    return new (Mem) FunctionDecl(SourceLocation(), Name, FTy, Params);
  }

  // Helper: allocate a CXXRecordDecl
  CXXRecordDecl* makeCXXRecordDecl(llvm::StringRef Name) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(CXXRecordDecl), alignof(CXXRecordDecl));
    return new (Mem) CXXRecordDecl(SourceLocation(), Name, TagDecl::TK_class);
  }

  // Helper: allocate a RecordDecl
  RecordDecl* makeRecordDecl(llvm::StringRef Name) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(RecordDecl), alignof(RecordDecl));
    return new (Mem) RecordDecl(SourceLocation(), Name, TagDecl::TK_class);
  }

  // Helper: allocate a RecordType
  RecordType* makeRecordType(RecordDecl* RD) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(RecordType), alignof(RecordType));
    return new (Mem) RecordType(RD);
  }

  // Helper: allocate a CXXMethodDecl
  CXXMethodDecl* makeMethodDecl(llvm::StringRef Name, QualType FTy,
                                 llvm::ArrayRef<ParmVarDecl*> Params,
                                 CXXRecordDecl* Parent) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(CXXMethodDecl), alignof(CXXMethodDecl));
    return new (Mem) CXXMethodDecl(SourceLocation(), Name, FTy, Params, Parent);
  }

  // Helper: allocate a CXXConstructorDecl
  CXXConstructorDecl* makeCtorDecl(CXXRecordDecl* Parent,
                                    llvm::ArrayRef<ParmVarDecl*> Params) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(CXXConstructorDecl), alignof(CXXConstructorDecl));
    return new (Mem) CXXConstructorDecl(SourceLocation(), Parent, Params);
  }

  // Helper: allocate a CXXDestructorDecl
  CXXDestructorDecl* makeDtorDecl(CXXRecordDecl* Parent) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(CXXDestructorDecl), alignof(CXXDestructorDecl));
    return new (Mem) CXXDestructorDecl(SourceLocation(), Parent);
  }

  // Helper: allocate a VarDecl
  VarDecl* makeVarDecl(llvm::StringRef Name, QualType T) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(VarDecl), alignof(VarDecl));
    return new (Mem) VarDecl(SourceLocation(), Name, T);
  }

  // Helper: allocate a StringLiteral
  StringLiteral* makeStringLiteral(llvm::StringRef Val) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(StringLiteral), alignof(StringLiteral));
    return new (Mem) StringLiteral(SourceLocation(), Val);
  }

  // Helper: allocate an EnumDecl
  EnumDecl* makeEnumDecl(llvm::StringRef Name) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(EnumDecl), alignof(EnumDecl));
    return new (Mem) EnumDecl(SourceLocation(), Name);
  }

  // Helper: allocate an EnumType
  EnumType* makeEnumType(EnumDecl* ED) {
    void* Mem = Ctx.getAllocator().Allocate(sizeof(EnumType), alignof(EnumType));
    return new (Mem) EnumType(ED);
  }
};

//===----------------------------------------------------------------------===//
// 1. Free function mangling
//===----------------------------------------------------------------------===//

TEST_F(IRManglerTest, FreeFunctionNoParams) {
  auto* VoidTy = makeBuiltin(BuiltinKind::Void);
  auto* FTy = makeFuncType(VoidTy, {});
  auto* FD = makeFuncDecl("foo", QualType(FTy, Qualifier::None), {});
  EXPECT_EQ(M->mangleFunctionName(FD), "_Z3foov");
}

TEST_F(IRManglerTest, FreeFunctionOneIntParam) {
  auto* IntTy = makeBuiltin(BuiltinKind::Int);
  auto* FTy = makeFuncType(IntTy, {IntTy});
  auto* Param = makeParam("x", QualType(IntTy, Qualifier::None), 0);
  auto* FD = makeFuncDecl("foo", QualType(FTy, Qualifier::None), {Param});
  EXPECT_EQ(M->mangleFunctionName(FD), "_Z3fooi");
}

TEST_F(IRManglerTest, FreeFunctionTwoParams) {
  auto* IntTy = makeBuiltin(BuiltinKind::Int);
  auto* FloatTy = makeBuiltin(BuiltinKind::Float);
  auto* FTy = makeFuncType(IntTy, {IntTy, FloatTy});
  auto* P1 = makeParam("a", QualType(IntTy, Qualifier::None), 0);
  auto* P2 = makeParam("b", QualType(FloatTy, Qualifier::None), 1);
  auto* FD = makeFuncDecl("add", QualType(FTy, Qualifier::None), {P1, P2});
  EXPECT_EQ(M->mangleFunctionName(FD), "_Z3addif");
}

//===----------------------------------------------------------------------===//
// 2. Member function mangling
//===----------------------------------------------------------------------===//

TEST_F(IRManglerTest, MemberFunction) {
  auto* RD = makeCXXRecordDecl("Foo");
  auto* IntTy = makeBuiltin(BuiltinKind::Int);
  auto* FTy = makeFuncType(IntTy, {IntTy});
  auto* Param = makeParam("x", QualType(IntTy, Qualifier::None), 0);
  auto* MD = makeMethodDecl("bar", QualType(FTy, Qualifier::None), {Param}, RD);
  EXPECT_EQ(M->mangleFunctionName(MD), "_ZN3Foo3barEi");
}

TEST_F(IRManglerTest, MemberFunctionNoParams) {
  auto* RD = makeCXXRecordDecl("Foo");
  auto* VoidTy = makeBuiltin(BuiltinKind::Void);
  auto* FTy = makeFuncType(VoidTy, {});
  auto* MD = makeMethodDecl("baz", QualType(FTy, Qualifier::None), {}, RD);
  EXPECT_EQ(M->mangleFunctionName(MD), "_ZN3Foo3bazEv");
}

//===----------------------------------------------------------------------===//
// 3. Constructor / Destructor mangling
//===----------------------------------------------------------------------===//

TEST_F(IRManglerTest, ConstructorNoParams) {
  auto* RD = makeCXXRecordDecl("Foo");
  auto* Ctor = makeCtorDecl(RD, {});
  EXPECT_EQ(M->mangleFunctionName(Ctor), "_ZN3FooC1Ev");
}

TEST_F(IRManglerTest, ConstructorWithParams) {
  auto* RD = makeCXXRecordDecl("Foo");
  auto* IntTy = makeBuiltin(BuiltinKind::Int);
  auto* Param = makeParam("x", QualType(IntTy, Qualifier::None), 0);
  auto* Ctor = makeCtorDecl(RD, {Param});
  EXPECT_EQ(M->mangleFunctionName(Ctor), "_ZN3FooC1Ei");
}

TEST_F(IRManglerTest, Destructor) {
  auto* RD = makeCXXRecordDecl("Foo");
  auto* Dtor = makeDtorDecl(RD);
  EXPECT_EQ(M->mangleFunctionName(Dtor), "_ZN3FooD1Ev");
}

//===----------------------------------------------------------------------===//
// 4. VTable / RTTI mangling
//===----------------------------------------------------------------------===//

TEST_F(IRManglerTest, VTableName) {
  auto* RD = makeCXXRecordDecl("Foo");
  EXPECT_EQ(M->mangleVTable(RD), "_ZTV3Foo");
}

TEST_F(IRManglerTest, VTableNameLonger) {
  auto* RD = makeCXXRecordDecl("MyClass");
  EXPECT_EQ(M->mangleVTable(RD), "_ZTV7MyClass");
}

TEST_F(IRManglerTest, TypeInfoName) {
  auto* RD = makeCXXRecordDecl("Foo");
  EXPECT_EQ(M->mangleTypeInfo(RD), "_ZTI3Foo");
}

TEST_F(IRManglerTest, TypeInfoStringName) {
  auto* RD = makeCXXRecordDecl("Foo");
  EXPECT_EQ(M->mangleTypeInfoName(RD), "_ZTS3Foo");
}

//===----------------------------------------------------------------------===//
// 5. Thunk mangling
//===----------------------------------------------------------------------===//

TEST_F(IRManglerTest, ThunkMangling) {
  auto* RD = makeCXXRecordDecl("Foo");
  auto* VoidTy = makeBuiltin(BuiltinKind::Void);
  auto* FTy = makeFuncType(VoidTy, {});
  auto* MD = makeMethodDecl("bar", QualType(FTy, Qualifier::None), {}, RD);
  auto ThunkName = M->mangleThunk(MD);
  EXPECT_TRUE(ThunkName.starts_with("_ZThn0_"));
  // The rest should be the mangled member function name
  EXPECT_TRUE(ThunkName.find("_ZN3Foo3barEv") != std::string::npos);
}

//===----------------------------------------------------------------------===//
// 6. Guard variable mangling
//===----------------------------------------------------------------------===//

TEST_F(IRManglerTest, GuardVariableMangling) {
  auto* IntTy = makeBuiltin(BuiltinKind::Int);
  auto* VD = makeVarDecl("staticVar", QualType(IntTy, Qualifier::None));
  auto GuardName = M->mangleGuardVariable(VD);
  EXPECT_EQ(GuardName, "_ZGVstaticVar");
}

//===----------------------------------------------------------------------===//
// 7. String literal mangling
//===----------------------------------------------------------------------===//

TEST_F(IRManglerTest, StringLiteralMangling) {
  auto* SL = makeStringLiteral("hello");
  auto SLName = M->mangleStringLiteral(SL);
  EXPECT_TRUE(SLName.starts_with("_ZL"));
  // Should contain the length (5) and hex-encoded "hello"
  EXPECT_TRUE(SLName.find("5") != std::string::npos);
}

//===----------------------------------------------------------------------===//
// 8. Destructor variant mangling
//===----------------------------------------------------------------------===//

TEST_F(IRManglerTest, DtorVariantComplete) {
  auto* RD = makeCXXRecordDecl("Foo");
  EXPECT_EQ(M->mangleDtorName(RD, DtorVariant::Complete), "_ZN3FooD1Ev");
}

TEST_F(IRManglerTest, DtorVariantDeleting) {
  auto* RD = makeCXXRecordDecl("Foo");
  EXPECT_EQ(M->mangleDtorName(RD, DtorVariant::Deleting), "_ZN3FooD0Ev");
}

//===----------------------------------------------------------------------===//
// 9. Type mangling (verify parity with CodeGen::Mangler)
//===----------------------------------------------------------------------===//

TEST_F(IRManglerTest, BuiltinTypeVoid) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Void)), "v");
}

TEST_F(IRManglerTest, BuiltinTypeInt) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Int)), "i");
}

TEST_F(IRManglerTest, BuiltinTypeFloat) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Float)), "f");
}

TEST_F(IRManglerTest, PointerToInt) {
  auto* IntTy = makeBuiltin(BuiltinKind::Int);
  EXPECT_EQ(M->mangleType(makePointer(IntTy)), "Pi");
}

TEST_F(IRManglerTest, ConstInt) {
  auto* IntTy = makeBuiltin(BuiltinKind::Int);
  QualType ConstInt(IntTy, Qualifier::Const);
  EXPECT_EQ(M->mangleType(ConstInt), "Ki");
}

TEST_F(IRManglerTest, RecordType) {
  auto* RD = makeRecordDecl("MyClass");
  auto* RT = makeRecordType(RD);
  EXPECT_EQ(M->mangleType(RT), "7MyClass");
}

TEST_F(IRManglerTest, FunctionTypeIntInt) {
  auto* IntTy = makeBuiltin(BuiltinKind::Int);
  EXPECT_EQ(M->mangleType(makeFuncType(IntTy, {IntTy})), "FiiE");
}

//===----------------------------------------------------------------------===//
// 10. Substitution compression tests
//===----------------------------------------------------------------------===//

TEST_F(IRManglerTest, SubstitutionRecordTypeReuse) {
  auto* RD = makeRecordDecl("MyClass");
  auto* RT = makeRecordType(RD);
  auto* VoidTy = makeBuiltin(BuiltinKind::Void);
  auto* FTy = makeFuncType(VoidTy, {RT, RT});
  auto* P1 = makeParam("a", QualType(RT, Qualifier::None), 0);
  auto* P2 = makeParam("b", QualType(RT, Qualifier::None), 1);
  auto* FD = makeFuncDecl("foo", QualType(FTy, Qualifier::None), {P1, P2});
  EXPECT_EQ(M->mangleFunctionName(FD), "_Z3foo7MyClassS_");
}

TEST_F(IRManglerTest, SubstitutionRecordInMemberAndParam) {
  auto* RD = makeCXXRecordDecl("Foo");
  auto* RT = makeRecordType(RD);
  auto* VoidTy = makeBuiltin(BuiltinKind::Void);
  auto* FTy = makeFuncType(VoidTy, {RT});
  auto* Param = makeParam("x", QualType(RT, Qualifier::None), 0);
  auto* MD = makeMethodDecl("method", QualType(FTy, Qualifier::None), {Param}, RD);
  EXPECT_EQ(M->mangleFunctionName(MD), "_ZN3Foo6methodES_");
}

TEST_F(IRManglerTest, SubstitutionResetBetweenCalls) {
  auto* RD = makeRecordDecl("MyClass");
  auto* RT = makeRecordType(RD);
  auto* VoidTy = makeBuiltin(BuiltinKind::Void);
  auto* FTy = makeFuncType(VoidTy, {RT});
  auto* Param = makeParam("x", QualType(RT, Qualifier::None), 0);
  auto* FD = makeFuncDecl("single", QualType(FTy, Qualifier::None), {Param});
  EXPECT_EQ(M->mangleFunctionName(FD), "_Z6single7MyClass");
  EXPECT_EQ(M->mangleFunctionName(FD), "_Z6single7MyClass");
}

} // anonymous namespace
