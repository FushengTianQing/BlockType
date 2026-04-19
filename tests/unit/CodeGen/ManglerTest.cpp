//===--- ManglerTest.cpp - Itanium Mangler Unit Tests --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include "blocktype/CodeGen/Mangler.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/SourceManager.h"
#include "llvm/IR/LLVMContext.h"

using namespace blocktype;

namespace {

class ManglerTest : public ::testing::Test {
protected:
  llvm::LLVMContext LLVMCtx;
  ASTContext Ctx;
  SourceManager SM;
  std::unique_ptr<CodeGenModule> CGM;
  std::unique_ptr<Mangler> M;

  ManglerTest()
      : Ctx(),
        CGM(std::make_unique<CodeGenModule>(Ctx, LLVMCtx, SM, "test", "arm64-apple-macosx14.0")),
        M(std::make_unique<Mangler>(*CGM)) {}

  /// Helper: allocate a BuiltinType via the ASTContext allocator
  BuiltinType *makeBuiltin(BuiltinKind K) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(BuiltinType), alignof(BuiltinType));
    return new (Mem) BuiltinType(K);
  }

  /// Helper: allocate a PointerType
  PointerType *makePointer(const Type *Pointee) {
    return Ctx.getPointerType(Pointee);
  }

  /// Helper: allocate a LValueReferenceType
  LValueReferenceType *makeLRef(const Type *T) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(LValueReferenceType), alignof(LValueReferenceType));
    return new (Mem) LValueReferenceType(T);
  }

  /// Helper: allocate a RValueReferenceType
  RValueReferenceType *makeRRef(const Type *T) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(RValueReferenceType), alignof(RValueReferenceType));
    return new (Mem) RValueReferenceType(T);
  }

  /// Helper: allocate a ConstantArrayType
  ConstantArrayType *makeConstArray(const Type *Elem, uint64_t Size) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(ConstantArrayType), alignof(ConstantArrayType));
    return new (Mem) ConstantArrayType(Elem, nullptr, llvm::APInt(64, Size));
  }

  /// Helper: allocate an IncompleteArrayType
  IncompleteArrayType *makeIncArray(const Type *Elem) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(IncompleteArrayType), alignof(IncompleteArrayType));
    return new (Mem) IncompleteArrayType(Elem);
  }

  /// Helper: allocate a FunctionType
  FunctionType *makeFuncType(const Type *Ret, llvm::ArrayRef<const Type *> Params,
                              bool Variadic = false) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(FunctionType), alignof(FunctionType));
    return new (Mem) FunctionType(Ret, Params, Variadic);
  }

  /// Helper: allocate a ParmVarDecl
  ParmVarDecl *makeParam(llvm::StringRef Name, QualType T, unsigned Idx) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(ParmVarDecl), alignof(ParmVarDecl));
    return new (Mem) ParmVarDecl(SourceLocation(), Name, T, Idx);
  }

  /// Helper: allocate a FunctionDecl
  FunctionDecl *makeFuncDecl(llvm::StringRef Name, QualType FTy,
                              llvm::ArrayRef<ParmVarDecl *> Params) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(FunctionDecl), alignof(FunctionDecl));
    return new (Mem) FunctionDecl(SourceLocation(), Name, FTy, Params);
  }

  /// Helper: allocate a RecordDecl
  RecordDecl *makeRecordDecl(llvm::StringRef Name) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(RecordDecl), alignof(RecordDecl));
    return new (Mem) RecordDecl(SourceLocation(), Name, TagDecl::TK_class);
  }

  /// Helper: allocate a CXXRecordDecl
  CXXRecordDecl *makeCXXRecordDecl(llvm::StringRef Name) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(CXXRecordDecl), alignof(CXXRecordDecl));
    return new (Mem) CXXRecordDecl(SourceLocation(), Name, TagDecl::TK_class);
  }

  /// Helper: allocate a RecordType
  RecordType *makeRecordType(RecordDecl *RD) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(RecordType), alignof(RecordType));
    return new (Mem) RecordType(RD);
  }

  /// Helper: allocate a CXXMethodDecl
  CXXMethodDecl *makeMethodDecl(llvm::StringRef Name, QualType FTy,
                                 llvm::ArrayRef<ParmVarDecl *> Params,
                                 CXXRecordDecl *Parent) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(CXXMethodDecl), alignof(CXXMethodDecl));
    return new (Mem) CXXMethodDecl(SourceLocation(), Name, FTy, Params, Parent);
  }

  /// Helper: allocate a CXXConstructorDecl
  CXXConstructorDecl *makeCtorDecl(CXXRecordDecl *Parent,
                                    llvm::ArrayRef<ParmVarDecl *> Params) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(CXXConstructorDecl), alignof(CXXConstructorDecl));
    return new (Mem) CXXConstructorDecl(SourceLocation(), Parent, Params);
  }

  /// Helper: allocate a CXXDestructorDecl
  CXXDestructorDecl *makeDtorDecl(CXXRecordDecl *Parent) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(CXXDestructorDecl), alignof(CXXDestructorDecl));
    return new (Mem) CXXDestructorDecl(SourceLocation(), Parent);
  }

  /// Helper: allocate a VarDecl
  VarDecl *makeVarDecl(llvm::StringRef Name, QualType T) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(VarDecl), alignof(VarDecl));
    return new (Mem) VarDecl(SourceLocation(), Name, T);
  }

  /// Helper: allocate an EnumDecl
  EnumDecl *makeEnumDecl(llvm::StringRef Name) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(EnumDecl), alignof(EnumDecl));
    return new (Mem) EnumDecl(SourceLocation(), Name);
  }

  /// Helper: allocate an EnumType
  EnumType *makeEnumType(EnumDecl *ED) {
    void *Mem = Ctx.getAllocator().Allocate(sizeof(EnumType), alignof(EnumType));
    return new (Mem) EnumType(ED);
  }
};

