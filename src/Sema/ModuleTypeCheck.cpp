//===- ModuleTypeCheck.cpp - Cross-Module Type Checking ---------*- C++ -*-===//
//
// Part of the BlockType Project, under the BSD 3-Clause License.
// See the LICENSE file in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// This file implements cross-module type checking and symbol resolution.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/TypeCasting.h"
#include "blocktype/Module/ModuleManager.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {

//===------------------------------------------------------------------===//
// 跨模块类型检查
//===------------------------------------------------------------------===//

/// 检查跨模块类型是否一致
///
/// 用于验证导入的类型与本地声明的类型是否兼容
bool Sema::checkCrossModuleType(const Type *T1, const Type *T2,
                                 ModuleDecl *M1, ModuleDecl *M2) {
  if (!T1 || !T2) {
    return T1 == T2;
  }

  // 1. 相同类型（同一模块内定义）
  if (T1 == T2) {
    return true;
  }

  // 2. 类型种类必须相同
  if (T1->getTypeClass() != T2->getTypeClass()) {
    return false;
  }

  // 3. 根据类型种类进行递归检查
  switch (T1->getTypeClass()) {
  case TypeClass::Builtin:
    // 内置类型直接比较
    return T1 == T2;

  case TypeClass::Pointer: {
    // 指针类型：比较指向的类型
    const PointerType *PT1 = dyn_cast<PointerType>(T1);
    const PointerType *PT2 = dyn_cast<PointerType>(T2);
    if (!PT1 || !PT2) return false;
    return checkCrossModuleType(PT1->getPointeeType(),
                                PT2->getPointeeType(), M1, M2);
  }

  case TypeClass::LValueReference: {
    // 左值引用：比较引用的类型
    const LValueReferenceType *RT1 = dyn_cast<LValueReferenceType>(T1);
    const LValueReferenceType *RT2 = dyn_cast<LValueReferenceType>(T2);
    if (!RT1 || !RT2) return false;
    return checkCrossModuleType(RT1->getReferencedType(),
                                RT2->getReferencedType(), M1, M2);
  }

  case TypeClass::RValueReference: {
    // 右值引用：比较引用的类型
    const RValueReferenceType *RT1 = dyn_cast<RValueReferenceType>(T1);
    const RValueReferenceType *RT2 = dyn_cast<RValueReferenceType>(T2);
    if (!RT1 || !RT2) return false;
    return checkCrossModuleType(RT1->getReferencedType(),
                                RT2->getReferencedType(), M1, M2);
  }

  case TypeClass::ConstantArray: {
    // 常量数组类型：比较元素类型和大小
    const ConstantArrayType *AT1 = dyn_cast<ConstantArrayType>(T1);
    const ConstantArrayType *AT2 = dyn_cast<ConstantArrayType>(T2);
    if (!AT1 || !AT2) return false;
    if (AT1->getSize() != AT2->getSize()) {
      return false;
    }
    return checkCrossModuleType(AT1->getElementType(),
                                AT2->getElementType(), M1, M2);
  }

  case TypeClass::IncompleteArray:
  case TypeClass::VariableArray: {
    // 不完整数组和变长数组：只比较元素类型
    const ArrayType *AT1 = dyn_cast<ArrayType>(T1);
    const ArrayType *AT2 = dyn_cast<ArrayType>(T2);
    if (!AT1 || !AT2) return false;
    return checkCrossModuleType(AT1->getElementType(),
                                AT2->getElementType(), M1, M2);
  }

  case TypeClass::Function: {
    // 函数类型：比较参数和返回类型
    const FunctionType *FT1 = dyn_cast<FunctionType>(T1);
    const FunctionType *FT2 = dyn_cast<FunctionType>(T2);
    if (!FT1 || !FT2) return false;

    // 检查返回类型
    if (!checkCrossModuleType(FT1->getReturnType(),
                              FT2->getReturnType(), M1, M2)) {
      return false;
    }

    // 检查参数类型
    llvm::ArrayRef<const Type *> Params1 = FT1->getParamTypes();
    llvm::ArrayRef<const Type *> Params2 = FT2->getParamTypes();

    if (Params1.size() != Params2.size()) {
      return false;
    }

    for (size_t I = 0; I < Params1.size(); ++I) {
      if (!checkCrossModuleType(Params1[I], Params2[I], M1, M2)) {
        return false;
      }
    }

    return true;
  }

  case TypeClass::Record: {
    // 记录类型（类/结构体）：结构等价性检查
    const RecordType *RT1 = dyn_cast<RecordType>(T1);
    const RecordType *RT2 = dyn_cast<RecordType>(T2);
    if (!RT1 || !RT2) return false;

    RecordDecl *RD1 = RT1->getDecl();
    RecordDecl *RD2 = RT2->getDecl();

    // 如果来自同一模块，直接比较声明
    // TODO: 实现 getOwningModule
    // if (RD1->getOwningModule() == RD2->getOwningModule()) {
    //   return RD1 == RD2;
    // }

    // 跨模块：检查结构等价性
    return checkRecordEquivalence(RD1, RD2, M1, M2);
  }

  case TypeClass::Enum: {
    // 枚举类型：比较底层类型和枚举值
    const EnumType *ET1 = dyn_cast<EnumType>(T1);
    const EnumType *ET2 = dyn_cast<EnumType>(T2);
    if (!ET1 || !ET2) return false;

    EnumDecl *ED1 = ET1->getDecl();
    EnumDecl *ED2 = ET2->getDecl();

    // 如果来自同一模块，直接比较声明
    // TODO: 实现 getOwningModule
    // if (ED1->getOwningModule() == ED2->getOwningModule()) {
    //   return ED1 == ED2;
    // }

    // 跨模块：检查枚举等价性
    return checkEnumEquivalence(ED1, ED2, M1, M2);
  }

  case TypeClass::Typedef: {
    // typedef 类型：比较底层类型
    const TypedefType *TT1 = dyn_cast<TypedefType>(T1);
    const TypedefType *TT2 = dyn_cast<TypedefType>(T2);
    if (!TT1 || !TT2) return false;

    // 获取底层类型
    QualType Underlying1 = TT1->getDecl()->getUnderlyingType();
    QualType Underlying2 = TT2->getDecl()->getUnderlyingType();

    return checkCrossModuleType(Underlying1.getTypePtr(),
                                Underlying2.getTypePtr(), M1, M2);
  }

  case TypeClass::Auto:
    // auto 类型：需要推导后比较
    // TODO: 实现推导后的类型比较
    return false;

  case TypeClass::Decltype:
    // decltype 类型：比较表达式类型
    // TODO: 实现表达式类型比较
    return false;

  case TypeClass::TemplateTypeParm: {
    // 模板类型参数:比较索引和深度
    const TemplateTypeParmType *TTP1 = dyn_cast<TemplateTypeParmType>(T1);
    const TemplateTypeParmType *TTP2 = dyn_cast<TemplateTypeParmType>(T2);
    if (!TTP1 || !TTP2) return false;

    // 索引和深度必须相同
    if (TTP1->getIndex() != TTP2->getIndex() ||
        TTP1->getDepth() != TTP2->getDepth()) {
      return false;
    }

    // 参数包属性必须相同
    if (TTP1->isParameterPack() != TTP2->isParameterPack()) {
      return false;
    }

    return true;
  }

  case TypeClass::TemplateSpecialization: {
    // 模板特化类型:比较模板名和模板参数
    const TemplateSpecializationType *TST1 = dyn_cast<TemplateSpecializationType>(T1);
    const TemplateSpecializationType *TST2 = dyn_cast<TemplateSpecializationType>(T2);
    if (!TST1 || !TST2) return false;

    // 模板名必须相同
    if (TST1->getTemplateName() != TST2->getTemplateName()) {
      return false;
    }

    // 模板参数数量必须相同
    llvm::ArrayRef<TemplateArgument> Args1 = TST1->getTemplateArgs();
    llvm::ArrayRef<TemplateArgument> Args2 = TST2->getTemplateArgs();

    if (Args1.size() != Args2.size()) {
      return false;
    }

    // 逐个比较模板参数
    for (size_t I = 0; I < Args1.size(); ++I) {
      if (!checkTemplateArgumentEquivalence(Args1[I], Args2[I], M1, M2)) {
        return false;
      }
    }

    return true;
  }

  default:
    // 未知类型，保守处理
    return false;
  }
}

