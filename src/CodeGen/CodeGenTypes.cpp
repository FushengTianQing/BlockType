//===--- CodeGenTypes.cpp - C++ to LLVM Type Mapping ----------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/CodeGen/CodeGenTypes.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/TargetInfo.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// 类型转换主接口
//===----------------------------------------------------------------------===//

llvm::Type *CodeGenTypes::ConvertType(QualType T) {
  if (T.isNull()) return nullptr;

  const Type *Ty = T.getTypePtr();

  // 检查缓存
  auto It = TypeCache.find(Ty);
  if (It != TypeCache.end()) return It->second;

  // 按类型分派
  llvm::Type *Result = nullptr;
  switch (Ty->getTypeClass()) {
  case TypeClass::Builtin:
    Result = ConvertBuiltinType(llvm::cast<BuiltinType>(Ty)); break;
  case TypeClass::Pointer:
    Result = ConvertPointerType(llvm::cast<PointerType>(Ty)); break;
  case TypeClass::LValueReference:
  case TypeClass::RValueReference:
    Result = ConvertReferenceType(llvm::cast<ReferenceType>(Ty)); break;
  case TypeClass::ConstantArray:
  case TypeClass::IncompleteArray:
  case TypeClass::VariableArray:
    Result = ConvertArrayType(llvm::cast<ArrayType>(Ty)); break;
  case TypeClass::Function:
    Result = ConvertFunctionType(llvm::cast<FunctionType>(Ty)); break;
  case TypeClass::Record:
    Result = ConvertRecordType(llvm::cast<RecordType>(Ty)); break;
  case TypeClass::Enum:
    Result = ConvertEnumType(llvm::cast<EnumType>(Ty)); break;
  case TypeClass::Typedef:
    Result = ConvertTypedefType(llvm::cast<TypedefType>(Ty)); break;
  case TypeClass::TemplateSpecialization:
    Result = ConvertTemplateSpecializationType(
        llvm::cast<TemplateSpecializationType>(Ty)); break;
  case TypeClass::MemberPointer:
    Result = ConvertMemberPointerType(llvm::cast<MemberPointerType>(Ty)); break;
  case TypeClass::Auto:
    Result = ConvertAutoType(llvm::cast<AutoType>(Ty)); break;
  case TypeClass::Decltype:
    Result = ConvertDecltypeType(llvm::cast<DecltypeType>(Ty)); break;
  case TypeClass::Elaborated:
    // Elaborated type: resolve to the underlying named type
    if (auto *ET = llvm::dyn_cast<ElaboratedType>(Ty)) {
      Result = ConvertType(QualType(ET->getNamedType(), Qualifier::None));
    }
    break;
  case TypeClass::Dependent:
  case TypeClass::Unresolved:
  case TypeClass::TemplateTypeParm:
    // Dependent/unresolved types should not appear in CodeGen.
    // Use void as a safe fallback.
    Result = llvm::Type::getVoidTy(CGM.getLLVMContext());
    break;
  }

  if (Result) TypeCache[Ty] = Result;
  return Result;
}

llvm::Type *CodeGenTypes::ConvertTypeForMem(QualType T) {
  return ConvertType(T);
}

llvm::Type *CodeGenTypes::ConvertTypeForValue(QualType T) {
  if (T.isNull()) return nullptr;

  // 对于数组类型，值类型不存在于 LLVM 中（数组不能作为一等值传递）
  // 返回指向首元素的指针类型代替
  const Type *Ty = T.getTypePtr();
  if (Ty->getTypeClass() == TypeClass::ConstantArray ||
      Ty->getTypeClass() == TypeClass::IncompleteArray ||
      Ty->getTypeClass() == TypeClass::VariableArray) {
    auto *AT = llvm::cast<ArrayType>(Ty);
    return llvm::PointerType::get(
        ConvertType(QualType(AT->getElementType(), Qualifier::None)), 0);
  }

  return ConvertType(T);
}

//===----------------------------------------------------------------------===//
// 内部类型转换分派
//===----------------------------------------------------------------------===//