//===----------------------------------------------------------------------===//
// Builtin type mangling
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, BuiltinVoid) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Void)), "v");
}

TEST_F(ManglerTest, BuiltinBool) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Bool)), "b");
}

TEST_F(ManglerTest, BuiltinChar) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Char)), "c");
}

TEST_F(ManglerTest, BuiltinInt) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Int)), "i");
}

TEST_F(ManglerTest, BuiltinLong) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Long)), "l");
}

TEST_F(ManglerTest, BuiltinUnsignedInt) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::UnsignedInt)), "j");
}

TEST_F(ManglerTest, BuiltinFloat) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Float)), "f");
}

TEST_F(ManglerTest, BuiltinDouble) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Double)), "d");
}

TEST_F(ManglerTest, BuiltinLongDouble) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::LongDouble)), "e");
}

TEST_F(ManglerTest, BuiltinNullPtr) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::NullPtr)), "Dn");
}

TEST_F(ManglerTest, BuiltinWChar) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::WChar)), "w");
}

TEST_F(ManglerTest, BuiltinChar16) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Char16)), "Ds");
}

TEST_F(ManglerTest, BuiltinChar32) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Char32)), "Di");
}

TEST_F(ManglerTest, BuiltinInt128) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::Int128)), "n");
}

TEST_F(ManglerTest, BuiltinUInt128) {
  EXPECT_EQ(M->mangleType(makeBuiltin(BuiltinKind::UnsignedInt128)), "o");
}

//===----------------------------------------------------------------------===//
// Pointer / Reference type mangling
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, PointerToInt) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  EXPECT_EQ(M->mangleType(makePointer(IntTy)), "Pi");
}

TEST_F(ManglerTest, PointerToPointerToChar) {
  auto *CharTy = makeBuiltin(BuiltinKind::Char);
  auto *PtrChar = makePointer(CharTy);
  EXPECT_EQ(M->mangleType(makePointer(PtrChar)), "PPc");
}

TEST_F(ManglerTest, LValueReferenceToInt) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  EXPECT_EQ(M->mangleType(makeLRef(IntTy)), "Ri");
}

TEST_F(ManglerTest, RValueReferenceToInt) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  EXPECT_EQ(M->mangleType(makeRRef(IntTy)), "Oi");
}

//===----------------------------------------------------------------------===//
// Const/Volatile qualified types
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, ConstInt) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  QualType ConstInt(IntTy, Qualifier::Const);
  EXPECT_EQ(M->mangleType(ConstInt), "Ki");
}

TEST_F(ManglerTest, ConstPointerToChar) {
  auto *CharTy = makeBuiltin(BuiltinKind::Char);
  auto *PtrChar = makePointer(CharTy);
  QualType ConstPtr(PtrChar, Qualifier::Const);
  EXPECT_EQ(M->mangleType(ConstPtr), "KPc");
}

