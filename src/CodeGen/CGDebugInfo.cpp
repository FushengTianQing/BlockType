//===--- CGDebugInfo.cpp - DWARF Debug Information Gen --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/CodeGen/CGDebugInfo.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenTypes.h"
#include "blocktype/CodeGen/CGCXX.h"
#include "blocktype/CodeGen/TargetInfo.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// 构造 / 析构
//===----------------------------------------------------------------------===//

CGDebugInfo::CGDebugInfo(CodeGenModule &M) : CGM(M) {}

CGDebugInfo::~CGDebugInfo() = default;

//===----------------------------------------------------------------------===//
// 初始化 / 完成
//===----------------------------------------------------------------------===//

void CGDebugInfo::Initialize(llvm::StringRef FileName,
                              llvm::StringRef Directory) {
  if (Initialized) return;

  DIB = std::make_unique<llvm::DIBuilder>(*CGM.getModule());
  CurDir = Directory.str();

  CurFile = DIB->createFile(FileName, Directory);

  CU = DIB->createCompileUnit(
      llvm::dwarf::DW_LANG_C_plus_plus, CurFile,
      "BlockType Compiler", false, "", 0);

  Initialized = true;
}

void CGDebugInfo::Finalize() {
  if (!Initialized || !DIB) return;
  DIB->finalize();
}

//===----------------------------------------------------------------------===//
// 类型调试信息
//===----------------------------------------------------------------------===//

llvm::DIType *CGDebugInfo::GetDIType(QualType T) {
  if (T.isNull()) return nullptr;

  const Type *Ty = T.getTypePtr();

  auto It = TypeCache.find(Ty);
  if (It != TypeCache.end()) return It->second;

  llvm::DIType *Result = nullptr;

  switch (Ty->getTypeClass()) {
  case TypeClass::Builtin:
    Result = GetBuiltinDIType(llvm::cast<BuiltinType>(Ty));
    break;
  case TypeClass::Pointer:
    Result = GetPointerDIType(llvm::cast<PointerType>(Ty));
    break;
  case TypeClass::LValueReference:
  case TypeClass::RValueReference:
    Result = GetReferenceDIType(llvm::cast<ReferenceType>(Ty));
    break;
  case TypeClass::ConstantArray:
  case TypeClass::IncompleteArray:
  case TypeClass::VariableArray:
    Result = GetArrayDIType(llvm::cast<ArrayType>(Ty));
    break;
  case TypeClass::Record:
    Result = GetRecordDIType(llvm::cast<RecordType>(Ty));
    break;
  case TypeClass::Enum:
    Result = GetEnumDIType(llvm::cast<EnumType>(Ty));
    break;
  case TypeClass::Function:
    Result = GetFunctionDIType(llvm::cast<FunctionType>(Ty));
    break;
  case TypeClass::Typedef: {
    auto *TT = llvm::cast<TypedefType>(Ty);
    if (auto *TND = TT->getDecl()) {
      llvm::DIType *Underlying = GetDIType(TND->getUnderlyingType());
      Result = DIB->createTypedef(Underlying, TND->getName(), CurFile,
                                   0, nullptr);
    }
    break;
  }
  case TypeClass::Elaborated: {
    auto *ET = llvm::dyn_cast<ElaboratedType>(Ty);
    if (ET) Result = GetDIType(QualType(ET->getNamedType(), Qualifier::None));
    break;
  }
  default:
    break;
  }

  if (Result) TypeCache[Ty] = Result;
  return Result;
}