llvm::Type *CodeGenTypes::ConvertBuiltinType(const BuiltinType *BT) {
  llvm::LLVMContext &Ctx = CGM.getLLVMContext();
  switch (BT->getKind()) {
  case BuiltinKind::Void:
    return llvm::Type::getVoidTy(Ctx);
  case BuiltinKind::Bool:
    return llvm::Type::getInt1Ty(Ctx);
  case BuiltinKind::Char:
  case BuiltinKind::SignedChar:
  case BuiltinKind::UnsignedChar:
  case BuiltinKind::Char8:
    return llvm::Type::getInt8Ty(Ctx);
  case BuiltinKind::WChar:
    return llvm::Type::getInt32Ty(Ctx);
  case BuiltinKind::Char16:
    return llvm::Type::getInt16Ty(Ctx);
  case BuiltinKind::Char32:
    return llvm::Type::getInt32Ty(Ctx);
  case BuiltinKind::Short:
  case BuiltinKind::UnsignedShort:
    return llvm::Type::getInt16Ty(Ctx);
  case BuiltinKind::Int:
  case BuiltinKind::UnsignedInt:
    return llvm::Type::getInt32Ty(Ctx);
  case BuiltinKind::Long:
  case BuiltinKind::UnsignedLong:
    return llvm::Type::getInt64Ty(Ctx);
  case BuiltinKind::LongLong:
  case BuiltinKind::UnsignedLongLong:
    return llvm::Type::getInt64Ty(Ctx);
  case BuiltinKind::Int128:
  case BuiltinKind::UnsignedInt128:
    return llvm::Type::getInt128Ty(Ctx);
  case BuiltinKind::Float:
    return llvm::Type::getFloatTy(Ctx);
  case BuiltinKind::Double:
    return llvm::Type::getDoubleTy(Ctx);
  case BuiltinKind::LongDouble: {
    // macOS: long double == double → use getDoubleTy
    // Linux: long double == 80-bit extended → use getFP128Ty (LLVM 用 128-bit 表示)
    uint64_t LDSize = CGM.getTarget().getBuiltinSize(BuiltinKind::LongDouble);
    if (LDSize <= 8)
      return llvm::Type::getDoubleTy(Ctx);
    return llvm::Type::getFP128Ty(Ctx);
  }
  case BuiltinKind::Float128:
    return llvm::Type::getFP128Ty(Ctx);
  case BuiltinKind::NullPtr:
    return llvm::PointerType::get(Ctx, 0);
  default:
    return llvm::Type::getVoidTy(Ctx);
  }
}

llvm::Type *CodeGenTypes::ConvertPointerType(const PointerType *PT) {
  llvm::Type *Pointee = ConvertType(QualType(PT->getPointeeType(), Qualifier::None));
  if (!Pointee) Pointee = llvm::Type::getInt8Ty(CGM.getLLVMContext());
  return llvm::PointerType::get(Pointee, 0);
}

llvm::Type *CodeGenTypes::ConvertReferenceType(const ReferenceType *RT) {
  // 引用在 LLVM IR 中表示为指针
  llvm::Type *Referenced = ConvertType(
      QualType(RT->getReferencedType(), Qualifier::None));
  if (!Referenced) Referenced = llvm::Type::getInt8Ty(CGM.getLLVMContext());
  return llvm::PointerType::get(Referenced, 0);
}

llvm::Type *CodeGenTypes::ConvertArrayType(const ArrayType *AT) {
  llvm::Type *ElemTy = ConvertType(QualType(AT->getElementType(), Qualifier::None));
  if (!ElemTy) return nullptr;

  if (auto *CAT = llvm::dyn_cast<ConstantArrayType>(AT)) {
    return llvm::ArrayType::get(ElemTy, CAT->getSize().getZExtValue());
  }
  // IncompleteArray / VariableArray: use zero-length array as placeholder
  return llvm::ArrayType::get(ElemTy, 0);
}

llvm::Type *CodeGenTypes::ConvertFunctionType(const FunctionType *FT) {
  return GetFunctionType(FT);
}

llvm::Type *CodeGenTypes::ConvertRecordType(const RecordType *RT) {
  return GetRecordType(RT->getDecl());
}

llvm::Type *CodeGenTypes::ConvertEnumType(const EnumType *ET) {
  // 枚举类型映射到其底层整数类型
  if (auto *ED = ET->getDecl()) {
    QualType Underlying = ED->getUnderlyingType();
    if (!Underlying.isNull()) {
      return ConvertType(Underlying);
    }
  }
  // 默认 int/i32
  return llvm::Type::getInt32Ty(CGM.getLLVMContext());
}

