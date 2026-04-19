//===--- Mangler.h - Itanium C++ ABI Name Mangling ----------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Mangler class for generating Itanium C++ ABI mangled
// names. Produces names like _Z3fooi, _ZN3Foo3barEi, _ZN3FooC1Ei, etc.
//
// Reference: https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangle
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ArrayRef.h"
#include "blocktype/AST/Type.h"
#include <string>

namespace blocktype {

/// DtorVariant — 析构函数的 Itanium ABI 变体。
enum class DtorVariant {
  Complete,  ///< D1: complete object destructor
  Deleting   ///< D0: deleting destructor (calls operator delete after D1)
};

class CodeGenModule;
class FunctionDecl;
class CXXMethodDecl;
class CXXConstructorDecl;
class CXXDestructorDecl;
class CXXRecordDecl;
class VarDecl;
class NamespaceDecl;
class ParmVarDecl;

/// Mangler — Itanium C++ ABI name mangling（参照 Clang ItaniumMangleContext）。
///
/// 职责：
/// 1. 为函数生成唯一符号名（支持重载区分）
/// 2. 为类成员函数生成限定名（ClassName::methodName）
/// 3. 为构造/析构函数生成特殊符号名
/// 4. 为虚函数表生成标准名称（_ZTV...）
/// 5. 为全局变量生成符号名
///
/// 局限（AST 限制）：
/// - FunctionDecl 没有父作用域指针，无法自动获取命名空间限定
/// - 命名空间限定需要 DeclContext 父链支持
class Mangler {
  CodeGenModule &CGM;

public:
  explicit Mangler(CodeGenModule &M) : CGM(M) {}

  //===------------------------------------------------------------------===//
  // 主要入口
  //===------------------------------------------------------------------===//

  /// 获取函数的 mangled 名称。
  std::string getMangledName(const FunctionDecl *FD);

  /// 获取全局变量的 mangled 名称。
  std::string getMangledName(const VarDecl *VD);

  /// 获取 VTable 名称（如 _ZTV3Foo）。
  std::string getVTableName(const CXXRecordDecl *RD);

  /// 获取 RTTI 名称（如 _ZTI3Foo）。
  std::string getRTTIName(const CXXRecordDecl *RD);

  /// 获取 VTable typeinfo name（如 _ZTS3Foo）。
  std::string getTypeinfoName(const CXXRecordDecl *RD);

  /// 生成指定类的析构函数变体的完整 mangled name。
  /// 用于编译器生成的 D0 (deleting destructor) 包装函数。
  std::string getMangledDtorName(const CXXRecordDecl *RD, DtorVariant Variant);

  //===------------------------------------------------------------------===//
  // Type mangling
  //===------------------------------------------------------------------===//

  /// 编码一个 QualType 为 Itanium mangling 字符串。
  std::string mangleType(QualType T);

  /// 编码一个 const Type* 为 Itanium mangling 字符串。
  std::string mangleType(const Type *T);

private:
  //===------------------------------------------------------------------===//
  // 内部编码方法
  //===------------------------------------------------------------------===//

  /// 编码 builtin 类型。
  void mangleBuiltinType(const BuiltinType *T, std::string &Out);

  /// 编码指针类型。
  void manglePointerType(const PointerType *T, std::string &Out);

  /// 编码引用类型。
  void mangleReferenceType(const ReferenceType *T, std::string &Out);

  /// 编码数组类型。
  void mangleArrayType(const ArrayType *T, std::string &Out);

  /// 编码函数类型。
  void mangleFunctionType(const FunctionType *T, std::string &Out);

  /// 编码 record 类型。
  void mangleRecordType(const RecordType *T, std::string &Out);

  /// 编码 enum 类型。
  void mangleEnumType(const EnumType *T, std::string &Out);

  /// 编码 typedef 类型（解包到底层类型）。
  void mangleTypedefType(const TypedefType *T, std::string &Out);

  /// 编码 elaborated 类型（解包到命名类型）。
  void mangleElaboratedType(const ElaboratedType *T, std::string &Out);

  /// 编码 template specialization 类型。
  void mangleTemplateSpecializationType(const TemplateSpecializationType *T,
                                        std::string &Out);

  /// 编码 QualType（处理 CVR 限定符）。
  void mangleQualType(QualType QT, std::string &Out);

  /// 编码函数参数类型列表。
  void mangleFunctionParamTypes(llvm::ArrayRef<ParmVarDecl *> Params,
                                std::string &Out);

  /// 编码嵌套名称限定符（namespace::Class::）。
  void mangleNestedName(const CXXRecordDecl *RD, std::string &Out);

  /// 编码 source-name（长度 + 名称）。
  static void mangleSourceName(llvm::StringRef Name, std::string &Out);

  /// 编码构造函数名称。
  void mangleCtorName(const CXXConstructorDecl *Ctor, std::string &Out);

  /// 编码析构函数名称。
  /// \param Variant 选择 D0 (deleting) 或 D1 (complete) 变体，默认 D1
  void mangleDtorName(const CXXDestructorDecl *Dtor, std::string &Out,
                      DtorVariant Variant = DtorVariant::Complete);
};

} // namespace blocktype
