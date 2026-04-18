//===--- CGDebugInfo.h - DWARF Debug Information Generation --*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CGDebugInfo class for generating DWARF debug
// information for BlockType programs.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/IR/DIBuilder.h"
#include "llvm/ADT/DenseMap.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"

namespace llvm {
class AllocaInst;
class GlobalVariable;
}

namespace blocktype {

class CodeGenModule;

/// CGDebugInfo — DWARF 调试信息生成。
///
/// 职责（参照 Clang CGDebugInfo）：
/// 1. 生成编译单元信息（文件名、命令行、语言标准）
/// 2. 生成类型调试信息（Builtin/Pointer/Array/Record/Enum/Function）
/// 3. 生成函数和变量的调试信息（名称、类型、位置）
/// 4. 生成行号信息（源位置 → IR 位置的映射）
/// 5. 生成作用域信息（词法块、命名空间）
class CGDebugInfo {
  CodeGenModule &CGM;
  std::unique_ptr<llvm::DIBuilder> DIB;

  /// 编译单元
  llvm::DICompileUnit *CU = nullptr;

  /// 类型缓存：QualType → DIType
  llvm::DenseMap<const Type *, llvm::DIType *> TypeCache;

  /// 函数缓存：FunctionDecl → DISubprogram
  llvm::DenseMap<const FunctionDecl *, llvm::DISubprogram *> FnCache;

  /// 当前正在生成的函数的 DISubprogram（用于变量作用域）
  llvm::DISubprogram *CurrentFnSP = nullptr;

  /// RecordDecl → DICompositeType（用于前向声明解析）
  llvm::DenseMap<const RecordDecl *, llvm::DICompositeType *> RecordDIcache;

  /// 当前源文件
  llvm::DIFile *CurFile = nullptr;

  /// 当前源目录
  std::string CurDir;

  /// 是否已初始化
  bool Initialized = false;

public:
  explicit CGDebugInfo(CodeGenModule &M);
  ~CGDebugInfo();

  /// 初始化调试信息（创建编译单元）。
  void Initialize(llvm::StringRef FileName, llvm::StringRef Directory);

  /// 完成调试信息生成（finalize DIBuilder）。
  void Finalize();

  /// 是否已初始化
  bool isInitialized() const { return Initialized; }

  //===------------------------------------------------------------------===//
  // 编译单元
  //===------------------------------------------------------------------===//

  /// 获取编译单元
  llvm::DICompileUnit *getCompileUnit() const { return CU; }

  /// 获取 DIFile
  llvm::DIFile *getFile() const { return CurFile; }

  //===------------------------------------------------------------------===//
  // 类型调试信息
  //===------------------------------------------------------------------===//

  /// 获取类型的调试信息。
  llvm::DIType *GetDIType(QualType T);

  /// 获取内建类型的调试信息。
  llvm::DIType *GetBuiltinDIType(const BuiltinType *BT);

  /// 获取指针类型的调试信息。
  llvm::DIDerivedType *GetPointerDIType(const PointerType *PT);

  /// 获取引用类型的调试信息。
  llvm::DIDerivedType *GetReferenceDIType(const ReferenceType *RT);

  /// 获取数组类型的调试信息。
  llvm::DIType *GetArrayDIType(const ArrayType *AT);

  /// 获取记录类型的调试信息。
  llvm::DIType *GetRecordDIType(const RecordType *RT);

  /// 获取枚举类型的调试信息。
  llvm::DIType *GetEnumDIType(const EnumType *ET);

  /// 获取函数类型的调试信息。
  llvm::DISubroutineType *GetFunctionDIType(const FunctionType *FT);

  //===------------------------------------------------------------------===//
  // 函数/变量调试信息
  //===------------------------------------------------------------------===//

  /// 为函数生成调试信息。
  llvm::DISubprogram *GetFunctionDI(FunctionDecl *FD);

  /// 为全局变量生成调试信息。
  void EmitGlobalVarDI(VarDecl *VD, llvm::GlobalVariable *GV);

  /// 为局部变量生成调试信息。
  void EmitLocalVarDI(VarDecl *VD, llvm::AllocaInst *Alloca);

  /// 为函数参数生成调试信息。
  void EmitParamDI(ParmVarDecl *PVD, llvm::AllocaInst *Alloca, unsigned ArgNo);

  //===------------------------------------------------------------------===//
  // 行号信息
  //===------------------------------------------------------------------===//

  /// 设置当前的源位置（附加到下一条 IR 指令）。
  void setLocation(SourceLocation Loc);

  /// 为 LLVM 函数设置调试信息（子程序 + 行号）。
  void setFunctionLocation(llvm::Function *Fn, FunctionDecl *FD);

  //===------------------------------------------------------------------===//
  // 作用域信息
  //===------------------------------------------------------------------===//

  /// 创建词法块调试信息。
  llvm::DILexicalBlock *CreateLexicalBlock(SourceLocation Loc);

  /// 获取当前的作用域（如果正在生成函数，返回函数的 scope）。
  llvm::DIScope *getCurrentScope();

  /// 清除当前函数的 DISubprogram（在函数生成结束时调用）。
  void clearCurrentFnSP() { CurrentFnSP = nullptr; }

  //===------------------------------------------------------------------===//
  // 辅助方法
  //===------------------------------------------------------------------===//

  /// 将 SourceLocation 转换为 DILocation。
  llvm::DILocation *getSourceLocation(SourceLocation Loc);

  /// 获取类型的字节大小。
  uint64_t GetTypeSize(QualType T);

  /// 获取类型的对齐。
  uint32_t GetTypeAlign(QualType T);

  /// 获取类型的行号（用于类型定义位置）。
  unsigned getLineNumber(SourceLocation Loc);

  /// 获取类型的列号（用于类型定义位置）。
  unsigned getColumnNumber(SourceLocation Loc);
};

} // namespace blocktype