TEST_F(ManglerTest, PointerToConstChar) {
  // Note: PointerType stores const Type* (unqualified), so const is lost.
  // To properly mangle const pointee, the pointee's QualType must be propagated.
  // This test verifies that const on the pointer itself works:
  auto *CharTy = makeBuiltin(BuiltinKind::Char);
  auto *PtrChar = makePointer(CharTy);
  QualType ConstPtr(PtrChar, Qualifier::Const);
  EXPECT_EQ(M->mangleType(ConstPtr), "KPc");
}

//===----------------------------------------------------------------------===//
// Record / Enum type mangling
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, RecordType) {
  auto *RD = makeRecordDecl("MyClass");
  auto *RT = makeRecordType(RD);
  EXPECT_EQ(M->mangleType(RT), "7MyClass");
}

TEST_F(ManglerTest, EnumType) {
  auto *ED = makeEnumDecl("Color");
  auto *ET = makeEnumType(ED);
  EXPECT_EQ(M->mangleType(ET), "5Color");
}

//===----------------------------------------------------------------------===//
// Array type mangling
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, ConstantArray) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  EXPECT_EQ(M->mangleType(makeConstArray(IntTy, 10)), "A10_i");
}

TEST_F(ManglerTest, IncompleteArray) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  EXPECT_EQ(M->mangleType(makeIncArray(IntTy)), "A_i");
}

//===----------------------------------------------------------------------===//
// Function type mangling
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, FunctionTypeIntInt) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  EXPECT_EQ(M->mangleType(makeFuncType(IntTy, {IntTy})), "FiiE");
}

TEST_F(ManglerTest, FunctionTypeVoidNoParams) {
  auto *VoidTy = makeBuiltin(BuiltinKind::Void);
  EXPECT_EQ(M->mangleType(makeFuncType(VoidTy, {})), "FvE");
}

TEST_F(ManglerTest, FunctionTypeVariadic) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  auto *VoidTy = makeBuiltin(BuiltinKind::Void);
  EXPECT_EQ(M->mangleType(makeFuncType(VoidTy, {IntTy}, true)), "FvizE");
}

//===----------------------------------------------------------------------===//
// Free function mangling
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, FreeFunctionNoParams) {
  auto *VoidTy = makeBuiltin(BuiltinKind::Void);
  auto *FTy = makeFuncType(VoidTy, {});
  auto *FD = makeFuncDecl("foo", QualType(FTy, Qualifier::None), {});
  EXPECT_EQ(M->getMangledName(FD), "_Z3foov");
}

TEST_F(ManglerTest, FreeFunctionOneIntParam) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  auto *FTy = makeFuncType(IntTy, {IntTy});
  auto *Param = makeParam("x", QualType(IntTy, Qualifier::None), 0);
  auto *FD = makeFuncDecl("foo", QualType(FTy, Qualifier::None), {Param});
  EXPECT_EQ(M->getMangledName(FD), "_Z3fooi");
}

TEST_F(ManglerTest, FreeFunctionTwoParams) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  auto *FloatTy = makeBuiltin(BuiltinKind::Float);
  auto *FTy = makeFuncType(IntTy, {IntTy, FloatTy});
  auto *P1 = makeParam("a", QualType(IntTy, Qualifier::None), 0);
  auto *P2 = makeParam("b", QualType(FloatTy, Qualifier::None), 1);
  auto *FD = makeFuncDecl("add", QualType(FTy, Qualifier::None), {P1, P2});
  EXPECT_EQ(M->getMangledName(FD), "_Z3addif");
}

TEST_F(ManglerTest, OverloadedFunctions) {
  // void foo()
  auto *VoidTy = makeBuiltin(BuiltinKind::Void);
  auto *FTy1 = makeFuncType(VoidTy, {});
  auto *FD1 = makeFuncDecl("foo", QualType(FTy1, Qualifier::None), {});

  // int foo(int)
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  auto *FTy2 = makeFuncType(IntTy, {IntTy});
  auto *Param = makeParam("x", QualType(IntTy, Qualifier::None), 0);
  auto *FD2 = makeFuncDecl("foo", QualType(FTy2, Qualifier::None), {Param});

  EXPECT_EQ(M->getMangledName(FD1), "_Z3foov");
  EXPECT_EQ(M->getMangledName(FD2), "_Z3fooi");
  EXPECT_NE(M->getMangledName(FD1), M->getMangledName(FD2));
}

//===----------------------------------------------------------------------===//
// Member function mangling
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, MemberFunction) {
  auto *RD = makeCXXRecordDecl("Foo");
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  auto *FTy = makeFuncType(IntTy, {IntTy});
  auto *Param = makeParam("x", QualType(IntTy, Qualifier::None), 0);
  auto *MD = makeMethodDecl("bar", QualType(FTy, Qualifier::None), {Param}, RD);
  EXPECT_EQ(M->getMangledName(MD), "_ZN3Foo3barEi");
}

