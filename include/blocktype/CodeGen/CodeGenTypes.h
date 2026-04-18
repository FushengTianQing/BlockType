//===--- CodeGenTypes.h - C++ to LLVM Type Mapping -----------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CodeGenTypes class for converting BlockType types
// to LLVM IR types.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Type.h"
#include "blocktype/AST/Type.h"

namespace llvm {
class FunctionType;
class StructType;
class Constant;
} // namespace llvm

namespace blocktype {

class CodeGenModule;
class FunctionDecl;
class RecordDecl;
class FieldDecl;
class CXXRecordDecl;

//===----------------------------------------------------------------------===//
// ABIArgInfo — 描述参数/返回值的 ABI 传递方式
//===----------------------------------------------------------------------===//

/// ABIArgKind — 参数或返回值在 ABI 层面的传递方式。
///
/// 参照 Clang ABIArgInfo，简化为 BlockType 当前需要的子集。
enum class ABIArgKind {
  Direct,   ///< 直接传递（值或指针，无特殊属性）
  SRet,     ///< 通过隐藏指针参数传递（结构体返回值 > ABI 限制）
  InReg,    ///< 通过寄存器传递（添加 inreg 属性）
};

/// ABIArgInfo — 描述一个参数或返回值的 ABI 传递细节。
struct ABIArgInfo {
  ABIArgKind Kind = ABIArgKind::Direct;

  /// 对于 SRet：指向的类型（即原始返回类型对应的 LLVM 类型）
  /// 仅当 Kind == SRet 时有意义
  llvm::Type *SRetType = nullptr;

  static ABIArgInfo getDirect() { return {ABIArgKind::Direct, nullptr}; }
  static ABIArgInfo getSRet(llvm::Type *T) { return {ABIArgKind::SRet, T}; }
  static ABIArgInfo getInReg() { return {ABIArgKind::InReg, nullptr}; }

  bool isSRet() const { return Kind == ABIArgKind::SRet; }
  bool isInReg() const { return Kind == ABIArgKind::InReg; }
  bool isDirect() const { return Kind == ABIArgKind::Direct; }
};

/// FunctionABITy — 函数的完整 ABI 信息。
///
/// 包含 LLVM 函数类型以及每个参数和返回值的 ABI 传递方式。
/// 在 GetFunctionTypeForDecl 中计算，供 GetOrCreateFunctionDecl
/// 在创建 llvm::Function 时设置参数属性。
struct FunctionABITy {
  llvm::FunctionType *FnTy = nullptr;

  /// 返回值的 ABI 信息
  ABIArgInfo RetInfo;

  /// 每个参数的 ABI 信息（与 FnTy 的参数一一对应，含隐式参数）
  llvm::SmallVector<ABIArgInfo, 8> ParamInfos;
};

/// CodeGenTypes — C++ 类型到 LLVM 类型的映射引擎。
///
/// 职责（参照 Clang CodeGenTypes）：
/// 1. 将 BlockType 的 QualType 映射为 llvm::Type*
/// 2. 缓存已转换的类型（避免重复创建）
/// 3. 生成函数类型（处理 this 指针、sret、inreg、变参等）
/// 4. 生成结构体类型（处理继承、虚基类、字段布局）
/// 5. 处理枚举类型（映射到底层整数类型）
class CodeGenTypes {
  CodeGenModule &CGM;

  /// Type → llvm::Type* 缓存
  llvm::DenseMap<const Type *, llvm::Type *> TypeCache;

  /// FunctionDecl → llvm::FunctionType* 缓存
  llvm::DenseMap<const FunctionDecl *, llvm::FunctionType *> FunctionTypeCache;

  /// FunctionDecl → FunctionABITy 缓存（sret/inreg 信息）
  llvm::DenseMap<const FunctionDecl *, FunctionABITy> FunctionABICache;

  /// RecordDecl → llvm::StructType* 缓存
  llvm::DenseMap<const RecordDecl *, llvm::StructType *> RecordTypeCache;

  /// FieldDecl → GEP 索引缓存（包含基类字段的正确偏移）
  llvm::DenseMap<const FieldDecl *, unsigned> FieldIndexCache;

public:
  explicit CodeGenTypes(CodeGenModule &M) : CGM(M) {}