llvm::DIType *CGDebugInfo::GetBuiltinDIType(const BuiltinType *BT) {
  if (!BT) return nullptr;

  llvm::dwarf::TypeKind Encoding = llvm::dwarf::DW_ATE_signed;
  uint64_t Size = 32;

  switch (BT->getKind()) {
  case BuiltinKind::Void:
    return nullptr;
  case BuiltinKind::Bool:
    Encoding = llvm::dwarf::DW_ATE_boolean; Size = 8; break;
  case BuiltinKind::Char:
  case BuiltinKind::SignedChar:
    Encoding = llvm::dwarf::DW_ATE_signed_char; Size = 8; break;
  case BuiltinKind::UnsignedChar:
  case BuiltinKind::Char8:
    Encoding = llvm::dwarf::DW_ATE_unsigned_char; Size = 8; break;
  case BuiltinKind::WChar:
    Encoding = llvm::dwarf::DW_ATE_signed; Size = 32; break;
  case BuiltinKind::Char16:
    Encoding = llvm::dwarf::DW_ATE_unsigned; Size = 16; break;
  case BuiltinKind::Char32:
    Encoding = llvm::dwarf::DW_ATE_unsigned; Size = 32; break;
  case BuiltinKind::Short:
    Encoding = llvm::dwarf::DW_ATE_signed; Size = 16; break;
  case BuiltinKind::UnsignedShort:
    Encoding = llvm::dwarf::DW_ATE_unsigned; Size = 16; break;
  case BuiltinKind::Int:
    Encoding = llvm::dwarf::DW_ATE_signed; Size = 32; break;
  case BuiltinKind::UnsignedInt:
    Encoding = llvm::dwarf::DW_ATE_unsigned; Size = 32; break;
  case BuiltinKind::Long:
    Encoding = llvm::dwarf::DW_ATE_signed; Size = 64; break;
  case BuiltinKind::UnsignedLong:
    Encoding = llvm::dwarf::DW_ATE_unsigned; Size = 64; break;
  case BuiltinKind::LongLong:
    Encoding = llvm::dwarf::DW_ATE_signed; Size = 64; break;
  case BuiltinKind::UnsignedLongLong:
    Encoding = llvm::dwarf::DW_ATE_unsigned; Size = 64; break;
  case BuiltinKind::Int128:
    Encoding = llvm::dwarf::DW_ATE_signed; Size = 128; break;
  case BuiltinKind::UnsignedInt128:
    Encoding = llvm::dwarf::DW_ATE_unsigned; Size = 128; break;
  case BuiltinKind::Float:
    Encoding = llvm::dwarf::DW_ATE_float; Size = 32; break;
  case BuiltinKind::Double:
    Encoding = llvm::dwarf::DW_ATE_float; Size = 64; break;
  case BuiltinKind::LongDouble:
  case BuiltinKind::Float128:
    Encoding = llvm::dwarf::DW_ATE_float; Size = 128; break;
  case BuiltinKind::NullPtr:
    // nullptr_t 作为指针大小的地址类型
    Size = CGM.getTarget().getPointerSize() * 8;
    return DIB->createBasicType("nullptr_t", Size, llvm::dwarf::DW_ATE_address);
  default:
    Encoding = llvm::dwarf::DW_ATE_signed; Size = 32; break;
  }

  QualType QT(const_cast<BuiltinType *>(BT), Qualifier::None);
  Size = CGM.getTarget().getTypeSize(QT) * 8;

  return DIB->createBasicType(BT->getName(), Size, Encoding);
}

llvm::DIDerivedType *CGDebugInfo::GetPointerDIType(const PointerType *PT) {
  if (!PT) return nullptr;

  llvm::DIType *PointeeDI =
      GetDIType(QualType(PT->getPointeeType(), Qualifier::None));
  if (!PointeeDI)
    PointeeDI = DIB->createBasicType("void", 0, llvm::dwarf::DW_ATE_address);

  uint64_t PtrSize = CGM.getTarget().getPointerSize() * 8;
  return DIB->createPointerType(PointeeDI, PtrSize);
}

llvm::DIDerivedType *CGDebugInfo::GetReferenceDIType(const ReferenceType *RT) {
  if (!RT) return nullptr;

  llvm::DIType *ReferencedDI =
      GetDIType(QualType(RT->getReferencedType(), Qualifier::None));
  if (!ReferencedDI)
    ReferencedDI = DIB->createBasicType("void", 0, llvm::dwarf::DW_ATE_address);

  uint64_t PtrSize = CGM.getTarget().getPointerSize() * 8;
  return DIB->createReferenceType(llvm::dwarf::DW_TAG_reference_type,
                                   ReferencedDI, PtrSize);
}

llvm::DIType *CGDebugInfo::GetArrayDIType(const ArrayType *AT) {
  if (!AT) return nullptr;

  llvm::DIType *ElemDI =
      GetDIType(QualType(AT->getElementType(), Qualifier::None));
  if (!ElemDI) return nullptr;

  if (auto *CAT = llvm::dyn_cast<ConstantArrayType>(AT)) {
    uint64_t Count = CAT->getSize().getZExtValue();
    llvm::SmallVector<llvm::Metadata *, 8> Subscripts;
    Subscripts.push_back(
        DIB->getOrCreateSubrange(0, static_cast<int64_t>(Count)));
    auto SubArray = DIB->getOrCreateArray(Subscripts);
    uint64_t ElemSize = GetTypeSize(QualType(AT->getElementType(), Qualifier::None));
    uint32_t ElemAlign = GetTypeAlign(QualType(AT->getElementType(), Qualifier::None));
    return DIB->createArrayType(Count * ElemSize, ElemAlign, ElemDI, SubArray);
  }

  // 不完整/变长数组
  llvm::SmallVector<llvm::Metadata *, 8> Subscripts;
  Subscripts.push_back(DIB->getOrCreateSubrange(0, (int64_t)0));
  auto SubArray = DIB->getOrCreateArray(Subscripts);
  return DIB->createArrayType(0, 0, ElemDI, SubArray);
}