llvm::Type *CodeGenTypes::ConvertTypedefType(const TypedefType *TT) {
  // Typedef: 递归到目标类型
  // TypedefNameDecl has getUnderlyingType()
  if (auto *TND = TT->getDecl()) {
    QualType Underlying = TND->getUnderlyingType();
    if (!Underlying.isNull()) return ConvertType(Underlying);
  }
  return llvm::Type::getInt32Ty(CGM.getLLVMContext());
}

llvm::Type *CodeGenTypes::ConvertTemplateSpecializationType(
    const TemplateSpecializationType *TST) {
  // Template specialization types should be resolved to concrete types
  // by Sema before reaching CodeGen. If we get here, try to resolve
  // through the template decl.
  if (auto *TD = TST->getTemplateDecl()) {
    if (auto *RD = llvm::dyn_cast<RecordDecl>(TD)) {
      QualType RT = CGM.getASTContext().getRecordType(RD);
      return ConvertRecordType(llvm::cast<RecordType>(RT.getTypePtr()));
    }
  }
  return llvm::Type::getInt8Ty(CGM.getLLVMContext());
}

llvm::Type *CodeGenTypes::ConvertMemberPointerType(const MemberPointerType *MPT) {
  // Member pointers are typically represented as a pair of pointers
  // { function-pointer-or-adjustment-offset, this-adjustment }
  // Simplified: use i8* as a placeholder
  return llvm::PointerType::get(CGM.getLLVMContext(), 0);
}

llvm::Type *CodeGenTypes::ConvertAutoType(const AutoType *AT) {
  if (AT->isDeduced()) {
    return ConvertType(AT->getDeducedType());
  }
  // Undeduced auto — should not reach CodeGen
  return llvm::Type::getInt32Ty(CGM.getLLVMContext());
}

llvm::Type *CodeGenTypes::ConvertDecltypeType(const DecltypeType *DT) {
  QualType Underlying = DT->getUnderlyingType();
  if (!Underlying.isNull()) return ConvertType(Underlying);
  return llvm::Type::getInt32Ty(CGM.getLLVMContext());
}

//===----------------------------------------------------------------------===//
// 函数类型
//===----------------------------------------------------------------------===//

llvm::FunctionType *CodeGenTypes::GetFunctionType(const FunctionType *FT) {
  llvm::Type *RetTy = ConvertType(QualType(FT->getReturnType(), Qualifier::None));
  if (!RetTy) RetTy = llvm::Type::getVoidTy(CGM.getLLVMContext());

  llvm::SmallVector<llvm::Type *, 8> ParamTys;
  for (const Type *PT : FT->getParamTypes()) {
    llvm::Type *P = ConvertType(QualType(PT, Qualifier::None));
    if (P) ParamTys.push_back(P);
  }

  return llvm::FunctionType::get(RetTy, ParamTys, FT->isVariadic());
}

bool CodeGenTypes::needsSRet(QualType RetTy) const {
  if (RetTy.isNull()) return false;
  const Type *Ty = RetTy.getTypePtr();

  // 只有 record 类型需要 sret
  if (!Ty->isRecordType()) return false;

  // 查询 TargetInfo：如果结构体可以在寄存器中返回，则不需要 sret
  return !CGM.getTarget().isStructReturnInRegister(RetTy);
}

bool CodeGenTypes::shouldUseInReg(QualType ParamTy) const {
  // 当前简化实现：不使用 inreg
  // 后续可扩展：某些 ABI（如 x86 32-bit）对 this 指针和小结构体使用 inreg
  (void)ParamTy;
  return false;
}

llvm::FunctionType *CodeGenTypes::GetFunctionTypeForDecl(FunctionDecl *FD) {
  // 委托给 GetFunctionABI，返回其中的 FnTy
  return GetFunctionABI(FD)->FnTy;
}