  //===------------------------------------------------------------------===//
  // 类型转换主接口
  //===------------------------------------------------------------------===//

  /// 将 BlockType QualType 转换为 LLVM Type。
  /// 顶层 const/volatile 限定符被忽略（LLVM 不区分）。
  /// 引用类型 T& 被转换为指针类型 T*。
  llvm::Type *ConvertType(QualType T);

  /// 将 BlockType Type* 转换为 LLVM Type（用于内存操作）。
  llvm::Type *ConvertTypeForMem(QualType T);

  /// 将 BlockType Type* 转换为 LLVM Type（用于寄存器值，不涉及内存）。
  /// 例如：数组的值类型在 LLVM 中不存在，需要其他处理。
  llvm::Type *ConvertTypeForValue(QualType T);

  //===------------------------------------------------------------------===//
  // 函数类型
  //===------------------------------------------------------------------===//

  /// 将 BlockType FunctionType 转换为 LLVM FunctionType。
  llvm::FunctionType *GetFunctionType(const FunctionType *FT);

  /// 根据 FunctionDecl 生成 LLVM FunctionType。
  /// 处理 this 指针（成员函数）、变参等特殊情况。
  llvm::FunctionType *GetFunctionTypeForDecl(FunctionDecl *FD);

  /// 根据 FunctionDecl 获取完整的 ABI 信息（含 sret/inreg）。
  /// 返回值指针由 CodeGenTypes 内部缓存拥有，不需要调用者释放。
  const FunctionABITy *GetFunctionABI(FunctionDecl *FD);

  //===------------------------------------------------------------------===//
  // 记录类型（struct/class）
  //===------------------------------------------------------------------===//

  /// 将 RecordDecl 转换为 LLVM StructType。
  llvm::StructType *GetRecordType(RecordDecl *RD);

  /// 将 CXXRecordDecl 转换为 LLVM StructType（含虚函数表指针、基类子对象）。
  llvm::StructType *GetCXXRecordType(CXXRecordDecl *RD);

  /// 获取记录类型中字段的索引。
  unsigned GetFieldIndex(FieldDecl *FD);

  //===------------------------------------------------------------------===//
  // 类型信息查询
  //===------------------------------------------------------------------===//

  /// 获取类型的大小（字节）。
  uint64_t GetTypeSize(QualType T) const;

  /// 获取类型的对齐（字节）。
  uint64_t GetTypeAlign(QualType T) const;

  /// 获取类型大小常量。
  llvm::Constant *GetSize(uint64_t SizeInBytes);

  /// 获取类型对齐常量。
  llvm::Constant *GetAlign(uint64_t AlignInBytes);

private:
  //===------------------------------------------------------------------===//
  // 内部类型转换分派
  //===------------------------------------------------------------------===//

  llvm::Type *ConvertBuiltinType(const BuiltinType *BT);
  llvm::Type *ConvertPointerType(const PointerType *PT);
  llvm::Type *ConvertReferenceType(const ReferenceType *RT);
  llvm::Type *ConvertArrayType(const ArrayType *AT);
  llvm::Type *ConvertFunctionType(const FunctionType *FT);
  llvm::Type *ConvertRecordType(const RecordType *RT);
  llvm::Type *ConvertEnumType(const EnumType *ET);
  llvm::Type *ConvertTypedefType(const TypedefType *TT);
  llvm::Type *ConvertTemplateSpecializationType(
      const TemplateSpecializationType *TST);
  llvm::Type *ConvertMemberPointerType(const MemberPointerType *MPT);
  llvm::Type *ConvertAutoType(const AutoType *AT);
  llvm::Type *ConvertDecltypeType(const DecltypeType *DT);

  /// 递归收集基类字段类型（展平到派生类的 StructType 中）
  unsigned collectBaseClassFields(CXXRecordDecl *CXXRD,
                                  llvm::SmallVector<llvm::Type *, 16> &FieldTypes);

  /// 递归检查类或其基类是否有虚函数
  static bool hasVirtualInHierarchy(CXXRecordDecl *RD);

  /// 判断返回类型是否需要通过 sret 传递
  bool needsSRet(QualType RetTy) const;

  /// 判断参数是否应该标记 inreg
  bool shouldUseInReg(QualType ParamTy) const;
};

} // namespace blocktype