llvm::DIType *CGDebugInfo::GetRecordDIType(const RecordType *RT) {
  if (!RT) return nullptr;
  RecordDecl *RD = RT->getDecl();
  if (!RD) return nullptr;

  auto It = RecordDIcache.find(RD);
  if (It != RecordDIcache.end()) return It->second;

  // 前向声明（处理递归引用）
  llvm::DICompositeType *FwdDecl = DIB->createReplaceableCompositeType(
      llvm::dwarf::DW_TAG_structure_type, RD->getName(), CU, CurFile, 0);
  RecordDIcache[RD] = FwdDecl;

  llvm::SmallVector<llvm::Metadata *, 16> Elements;

  if (auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(RD)) {
    // 基类
    for (const auto &Base : CXXRD->bases()) {
      QualType BT = Base.getType();
      if (auto *BRT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
        if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(BRT->getDecl())) {
          llvm::DIType *BaseDI = GetDIType(BT);
          if (BaseDI) {
            uint64_t BaseOff = CGM.getCXX().GetBaseOffset(CXXRD, BaseRD);
            Elements.push_back(DIB->createInheritance(
                FwdDecl, BaseDI, BaseOff * 8, 0, llvm::DINode::FlagPublic));
          }
        }
      }
    }

    // vptr
    bool HasVFunc = false;
    for (CXXMethodDecl *MD : CXXRD->methods()) {
      if (MD->isVirtual()) { HasVFunc = true; break; }
    }
    if (HasVFunc) {
      uint64_t PtrSize = CGM.getTarget().getPointerSize();
      Elements.push_back(DIB->createMemberType(
          CU, "_vptr", CurFile, 0, PtrSize * 8, 8, 0,
          llvm::DINode::FlagArtificial,
          DIB->createBasicType("ptr", PtrSize * 8,
                                llvm::dwarf::DW_ATE_address)));
    }
  }

  // 字段
  for (FieldDecl *FD : RD->fields()) {
    llvm::DIType *FieldDI = GetDIType(FD->getType());
    if (!FieldDI) continue;

    uint64_t FieldOffset = CGM.getCXX().GetFieldOffset(FD);
    uint64_t FieldSize = GetTypeSize(FD->getType());
    uint32_t FieldAlign = GetTypeAlign(FD->getType());

    Elements.push_back(DIB->createMemberType(
        CU, FD->getName(), CurFile, 0,
        FieldSize * 8, FieldAlign * 8, FieldOffset * 8,
        llvm::DINode::FlagPublic, FieldDI));
  }

  auto ElemArray = DIB->getOrCreateArray(Elements);

  uint64_t RecordSize = 0;
  if (auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(RD)) {
    RecordSize = CGM.getCXX().GetClassSize(CXXRD);
  }
  if (RecordSize == 0) {
    uint64_t Offset = 0;
    for (FieldDecl *FD : RD->fields()) {
      uint64_t FSize = GetTypeSize(FD->getType());
      uint32_t FAlign = GetTypeAlign(FD->getType());
      if (FAlign > 0) Offset = (Offset + FAlign - 1) / FAlign * FAlign;
      Offset += FSize;
    }
    RecordSize = Offset > 0 ? Offset : 1;
  }

  // 替换前向声明为完整类型
  auto *CompleteTy = DIB->createStructType(
      CU, RD->getName(), CurFile, 0, RecordSize * 8, 8,
      llvm::DINode::FlagPublic, nullptr, ElemArray);
  DIB->replaceTemporary(llvm::TempMDNode(FwdDecl), CompleteTy);

  // 更新缓存：指向完整类型而非前向声明
  RecordDIcache[RD] = CompleteTy;

  return CompleteTy;
}