const FunctionABITy *CodeGenTypes::GetFunctionABI(FunctionDecl *FD) {
  // 检查缓存
  auto It = FunctionABICache.find(FD);
  if (It != FunctionABICache.end()) return &It->second;

  FunctionABITy ABI;
  QualType FT = FD->getType();

  if (!FT.isNull()) {
    if (auto *FnTy = llvm::dyn_cast<FunctionType>(FT.getTypePtr())) {
      QualType RetQT(QualType(FnTy->getReturnType(), Qualifier::None));
      llvm::Type *RetTy = ConvertType(RetQT);
      if (!RetTy) RetTy = llvm::Type::getVoidTy(CGM.getLLVMContext());

      // 检查返回值是否需要 sret
      bool UseSRet = needsSRet(RetQT);

      llvm::SmallVector<llvm::Type *, 8> ParamTys;
      llvm::SmallVector<ABIArgInfo, 8> ParamInfos;

      // sret：如果返回值通过内存传递，改为 void 返回并添加隐式首参数
      if (UseSRet) {
        ABI.RetInfo = ABIArgInfo::getSRet(RetTy);
        ParamTys.push_back(llvm::PointerType::get(RetTy, 0));
        ParamInfos.push_back(ABIArgInfo::getSRet(RetTy));
        RetTy = llvm::Type::getVoidTy(CGM.getLLVMContext());
      } else {
        ABI.RetInfo = ABIArgInfo::getDirect();
      }

      // 非静态成员函数需要添加 this 指针作为第一个参数
      if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(FD)) {
        if (!MD->isStatic()) {
          llvm::Type *ThisTy = nullptr;
          if (MD->getParent()) {
            llvm::StructType *ClassTy = GetRecordType(MD->getParent());
            ThisTy = llvm::PointerType::get(ClassTy, 0);
          } else {
            ThisTy = llvm::PointerType::get(CGM.getLLVMContext(), 0);
          }

          // this 指针的插入位置：sret 之后
          ParamTys.push_back(ThisTy);
          // this 指针 inreg 判断
          if (CGM.getTarget().isThisPassedInRegister()) {
            ParamInfos.push_back(ABIArgInfo::getInReg());
          } else {
            ParamInfos.push_back(ABIArgInfo::getDirect());
          }
        }
      }

      // 显式参数
      for (const Type *PT : FnTy->getParamTypes()) {
        llvm::Type *P = ConvertType(QualType(PT, Qualifier::None));
        if (P) {
          ParamTys.push_back(P);
          if (shouldUseInReg(QualType(PT, Qualifier::None))) {
            ParamInfos.push_back(ABIArgInfo::getInReg());
          } else {
            ParamInfos.push_back(ABIArgInfo::getDirect());
          }
        }
      }

      ABI.FnTy = llvm::FunctionType::get(RetTy, ParamTys, FnTy->isVariadic());
      ABI.ParamInfos = std::move(ParamInfos);
    }
  }

  if (!ABI.FnTy) {
    // Fallback: void()
    ABI.FnTy = llvm::FunctionType::get(
        llvm::Type::getVoidTy(CGM.getLLVMContext()), false);
    ABI.RetInfo = ABIArgInfo::getDirect();
  }

  // 同时缓存 FunctionTypeCache（向后兼容）
  FunctionTypeCache[FD] = ABI.FnTy;

  // 缓存 ABI 信息
  auto &Cached = FunctionABICache[FD];
  Cached = std::move(ABI);
  return &Cached;
}

//===----------------------------------------------------------------------===//
// 记录类型
//===----------------------------------------------------------------------===//

bool CodeGenTypes::hasVirtualInHierarchy(CXXRecordDecl *RD) {
  if (!RD) return false;
  for (CXXMethodDecl *MD : RD->methods()) {
    if (MD->isVirtual()) return true;
  }
  for (const auto &Base : RD->bases()) {
    QualType BT = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (hasVirtualInHierarchy(BaseRD)) return true;
      }
    }
  }
  return false;
}

