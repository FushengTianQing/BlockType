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

/// ArrayCookie — new[]/delete[] 数组分配的 cookie 布局常量。
/// 布局: [count(size_t)][elem0][elem1]...[elemN-1]
/// new[] 返回 ptr + CookieSize，delete[] 从 ptr - CookieSize 读取 count。
namespace ArrayCookie {
/// cookie 大小（存储数组元素数量的 size_t 字节数）
constexpr uint64_t CookieSize = sizeof(uint64_t); // 8 on 64-bit
} // namespace ArrayCookie

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

  /// 基类 vptr 结构体索引缓存：(Derived, Base) → vptr 在 Derived 的 LLVM StructType 中的字段索引
  llvm::DenseMap<std::pair<const CXXRecordDecl *, const CXXRecordDecl *>,
                 unsigned>
      BaseVPtrIndexCache;

  /// VTable 组偏移缓存：(RD, Base) → Base 的 vtable 组在 RD 的 vtable 全局变量中的起始偏移（条目数）
  llvm::DenseMap<std::pair<const CXXRecordDecl *, const CXXRecordDecl *>,
                 unsigned>
      VTableGroupOffsetCache;

  /// D0 (deleting destructor) 缓存：CXXRecordDecl → llvm::Function
  llvm::DenseMap<const CXXRecordDecl *, llvm::Function *> DeletingDtorCache;

  /// 检查 CXXRecordDecl 是否有虚函数（包括继承的）
  static bool hasVirtualFunctions(CXXRecordDecl *RD);

  /// 检查 CXXRecordDecl 或其任何基类是否有虚函数
  bool hasVirtualFunctionsInHierarchy(CXXRecordDecl *RD);

  /// 获取 RTTI 类的 vtable 名称（libcxxabi 外部符号）
  static std::string getRTTIClassVTableName(CXXRecordDecl *RD);

  /// === Issue 8: 增强的覆盖检测辅助方法 ===

  /// 检查 DerivedMD 是否覆盖 BaseMD（综合匹配：名称、参数数量、const/volatile 限定符、ref-qualifier）。
  bool isMethodOverride(const CXXMethodDecl *DerivedMD,
                        const CXXMethodDecl *BaseMD) const;

  /// 在 RD 的方法中查找覆盖 BaseMD 的方法。返回 nullptr 如果未找到。
  CXXMethodDecl *findOverride(CXXRecordDecl *RD, CXXMethodDecl *BaseMD);

  /// 检查 MD 是否与层级结构中某个基类的虚函数匹配（递归搜索）。
  bool methodMatchesInHierarchy(CXXMethodDecl *MD, CXXRecordDecl *BaseRD);

  /// 检查 MD 是否已经在某个基类的 vtable 中（即它覆盖了基类的方法）。
  bool isMethodInAnyBase(CXXMethodDecl *MD, CXXRecordDecl *RD);

  /// === Issue 9: 虚析构函数辅助 ===

  /// 检查方法是否为虚析构函数。
  static bool isVirtualDestructor(CXXMethodDecl *MD);

  /// 获取虚函数在 vtable 中的条目数量（虚析构函数占 2 个条目：D1+D0，其他占 1 个）。
  static unsigned vtableEntryCount(CXXMethodDecl *MD);

  /// 生成 D0 (deleting destructor) 包装函数。
  llvm::Function *EmitDeletingDestructor(CXXRecordDecl *RD);

  /// === Issue 10: 多重继承 vtable 辅助 ===

  /// 获取第一个具有虚函数的直接基类（主基类），返回 nullptr 如果没有。
  CXXRecordDecl *getPrimaryBase(CXXRecordDecl *RD);

  /// 计算基类展平后在 LLVM StructType 中占用的字段数量。
  unsigned getBaseFieldCount(CXXRecordDecl *RD);

  /// 计算基类 Base 在 Derived 的 LLVM StructType 中的 vptr 字段索引。
  unsigned getBaseVPtrStructIndex(CXXRecordDecl *Derived, CXXRecordDecl *Base);

  /// 获取主 vptr 在类结构体中的索引（考虑 MI 情况下主基类的 vptr）。
  unsigned getPrimaryVPtrIndex(CXXRecordDecl *RD);

  /// 确定方法 MD 在类 RD 的 vtable 中属于哪个基类的组。
  /// 返回 nullptr 表示 MD 是 RD 自身新增的虚函数（属于主组）。
  CXXRecordDecl *findOwningBaseForMethod(CXXRecordDecl *RD, CXXMethodDecl *MD);

  /// 计算方法在指定基类组中的 vtable 索引（从 ott+RTTI 后开始计数）。
  unsigned computeIndexInBaseGroup(CXXMethodDecl *MD, CXXRecordDecl *BaseRD);

  /// 计算基类 Base 在类 RD 的 vtable 中的组偏移（绝对条目偏移）。
  unsigned computeVTableGroupOffset(CXXRecordDecl *RD, CXXRecordDecl *Base);

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

  /// 查找类的析构函数并返回对应的 llvm::Function。
  /// 如果类没有析构函数或析构函数是 trivial 的，返回 nullptr。
  llvm::Function *GetDestructor(CXXRecordDecl *RD);

  /// 在给定指针上调用析构函数（如果存在非 trivial 析构）。
  void EmitDestructorCall(CodeGenFunction &CGF, CXXRecordDecl *RD,
                           llvm::Value *Ptr);

  //===------------------------------------------------------------------===//
  // 虚函数表
  //===------------------------------------------------------------------===//

  /// 生成虚函数表。
  llvm::GlobalVariable *EmitVTable(CXXRecordDecl *RD);

  /// 获取虚函数表的类型。
  llvm::ArrayType *GetVTableType(CXXRecordDecl *RD);

  /// 计算虚函数表中方法的索引。
  unsigned GetVTableIndex(CXXMethodDecl *MD);

  /// 获取 vptr 在 LLVM StructType 中的字段索引。
  /// vptr 在需要时始终在索引 0（Itanium ABI 约定）。
  /// 如果类不需要 vptr，返回 -1。
  int GetVPtrIndex(CXXRecordDecl *RD);

  /// 生成虚函数调用（vptr load → GEP → load → indirect call）。
  /// \param StaticType 静态类型的 CXXRecordDecl（用于 MI 场景确定正确的 vptr），可为 nullptr
  llvm::Value *EmitVirtualCall(CodeGenFunction &CGF, CXXMethodDecl *MD,
                                llvm::Value *This,
                                llvm::ArrayRef<llvm::Value *> Args,
                                CXXRecordDecl *StaticType = nullptr);

  /// 初始化对象的 vptr 指针（在构造函数中调用）。
  void InitializeVTablePtr(CodeGenFunction &CGF, llvm::Value *This,
                            CXXRecordDecl *RD);

  //===------------------------------------------------------------------===//
  // RTTI（运行时类型信息）
  //===------------------------------------------------------------------===//

  /// 生成 RTTI 全局变量（typeinfo name + typeinfo 对象）。
  /// 返回 typeinfo 对象的 llvm::GlobalVariable*（即 _ZTI... 符号）。
  llvm::GlobalVariable *EmitTypeInfo(CXXRecordDecl *RD);

  /// 获取 catch clause 的 typeinfo（用于 landingpad clause）。
  /// 支持基础类型（int, float, pointer 等）和 record 类型。
  /// 返回 typeinfo 指针（i8*），catch-all 返回 nullptr。
  llvm::Value *EmitCatchTypeInfo(CodeGenFunction &CGF, QualType CatchType);

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
  /// \param Args 初始化参数列表（可为空，表示默认初始化）
  void EmitMemberInitializer(CodeGenFunction &CGF, CXXRecordDecl *Class,
                              llvm::Value *This, FieldDecl *Field,
                              llvm::ArrayRef<Expr *> Args);

  /// 生成所有基类和成员的析构代码（用于析构函数）。
  void EmitDestructorBody(CodeGenFunction &CGF, CXXRecordDecl *Class,
                           llvm::Value *This);
};

} // namespace blocktype