llvm::DIType *CGDebugInfo::GetEnumDIType(const EnumType *ET) {
  if (!ET) return nullptr;
  EnumDecl *ED = ET->getDecl();
  if (!ED) return nullptr;

  QualType Underlying = ED->getUnderlyingType();
  llvm::DIType *UnderlyingDI = nullptr;
  if (!Underlying.isNull()) UnderlyingDI = GetDIType(Underlying);
  if (!UnderlyingDI)
    UnderlyingDI = DIB->createBasicType("int", 32, llvm::dwarf::DW_ATE_signed);

  llvm::SmallVector<llvm::Metadata *, 16> Enumerators;
  for (EnumConstantDecl *ECD : ED->enumerators()) {
    llvm::APSInt Val = ECD->hasVal() ? ECD->getVal() : llvm::APSInt(32);
    Enumerators.push_back(DIB->createEnumerator(ECD->getName(), Val));
  }

  auto ElemArray = DIB->getOrCreateArray(Enumerators);
  uint64_t EnumSize = Underlying.isNull() ? 32
                      : CGM.getTarget().getTypeSize(Underlying) * 8;

  return DIB->createEnumerationType(CU, ED->getName(), CurFile, 0,
                                     EnumSize * 8, 8, ElemArray,
                                     UnderlyingDI, 0, "", false);
}

llvm::DISubroutineType *CGDebugInfo::GetFunctionDIType(const FunctionType *FT) {
  if (!FT) return nullptr;

  llvm::Metadata *RetDI = nullptr;
  QualType RetType(FT->getReturnType(), Qualifier::None);
  if (!RetType.isNull() && !RetType->isVoidType()) {
    RetDI = GetDIType(RetType);
  }

  llvm::SmallVector<llvm::Metadata *, 8> ParamDIs;
  ParamDIs.push_back(RetDI);

  for (const Type *PT : FT->getParamTypes()) {
    llvm::DIType *PDI = GetDIType(QualType(PT, Qualifier::None));
    ParamDIs.push_back(PDI);
  }

  auto TypeArray = DIB->getOrCreateTypeArray(ParamDIs);
  return DIB->createSubroutineType(TypeArray);
}

//===----------------------------------------------------------------------===//
// 函数/变量调试信息
//===----------------------------------------------------------------------===//

llvm::DISubprogram *CGDebugInfo::GetFunctionDI(FunctionDecl *FD) {
  if (!FD || !Initialized) return nullptr;

  auto It = FnCache.find(FD);
  if (It != FnCache.end()) return It->second;

  llvm::DISubroutineType *SubroutineTy = nullptr;
  QualType FT = FD->getType();
  if (!FT.isNull()) {
    if (auto *FnTy = llvm::dyn_cast<FunctionType>(FT.getTypePtr())) {
      SubroutineTy = GetFunctionDIType(FnTy);
    }
  }
  if (!SubroutineTy) {
    auto TypeArray = DIB->getOrCreateTypeArray(nullptr);
    SubroutineTy = DIB->createSubroutineType(TypeArray);
  }

  unsigned Line = getLineNumber(FD->getLocation());

  llvm::DISubprogram::DISPFlags SPFlags = llvm::DISubprogram::SPFlagDefinition;
  if (FD->isInline())
    SPFlags |= llvm::DISubprogram::SPFlagLocalToUnit;

  // 成员函数的作用域
  llvm::DIScope *Scope = CU;
  if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(FD)) {
    if (auto *Parent = MD->getParent()) {
      // 使用 RecordType 获取 Parent 的调试类型
      const RecordType *PRT = nullptr;
      // 从 ASTContext 获取 RecordType
      QualType ParentTy = CGM.getASTContext().getRecordType(Parent);
      llvm::DIType *ParentDI = GetDIType(ParentTy);
      if (ParentDI) Scope = llvm::cast<llvm::DIScope>(ParentDI);
    }
  }

  auto *SP = DIB->createFunction(Scope, FD->getName(), FD->getName(),
                                  CurFile, Line, SubroutineTy, Line,
                                  llvm::DINode::FlagPublic, SPFlags);

  FnCache[FD] = SP;
  CurrentFnSP = SP;
  return SP;
}

void CGDebugInfo::EmitGlobalVarDI(VarDecl *VD, llvm::GlobalVariable *GV) {
  if (!VD || !GV || !Initialized) return;

  llvm::DIType *TyDI = GetDIType(VD->getType());
  if (!TyDI) return;

  unsigned Line = getLineNumber(VD->getLocation());

  auto *DIGV = DIB->createGlobalVariableExpression(
      CU, VD->getName(), VD->getName(), CurFile, Line, TyDI,
      VD->isStatic());

  GV->addDebugInfo(DIGV);
}