unsigned CodeGenTypes::collectBaseClassFields(
    CXXRecordDecl *CXXRD, llvm::SmallVector<llvm::Type *, 16> &FieldTypes) {
  unsigned Count = 0;

  // 基类自身的 vptr（如果基类有虚函数且无更深的虚基类）
  bool HasVFunc = false;
  for (CXXMethodDecl *MD : CXXRD->methods()) {
    if (MD->isVirtual()) { HasVFunc = true; break; }
  }
  bool HasVirtualBase = false;
  for (const auto &Base : CXXRD->bases()) {
    QualType BT = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (hasVirtualInHierarchy(BaseRD)) { HasVirtualBase = true; break; }
      }
    }
  }
  if (HasVFunc && !HasVirtualBase) {
    FieldTypes.push_back(llvm::PointerType::get(CGM.getLLVMContext(), 0));
    ++Count;
  }

  // 递归添加基类的基类字段
  for (const auto &Base : CXXRD->bases()) {
    QualType BT = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        Count += collectBaseClassFields(BaseRD, FieldTypes);
      }
    }
  }

  // 添加基类自身字段
  for (FieldDecl *Field : CXXRD->fields()) {
    llvm::Type *FT = ConvertType(Field->getType());
    if (FT) {
      FieldTypes.push_back(FT);
      ++Count;
    }
  }

  return Count;
}

llvm::StructType *CodeGenTypes::GetCXXRecordType(CXXRecordDecl *RD) {
  return GetRecordType(RD); // GetRecordType already handles vptr for CXXRecordDecl
}

llvm::StructType *CodeGenTypes::GetRecordType(RecordDecl *RD) {
  // 检查缓存
  auto It = RecordTypeCache.find(RD);
  if (It != RecordTypeCache.end()) return It->second;

  // 创建 opaque struct type（先占位，避免递归）
  llvm::StructType *STy = llvm::StructType::create(
      CGM.getLLVMContext(), RD->getName());

  // 缓存 opaque type（处理递归引用）
  RecordTypeCache[RD] = STy;

  // 收集字段类型
  llvm::SmallVector<llvm::Type *, 16> FieldTypes;
  unsigned Idx = 0;

  // CXXRecordDecl: 添加 vptr + 基类字段 + 自身字段
  if (auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(RD)) {
    // vptr：当类或其层次结构中有虚函数，且没有直接基类具有虚函数时
    bool HasVPtr = hasVirtualInHierarchy(CXXRD);
    bool HasVirtualBase = false;
    for (const auto &Base : CXXRD->bases()) {
      QualType BT = Base.getType();
      if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
        if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
          if (hasVirtualInHierarchy(BaseRD)) { HasVirtualBase = true; break; }
        }
      }
    }
    if (HasVPtr && !HasVirtualBase) {
      FieldTypes.push_back(llvm::PointerType::get(CGM.getLLVMContext(), 0));
      ++Idx;
    }

    // 展平基类字段（递归包含基类的基类）
    for (const auto &Base : CXXRD->bases()) {
      QualType BT = Base.getType();
      if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
        if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
          Idx += collectBaseClassFields(BaseRD, FieldTypes);
        }
      }
    }
  }

  // 添加自身字段（并缓存 FieldDecl → GEP 索引映射）
  for (FieldDecl *Field : RD->fields()) {
    llvm::Type *FT = ConvertType(Field->getType());
    if (FT) {
      FieldTypes.push_back(FT);
      FieldIndexCache[Field] = Idx;
      ++Idx;
    }
  }

  // 设置结构体内容
  STy->setBody(FieldTypes);

  return STy;
}

unsigned CodeGenTypes::GetFieldIndex(FieldDecl *FD) {
  // 直接从缓存获取（在 GetRecordType 中已正确计算）
  auto It = FieldIndexCache.find(FD);
  if (It != FieldIndexCache.end()) return It->second;
  return 0;
}

//===----------------------------------------------------------------------===//
// 类型信息查询
//===----------------------------------------------------------------------===//

uint64_t CodeGenTypes::GetTypeSize(QualType T) const {
  return CGM.getTarget().getTypeSize(T);
}

uint64_t CodeGenTypes::GetTypeAlign(QualType T) const {
  return CGM.getTarget().getTypeAlign(T);
}

llvm::Constant *CodeGenTypes::GetSize(uint64_t SizeInBytes) {
  return llvm::ConstantInt::get(
      llvm::Type::getInt64Ty(CGM.getLLVMContext()), SizeInBytes);
}

llvm::Constant *CodeGenTypes::GetAlign(uint64_t AlignInBytes) {
  return llvm::ConstantInt::get(
      llvm::Type::getInt64Ty(CGM.getLLVMContext()), AlignInBytes);
}