/// 检查记录类型的结构等价性
bool Sema::checkRecordEquivalence(RecordDecl *RD1, RecordDecl *RD2,
                                    ModuleDecl *M1, ModuleDecl *M2) {
  if (!RD1 || !RD2) {
    return RD1 == RD2;
  }

  // 1. 名称必须相同
  if (RD1->getName() != RD2->getName()) {
    return false;
  }

  // 2. 标签类型必须相同（class/struct/union）
  if (RD1->getTagKind() != RD2->getTagKind()) {
    return false;
  }

  // 3. 字段数量必须相同
  llvm::SmallVector<FieldDecl *, 8> Fields1;
  llvm::SmallVector<FieldDecl *, 8> Fields2;

  for (FieldDecl *F : RD1->fields()) {
    Fields1.push_back(F);
  }
  for (FieldDecl *F : RD2->fields()) {
    Fields2.push_back(F);
  }

  if (Fields1.size() != Fields2.size()) {
    return false;
  }

  // 4. 逐个比较字段
  for (size_t I = 0; I < Fields1.size(); ++I) {
    FieldDecl *F1 = Fields1[I];
    FieldDecl *F2 = Fields2[I];

    // 字段名必须相同
    if (F1->getName() != F2->getName()) {
      return false;
    }

    // 字段类型必须等价
    if (!checkCrossModuleType(F1->getType().getTypePtr(),
                              F2->getType().getTypePtr(), M1, M2)) {
      return false;
    }
  }

  return true;
}