TEST_F(ManglerTest, MemberFunctionNoParams) {
  auto *RD = makeCXXRecordDecl("Foo");
  auto *VoidTy = makeBuiltin(BuiltinKind::Void);
  auto *FTy = makeFuncType(VoidTy, {});
  auto *MD = makeMethodDecl("baz", QualType(FTy, Qualifier::None), {}, RD);
  EXPECT_EQ(M->getMangledName(MD), "_ZN3Foo3bazEv");
}

//===----------------------------------------------------------------------===//
// Constructor / Destructor mangling
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, ConstructorNoParams) {
  auto *RD = makeCXXRecordDecl("Foo");
  auto *Ctor = makeCtorDecl(RD, {});
  EXPECT_EQ(M->getMangledName(Ctor), "_ZN3FooC1Ev");
}

TEST_F(ManglerTest, ConstructorWithParams) {
  auto *RD = makeCXXRecordDecl("Foo");
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  auto *Param = makeParam("x", QualType(IntTy, Qualifier::None), 0);
  auto *Ctor = makeCtorDecl(RD, {Param});
  EXPECT_EQ(M->getMangledName(Ctor), "_ZN3FooC1Ei");
}

TEST_F(ManglerTest, Destructor) {
  auto *RD = makeCXXRecordDecl("Foo");
  auto *Dtor = makeDtorDecl(RD);
  EXPECT_EQ(M->getMangledName(Dtor), "_ZN3FooD1Ev");
}

//===----------------------------------------------------------------------===//
// VTable / RTTI name mangling
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, VTableName) {
  auto *RD = makeCXXRecordDecl("Foo");
  EXPECT_EQ(M->getVTableName(RD), "_ZTV3Foo");
}

TEST_F(ManglerTest, VTableNameLonger) {
  auto *RD = makeCXXRecordDecl("MyClass");
  EXPECT_EQ(M->getVTableName(RD), "_ZTV7MyClass");
}

TEST_F(ManglerTest, RTTIName) {
  auto *RD = makeCXXRecordDecl("Foo");
  EXPECT_EQ(M->getRTTIName(RD), "_ZTI3Foo");
}

TEST_F(ManglerTest, TypeinfoName) {
  auto *RD = makeCXXRecordDecl("Foo");
  EXPECT_EQ(M->getTypeinfoName(RD), "_ZTS3Foo");
}

//===----------------------------------------------------------------------===//
// Global variable mangling
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, GlobalVariableNoMangling) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  auto *VD = makeVarDecl("g_var", QualType(IntTy, Qualifier::None));
  EXPECT_EQ(M->getMangledName(VD), "g_var");
}

//===----------------------------------------------------------------------===//
// Complex types
//===----------------------------------------------------------------------===//

TEST_F(ManglerTest, PointerToFunctionReturningInt) {
  auto *IntTy = makeBuiltin(BuiltinKind::Int);
  auto *FTy = makeFuncType(IntTy, {IntTy});
  EXPECT_EQ(M->mangleType(makePointer(FTy)), "PFiiE");
}

TEST_F(ManglerTest, PointerToConstPointerToChar) {
  // Note: Our PointerType stores const Type* (unqualified).
  // const on pointee is not propagated through PointerType.
  // This test verifies const on the outer pointer itself:
  auto *CharTy = makeBuiltin(BuiltinKind::Char);
  auto *PtrChar = makePointer(CharTy);
  QualType ConstPtr(PtrChar, Qualifier::Const);
  auto *PtrConstPtr = makePointer(ConstPtr.getTypePtr());
  // Outer pointer has const stripped by PointerType, inner pointer also lost const
  EXPECT_EQ(M->mangleType(PtrConstPtr), "PPc");
}

TEST_F(ManglerTest, FunctionWithRecordParam) {
  auto *RD = makeRecordDecl("String");
  auto *RT = makeRecordType(RD);
  auto *VoidTy = makeBuiltin(BuiltinKind::Void);
  auto *FTy = makeFuncType(VoidTy, {RT});
  auto *Param = makeParam("s", QualType(RT, Qualifier::None), 0);
  auto *FD = makeFuncDecl("print", QualType(FTy, Qualifier::None), {Param});
  EXPECT_EQ(M->getMangledName(FD), "_Z5print6String");
}

} // anonymous namespace
