//===--- CGCXX.h - C++ Specific Code Generation --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CGCXX class for C++ specific code generation
// (classes, constructors/destructors, virtual tables, inheritance).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseMap.h"
#include "blocktype/AST/Decl.h"

namespace llvm {
class GlobalVariable;
class ArrayType;
class Function;
class Value;
class IRBuilderBase;
class AllocaInst;
} // namespace llvm

namespace blocktype {

class CodeGenModule;
class CodeGenFunction;

/// CGCXX — C++ 特有代码生成（类、构造/析构、虚函数表、继承）。
///
/// 职责（参照 Clang CGCXX + CGClass + CGVTables）：
/// 1. 生成类布局（字段偏移、大小、对齐、填充）
/// 2. 生成构造函数（基类初始化、成员初始化、构造函数体）
/// 3. 生成析构函数（成员析构、基类析构）
/// 4. 生成虚函数表（vtable 布局、vptr 初始化）
/// 5. 处理继承（单一/多重/虚继承）
/// 6. 生成成员函数调用（虚/非虚分派）
class CGCXX {
  CodeGenModule &CGM;

  /// VTable 缓存：CXXRecordDecl → llvm::GlobalVariable
  llvm::DenseMap<const CXXRecordDecl *, llvm::GlobalVariable *> VTables;

  /// RTTI 缓存：CXXRecordDecl → typeinfo 全局变量（_ZTI...）
  llvm::DenseMap<const CXXRecordDecl *, llvm::GlobalVariable *> TypeInfos;

  /// 字段偏移缓存：FieldDecl → 偏移量（字节）
  llvm::DenseMap<const FieldDecl *, uint64_t> FieldOffsetCache;

  /// 类大小缓存：CXXRecordDecl → 大小（字节）
  llvm::DenseMap<const CXXRecordDecl *, uint64_t> ClassSizeCache;

  /// 基类偏移缓存：(CXXRecordDecl*, CXXRecordDecl*) → 偏移量
  /// Key: (Derived, Base) → Base 在 Derived 中的偏移量
  llvm::DenseMap<std::pair<const CXXRecordDecl *, const CXXRecordDecl *>,
                 uint64_t>
      BaseOffsetCache;

  /// 检查 CXXRecordDecl 是否有虚函数（包括继承的）
  static bool hasVirtualFunctions(CXXRecordDecl *RD);

  /// 检查 CXXRecordDecl 或其任何基类是否有虚函数
  bool hasVirtualFunctionsInHierarchy(CXXRecordDecl *RD);

  /// 获取 RTTI 类的 vtable 名称（libcxxabi 外部符号）
  static std::string getRTTIClassVTableName(CXXRecordDecl *RD);

public:
  explicit CGCXX(CodeGenModule &M) : CGM(M) {}

  //===------------------------------------------------------------------===//
  // 类布局
  //===------------------------------------------------------------------===//

  /// 计算类的字段布局。返回每个字段的偏移量（字节）。
  llvm::SmallVector<uint64_t, 16> ComputeClassLayout(CXXRecordDecl *RD);

  /// 获取字段的偏移量。
  uint64_t GetFieldOffset(FieldDecl *FD);

  /// 获取类的完整大小。
  uint64_t GetClassSize(CXXRecordDecl *RD);

  /// 获取基类在派生类中的偏移量。
  uint64_t GetBaseOffset(CXXRecordDecl *Derived, CXXRecordDecl *Base);

  //===------------------------------------------------------------------===//
  // 构造函数 / 析构函数
  //===------------------------------------------------------------------===//

  /// 生成构造函数的完整代码（使用 CodeGenFunction）。
  void EmitConstructor(CXXConstructorDecl *Ctor, llvm::Function *Fn);

  /// 生成析构函数的完整代码（使用 CodeGenFunction）。
  void EmitDestructor(CXXDestructorDecl *Dtor, llvm::Function *Fn);

  //===------------------------------------------------------------------===//
  // 虚函数表
  //===------------------------------------------------------------------===//

  /// 生成虚函数表。
  llvm::GlobalVariable *EmitVTable(CXXRecordDecl *RD);

  /// 获取虚函数表的类型。
  llvm::ArrayType *GetVTableType(CXXRecordDecl *RD);

  /// 计算虚函数表中方法的索引。
  unsigned GetVTableIndex(CXXMethodDecl *MD);

  /// 生成虚函数调用（vptr load → GEP → load → indirect call）。
  llvm::Value *EmitVirtualCall(CodeGenFunction &CGF, CXXMethodDecl *MD,
                                llvm::Value *This,
                                llvm::ArrayRef<llvm::Value *> Args);

  /// 初始化对象的 vptr 指针（在构造函数中调用）。
  void InitializeVTablePtr(CodeGenFunction &CGF, llvm::Value *This,
                            CXXRecordDecl *RD);

  //===------------------------------------------------------------------===//
  // RTTI（运行时类型信息）
  //===------------------------------------------------------------------===//

  /// 生成 RTTI 全局变量（typeinfo name + typeinfo 对象）。
  /// 返回 typeinfo 对象的 llvm::GlobalVariable*（即 _ZTI... 符号）。
  llvm::GlobalVariable *EmitTypeInfo(CXXRecordDecl *RD);

  //===------------------------------------------------------------------===//
  // dynamic_cast
  //===------------------------------------------------------------------===//

  /// 生成 dynamic_cast 运行时检查代码。
  /// 处理 null check → 加载 vtable RTTI → 调用 __dynamic_cast。
  llvm::Value *EmitDynamicCast(CodeGenFunction &CGF,
                                class CXXDynamicCastExpr *CastExpr);

  //===------------------------------------------------------------------===//
  // 继承
  //===------------------------------------------------------------------===//

  /// 生成派生类到基类的偏移量（用于指针调整）。
  llvm::Value *EmitBaseOffset(CodeGenFunction &CGF, llvm::Value *DerivedPtr,
                               CXXRecordDecl *Base);

  /// 生成基类到派生类的偏移量。
  llvm::Value *EmitDerivedOffset(CodeGenFunction &CGF, llvm::Value *BasePtr,
                                  CXXRecordDecl *Derived);

  /// 将派生类指针调整为基类子对象指针。
  llvm::Value *EmitCastToBase(CodeGenFunction &CGF, CXXRecordDecl *Derived,
                               llvm::Value *DerivedPtr, CXXRecordDecl *Base);

  /// 将基类指针调整为派生类指针。
  llvm::Value *EmitCastToDerived(CodeGenFunction &CGF, CXXRecordDecl *Derived,
                                  llvm::Value *BasePtr, CXXRecordDecl *Base);

  //===------------------------------------------------------------------===//
  // 成员初始化
  //===------------------------------------------------------------------===//

  /// 生成基类初始化器。
  void EmitBaseInitializer(CodeGenFunction &CGF, CXXRecordDecl *Class,
                            llvm::Value *This, CXXRecordDecl::BaseSpecifier *Base,
                            Expr *Init);

  /// 生成成员初始化器。
  void EmitMemberInitializer(CodeGenFunction &CGF, CXXRecordDecl *Class,
                              llvm::Value *This, FieldDecl *Field, Expr *Init);

  /// 生成所有基类和成员的析构代码（用于析构函数）。
  void EmitDestructorBody(CodeGenFunction &CGF, CXXRecordDecl *Class,
                           llvm::Value *This);
};

} // namespace blocktype