/// 检查枚举类型的等价性
bool Sema::checkEnumEquivalence(EnumDecl *ED1, EnumDecl *ED2,
                                 ModuleDecl *M1, ModuleDecl *M2) {
  if (!ED1 || !ED2) {
    return ED1 == ED2;
  }

  // 1. 名称必须相同
  if (ED1->getName() != ED2->getName()) {
    return false;
  }

  // 2. 底层类型必须相同
  QualType Underlying1 = ED1->getUnderlyingType();
  QualType Underlying2 = ED2->getUnderlyingType();

  // 如果底层类型不为空，则进行比较
  if (!Underlying1.isNull() && !Underlying2.isNull()) {
    if (!checkCrossModuleType(Underlying1.getTypePtr(),
                              Underlying2.getTypePtr(), M1, M2)) {
      return false;
    }
  }

  // 3. 枚举值数量必须相同
  llvm::SmallVector<EnumConstantDecl *, 8> Enumerators1;
  llvm::SmallVector<EnumConstantDecl *, 8> Enumerators2;

  for (EnumConstantDecl *E : ED1->enumerators()) {
    Enumerators1.push_back(E);
  }
  for (EnumConstantDecl *E : ED2->enumerators()) {
    Enumerators2.push_back(E);
  }

  if (Enumerators1.size() != Enumerators2.size()) {
    return false;
  }

  // 4. 逐个比较枚举值
  for (size_t I = 0; I < Enumerators1.size(); ++I) {
    EnumConstantDecl *E1 = Enumerators1[I];
    EnumConstantDecl *E2 = Enumerators2[I];

    // 枚举值名称必须相同
    if (E1->getName() != E2->getName()) {
      return false;
    }

    // 枚举值必须相同
    // 如果两个枚举值都已求值,比较其值
    if (E1->hasVal() && E2->hasVal()) {
      if (E1->getVal() != E2->getVal()) {
        return false;
      }
    }
    // 如果其中一个未求值,保守处理,认为不同
    else if (E1->hasVal() != E2->hasVal()) {
      return false;
    }
  }

  return true;
}

//===------------------------------------------------------------------===//
// 跨模块符号解析
//===------------------------------------------------------------------===//

/// 解析跨模块符号引用
///
/// 当遇到跨模块符号引用时，验证符号类型的一致性
NamedDecl *Sema::resolveCrossModuleSymbol(llvm::StringRef Name,
                                           ModuleDecl *SourceMod,
                                           ModuleDecl *TargetMod) {
  if (!SourceMod || !TargetMod) {
    return nullptr;
  }

  // 1. 在目标模块中查找符号
  ModuleInfo *TargetInfo = ModMgr->getModuleInfo(TargetMod->getModuleName());
  if (!TargetInfo) {
    return nullptr;
  }

  // TODO: 实现符号查找
  return nullptr;
}

/// 验证导入模块的类型一致性
bool Sema::validateImportedModuleTypes(ModuleDecl *ImportingMod,
                                        ModuleDecl *ImportedMod) {
  if (!ImportingMod || !ImportedMod) {
    return false;
  }

  // 获取导入模块的信息
  ModuleInfo *ImportedInfo = ModMgr->getModuleInfo(ImportedMod->getModuleName());
  if (!ImportedInfo) {
    return false;
  }

  // TODO: 实现类型一致性验证
  return true;
}

