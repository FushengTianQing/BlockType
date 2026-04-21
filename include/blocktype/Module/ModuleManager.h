//===--- ModuleManager.h - Module Manager --------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ModuleManager class which manages C++20 modules.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Basic/LLVM.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>
#include <string>

namespace blocktype {

class ASTContext;
class DiagnosticsEngine;
class ModuleDecl;
class ImportDecl;
class NamedDecl;

/// ModuleInfo - 模块元信息
struct ModuleInfo {
  llvm::StringRef Name;              // 模块名 (e.g., "std.core")
  llvm::StringRef PrimaryModule;     // 主模块名
  llvm::StringRef Partition;         // 分区名（如果有）
  std::string BMIPath;               // BMI文件路径
  bool IsExported;                   // 是否导出
  bool IsPartition;                  // 是否为分区
  bool IsGlobalFragment;             // 是否为全局模块片段
  bool IsPrivateFragment;            // 是否为私有模块片段

  // 依赖信息
  llvm::SmallVector<llvm::StringRef, 8> Imports;
  llvm::SmallVector<llvm::StringRef, 4> Partitions;

  // 导出符号（简化版：仅存储名称）
  llvm::SmallVector<llvm::StringRef, 16> ExportedSymbols;

  // AST节点（如果已加载）
  ModuleDecl *Decl = nullptr;
};

/// ModuleManager - 模块管理器
///
/// 负责模块加载、缓存和生命周期管理。
/// 支持延迟加载和模块搜索路径。
class ModuleManager {
  ASTContext &Context;
  DiagnosticsEngine &Diags;

  /// 已加载模块缓存: Name → ModuleInfo
  llvm::StringMap<std::unique_ptr<ModuleInfo>> LoadedModules;

  /// 模块名 → ModuleDecl* 映射
  llvm::StringMap<ModuleDecl *> ModuleDecls;

  /// 当前模块（正在编译的模块）
  ModuleDecl *CurrentModule = nullptr;

  /// 模块搜索路径
  llvm::SmallVector<std::string, 4> SearchPaths;

public:
  explicit ModuleManager(ASTContext &C, DiagnosticsEngine &D);

  //===------------------------------------------------------------------===//
  // 模块加载
  //===------------------------------------------------------------------===//

  /// 加载模块（从BMI文件）
  /// \param Name 模块名
  /// \return 模块信息，失败返回 nullptr
  ModuleInfo *loadModule(llvm::StringRef Name);

  /// 从源文件编译模块
  /// \param SourcePath 源文件路径
  /// \return 模块信息
  ModuleInfo *compileModule(llvm::StringRef SourcePath);

  /// 查找模块BMI文件
  /// \param Name 模块名
  /// \return BMI文件路径，未找到返回空
  std::string findModuleBMI(llvm::StringRef Name);

  //===------------------------------------------------------------------===//
  // 模块注册
  //===------------------------------------------------------------------===//

  /// 注册模块声明
  void registerModuleDecl(ModuleDecl *MD);

  /// 注册导入声明
  void registerImportDecl(ImportDecl *ID);

  /// 获取模块声明
  ModuleDecl *getModuleDecl(llvm::StringRef Name) const;

  //===------------------------------------------------------------------===//
  // 当前模块管理
  //===------------------------------------------------------------------===//

  /// 设置当前模块
  void setCurrentModule(ModuleDecl *MD);

  /// 获取当前模块
  ModuleDecl *getCurrentModule() const { return CurrentModule; }

  /// 检查是否在模块中
  bool isInModule() const { return CurrentModule != nullptr; }

  //===------------------------------------------------------------------===//
  // 模块查询
  //===------------------------------------------------------------------===//

  /// 检查模块是否已加载
  bool isModuleLoaded(llvm::StringRef Name) const;

  /// 获取已加载模块列表
  llvm::SmallVector<ModuleInfo *, 16> getLoadedModules() const;

  /// 获取模块依赖（递归）
  llvm::SmallVector<ModuleInfo *, 8> getModuleDependencies(llvm::StringRef Name);

  //===------------------------------------------------------------------===//
  // 搜索路径管理
  //===------------------------------------------------------------------===//

  /// 添加模块搜索路径
  void addSearchPath(llvm::StringRef Path);

  /// 设置模块搜索路径
  void setSearchPaths(llvm::ArrayRef<std::string> Paths);

  /// 获取搜索路径
  llvm::ArrayRef<std::string> getSearchPaths() const { return SearchPaths; }

  //===------------------------------------------------------------------===//
  // 导出符号管理
  //===------------------------------------------------------------------===//

  /// 标记符号为导出
  void markExported(NamedDecl *D);

  /// 检查符号是否导出
  bool isExported(NamedDecl *D) const;

  /// 获取模块的所有导出符号
  llvm::ArrayRef<llvm::StringRef> getExportedSymbols(llvm::StringRef ModuleName) const;
};

} // namespace blocktype