void CGDebugInfo::EmitLocalVarDI(VarDecl *VD, llvm::AllocaInst *Alloca) {
  if (!VD || !Alloca || !Initialized) return;

  llvm::DIType *TyDI = GetDIType(VD->getType());
  if (!TyDI) return;

  unsigned Line = getLineNumber(VD->getLocation());

  llvm::DIScope *Scope = getCurrentScope();
  if (!Scope) Scope = CU;

  auto *DILV = DIB->createAutoVariable(Scope, VD->getName(), CurFile,
                                         Line, TyDI, false);
  auto *DILoc = getSourceLocation(VD->getLocation());

  llvm::BasicBlock *BB = Alloca->getParent();
  if (BB && !BB->empty()) {
    DIB->insertDeclare(Alloca, DILV, DIB->createExpression(),
                        DILoc, &*BB->getFirstInsertionPt());
  }
}

void CGDebugInfo::EmitParamDI(ParmVarDecl *PVD, llvm::AllocaInst *Alloca,
                                unsigned ArgNo) {
  if (!PVD || !Alloca || !Initialized) return;

  llvm::DIType *TyDI = GetDIType(PVD->getType());
  if (!TyDI) return;

  unsigned Line = getLineNumber(PVD->getLocation());

  llvm::DIScope *Scope = getCurrentScope();
  if (!Scope) Scope = CU;

  auto *DIPV = DIB->createParameterVariable(Scope, PVD->getName(),
                                              ArgNo + 1, CurFile, Line,
                                              TyDI, false);
  auto *DILoc = getSourceLocation(PVD->getLocation());

  llvm::BasicBlock *BB = Alloca->getParent();
  if (BB && !BB->empty()) {
    DIB->insertDeclare(Alloca, DIPV, DIB->createExpression(),
                        DILoc, &*BB->getFirstInsertionPt());
  }
}

//===----------------------------------------------------------------------===//
// 行号信息
//===----------------------------------------------------------------------===//

void CGDebugInfo::setLocation(SourceLocation Loc) {
  if (!Initialized || !Loc.isValid()) return;
  // 注意：完整的实现需要在 IRBuilder 上设置 CurrentDebugLocation
  // 但 CGDebugInfo 不直接持有 IRBuilder，需要通过 CodeGenFunction 集成
}

void CGDebugInfo::setFunctionLocation(llvm::Function *Fn, FunctionDecl *FD) {
  if (!Fn || !FD || !Initialized) return;
  auto *SP = GetFunctionDI(FD);
  if (SP) Fn->setSubprogram(SP);
}

//===----------------------------------------------------------------------===//
// 作用域信息
//===----------------------------------------------------------------------===//

llvm::DILexicalBlock *CGDebugInfo::CreateLexicalBlock(SourceLocation Loc) {
  if (!Initialized) return nullptr;
  llvm::DIScope *ParentScope = getCurrentScope();
  if (!ParentScope) ParentScope = CU;
  unsigned Line = getLineNumber(Loc);
  unsigned Col = getColumnNumber(Loc);
  return DIB->createLexicalBlock(ParentScope, CurFile, Line, Col);
}

llvm::DIScope *CGDebugInfo::getCurrentScope() {
  if (CurrentFnSP) return CurrentFnSP;
  return CU;
}

//===----------------------------------------------------------------------===//
// 辅助方法
//===----------------------------------------------------------------------===//

llvm::DILocation *CGDebugInfo::getSourceLocation(SourceLocation Loc) {
  if (!Loc.isValid() || !Initialized) return nullptr;
  unsigned Line = getLineNumber(Loc);
  unsigned Col = getColumnNumber(Loc);
  return llvm::DILocation::get(CGM.getLLVMContext(), Line, Col,
                                getCurrentScope());
}

uint64_t CGDebugInfo::GetTypeSize(QualType T) {
  if (T.isNull()) return 0;
  return CGM.getTarget().getTypeSize(T) * 8;
}

uint32_t CGDebugInfo::GetTypeAlign(QualType T) {
  if (T.isNull()) return 0;
  return static_cast<uint32_t>(CGM.getTarget().getTypeAlign(T) * 8);
}

unsigned CGDebugInfo::getLineNumber(SourceLocation Loc) {
  if (!Loc.isValid()) return 0;
  unsigned Offset = Loc.getOffset();
  return (Offset == 0) ? 0 : (Offset / 20 + 1);
}

unsigned CGDebugInfo::getColumnNumber(SourceLocation Loc) {
  if (!Loc.isValid()) return 0;
  return (Loc.getOffset() % 20) + 1;
}