/// 验证类型的完整性
bool Sema::validateTypeIntegrity(const Type *T, ModuleDecl *Mod) {
  if (!T) {
    return true;
  }

  // 根据类型种类进行验证
  switch (T->getTypeClass()) {
  case TypeClass::Builtin:
    // 内置类型总是完整的
    return true;

  case TypeClass::Pointer:
  case TypeClass::LValueReference:
  case TypeClass::RValueReference: {
    // 指针/引用：验证指向的类型
    const Type *Pointee = nullptr;
    if (auto *PT = dyn_cast<PointerType>(T)) {
      Pointee = PT->getPointeeType();
    } else if (auto *RT = dyn_cast<LValueReferenceType>(T)) {
      Pointee = RT->getReferencedType();
    } else if (auto *RT = dyn_cast<RValueReferenceType>(T)) {
      Pointee = RT->getReferencedType();
    }
    return validateTypeIntegrity(Pointee, Mod);
  }

  case TypeClass::ConstantArray:
  case TypeClass::IncompleteArray:
  case TypeClass::VariableArray: {
    // 数组：验证元素类型
    const ArrayType *AT = dyn_cast<ArrayType>(T);
    if (!AT) return false;
    return validateTypeIntegrity(AT->getElementType(), Mod);
  }

  case TypeClass::Function: {
    // 函数：验证返回类型和参数类型
    const FunctionType *FT = dyn_cast<FunctionType>(T);
    if (!FT) return false;
    if (!validateTypeIntegrity(FT->getReturnType(), Mod)) {
      return false;
    }
    for (const Type *ParamType : FT->getParamTypes()) {
      if (!validateTypeIntegrity(ParamType, Mod)) {
        return false;
      }
    }
    return true;
  }

  case TypeClass::Record: {
    // 记录类型：验证声明存在
    const RecordType *RT = dyn_cast<RecordType>(T);
    if (!RT) return false;
    RecordDecl *RD = RT->getDecl();
    if (!RD) {
      return false;
    }
    // 验证字段
    for (FieldDecl *F : RD->fields()) {
      if (!validateTypeIntegrity(F->getType().getTypePtr(), Mod)) {
        return false;
      }
    }
    return true;
  }

  case TypeClass::Enum: {
    // 枚举类型：验证声明存在
    const EnumType *ET = dyn_cast<EnumType>(T);
    if (!ET) return false;
    EnumDecl *ED = ET->getDecl();
    return ED != nullptr;
  }

  case TypeClass::Typedef: {
    // typedef：验证底层类型
    const TypedefType *TT = dyn_cast<TypedefType>(T);
    if (!TT) return false;

    // 获取底层类型
    QualType Underlying = TT->getDecl()->getUnderlyingType();
    return validateTypeIntegrity(Underlying.getTypePtr(), Mod);
  }

  default:
    // 其他类型：保守处理
    return true;
  }
}

/// 检查模板参数的等价性
bool Sema::checkTemplateArgumentEquivalence(const TemplateArgument &Arg1,
                                            const TemplateArgument &Arg2,
                                            ModuleDecl *M1, ModuleDecl *M2) {
  // 参数类型必须相同
  if (Arg1.getKind() != Arg2.getKind()) {
    return false;
  }

  switch (Arg1.getKind()) {
  case TemplateArgumentKind::Null:
    // 两个都是 null,认为等价
    return true;

  case TemplateArgumentKind::Type: {
    // 类型参数:比较类型
    return checkCrossModuleType(Arg1.getAsType().getTypePtr(),
                                Arg2.getAsType().getTypePtr(), M1, M2);
  }

  case TemplateArgumentKind::Integral: {
    // 整型参数:比较值
    return Arg1.getAsIntegral() == Arg2.getAsIntegral();
  }

  case TemplateArgumentKind::Declaration: {
    // 声明参数:比较声明
    ValueDecl *D1 = Arg1.getAsDecl();
    ValueDecl *D2 = Arg2.getAsDecl();
    if (!D1 || !D2) return D1 == D2;
    return D1->getName() == D2->getName();
  }

  case TemplateArgumentKind::NullPtr:
    // 两个都是 nullptr
    return true;

  case TemplateArgumentKind::Template: {
    // 模板参数:比较模板名
    TemplateDecl *TD1 = Arg1.getAsTemplate();
    TemplateDecl *TD2 = Arg2.getAsTemplate();
    if (!TD1 || !TD2) return TD1 == TD2;
    return TD1->getName() == TD2->getName();
  }

  case TemplateArgumentKind::TemplateExpansion: {
    // 模板展开参数:比较模板
    TemplateDecl *TD1 = Arg1.getAsTemplateOrTemplateExpansion();
    TemplateDecl *TD2 = Arg2.getAsTemplateOrTemplateExpansion();
    if (!TD1 || !TD2) return TD1 == TD2;
    return TD1->getName() == TD2->getName();
  }

  case TemplateArgumentKind::Expression: {
    // 表达式参数:尝试求值后比较
    // 简化实现:比较表达式类型
    Expr *E1 = Arg1.getAsExpr();
    Expr *E2 = Arg2.getAsExpr();
    if (!E1 || !E2) return E1 == E2;

    // 如果表达式类型相同,保守认为等价
    // 完整实现需要常量表达式求值
    return E1->getType() == E2->getType();
  }

  case TemplateArgumentKind::Pack: {
    // 参数包:逐个比较
    llvm::ArrayRef<TemplateArgument> Pack1 = Arg1.getAsPack();
    llvm::ArrayRef<TemplateArgument> Pack2 = Arg2.getAsPack();

    if (Pack1.size() != Pack2.size()) {
      return false;
    }

    for (size_t I = 0; I < Pack1.size(); ++I) {
      if (!checkTemplateArgumentEquivalence(Pack1[I], Pack2[I], M1, M2)) {
        return false;
      }
    }

    return true;
  }
  }

  return false;
}

} // namespace blocktype
