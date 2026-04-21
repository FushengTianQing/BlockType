//===- ModulePartition.cpp - Module Partition Support -----------*- C++ -*-===//
//
// Part of the BlockType Project, under the BSD 3-Clause License.
// See the LICENSE file in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// This file implements C++20 module partition support.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Module/ModuleManager.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"

namespace blocktype {

//===------------------------------------------------------------------===//
// 分区导入处理
//===------------------------------------------------------------------===//

/// 处理分区导入
///
/// 分区导入语法：import :PartitionName;
/// 只能在模块实现单元中使用
DeclResult Sema::ActOnPartitionImport(SourceLocation Loc,
                                       llvm::StringRef PartitionName) {
  // 1. 检查是否在模块中
  if (!ModMgr->isInModule()) {
    Diags.report(Loc, DiagID::err_not_implemented,
                 "partition import outside of module");
    return DeclResult::getInvalid();
  }

  // 2. 获取当前模块
  ModuleDecl *CurrentMod = ModMgr->getCurrentModule();
  if (!CurrentMod) {
    Diags.report(Loc, DiagID::err_not_implemented, "no current module");
    return DeclResult::getInvalid();
  }

  // 3. 检查分区是否属于当前模块
  if (!CurrentMod->hasPartition(PartitionName)) {
    Diags.report(Loc, DiagID::err_not_implemented,
                 "partition not found in current module");
    return DeclResult::getInvalid();
  }

  // 4. 加载分区
  ModuleInfo *Partition =
      ModMgr->loadModulePartition(CurrentMod, PartitionName);
  if (!Partition) {
    Diags.report(Loc, DiagID::err_not_implemented, "failed to load partition");
    return DeclResult::getInvalid();
  }

  // 5. 合并分区符号到当前模块
  if (!mergePartitionSymbols(CurrentMod, Partition)) {
    Diags.report(Loc, DiagID::err_not_implemented,
                 "failed to merge partition symbols");
    return DeclResult::getInvalid();
  }

  // 6. 创建 ImportDecl
  // TODO: 添加 ImportDecl::Create 方法
  // ImportDecl *ID = ImportDecl::Create(Context, CurrentMod, Loc,
  //                                     PartitionName, false, true);
  // if (ID) {
  //   ID->setIsPartitionImport(true);
  // }

  return DeclResult(/*success*/);
}

/// 合并分区符号到主模块
bool Sema::mergePartitionSymbols(ModuleDecl *MainModule,
                                  ModuleInfo *Partition) {
  if (!MainModule || !Partition) {
    return false;
  }

  // 1. 获取主模块信息
  ModuleInfo *MainInfo = ModMgr->getModuleInfo(MainModule->getModuleName());
  if (!MainInfo) {
    return false;
  }

  // 2. 合并导出符号
  for (const std::string &Exported : Partition->ExportedSymbols) {
    // 检查符号是否已经在主模块中
    bool Found = false;
    for (const std::string &MainExported : MainInfo->ExportedSymbols) {
      if (MainExported == Exported) {
        Found = true;
        break;
      }
    }

    // 如果不在，添加到主模块的导出符号列表
    if (!Found) {
      MainInfo->ExportedSymbols.push_back(Exported);
    }
  }

  // 3. 合并导入依赖
  for (const std::string &Import : Partition->Imports) {
    // 检查是否已经在主模块的导入列表中
    bool Found = false;
    for (const std::string &MainImport : MainInfo->Imports) {
      if (MainImport == Import) {
        Found = true;
        break;
      }
    }

    // 如果不在，添加到主模块的导入列表
    if (!Found) {
      MainInfo->Imports.push_back(Import);
    }
  }

  return true;
}

//===------------------------------------------------------------------===//
// 分区管理
//===------------------------------------------------------------------===//

/// 注册模块分区
void Sema::registerModulePartition(ModuleDecl *PartitionDecl) {
  if (!PartitionDecl || !PartitionDecl->isModulePartition()) {
    return;
  }

  // 1. 获取主模块名
  llvm::StringRef MainModuleName = PartitionDecl->getModuleName();
  llvm::StringRef PartitionName = PartitionDecl->getPartitionName();

  // 2. 查找或创建主模块
  ModuleDecl *MainModule = ModMgr->getModuleDecl(MainModuleName);
  if (!MainModule) {
    // 主模块尚未声明，创建占位符
    // TODO: 这可能需要错误处理
    return;
  }

  // 3. 将分区添加到主模块
  MainModule->addPartition(PartitionDecl);

  // 4. 创建分区信息
  std::string FullPartitionName =
      MainModuleName.str() + ":" + PartitionName.str();
  ModuleInfo *PartitionInfo = new ModuleInfo();
  PartitionInfo->Name = FullPartitionName;
  PartitionInfo->IsPartition = true;
  PartitionInfo->PrimaryModule = MainModuleName.str();

  // 注册到 ModuleManager
  ModMgr->registerModuleInfo(PartitionInfo);
}

/// 检查分区是否有效
bool Sema::validatePartition(ModuleDecl *PartitionDecl) {
  if (!PartitionDecl || !PartitionDecl->isModulePartition()) {
    return false;
  }

  // 1. 检查分区名是否有效
  llvm::StringRef PartitionName = PartitionDecl->getPartitionName();
  if (PartitionName.empty()) {
    Diags.report(PartitionDecl->getLocation(), DiagID::err_not_implemented,
                 "empty partition name");
    return false;
  }

  // 2. 检查主模块是否存在
  llvm::StringRef MainModuleName = PartitionDecl->getModuleName();
  ModuleDecl *MainModule = ModMgr->getModuleDecl(MainModuleName);

  if (!MainModule) {
    // 主模块尚未声明，允许（可能是分区先声明）
    return true;
  }

  // 3. 检查分区是否已经存在
  if (MainModule->hasPartition(PartitionName)) {
    // 检查是否为同一分区
    ModuleDecl *ExistingPartition =
        MainModule->getPartition(PartitionName);
    if (ExistingPartition != PartitionDecl) {
      Diags.report(PartitionDecl->getLocation(), DiagID::err_not_implemented,
                   "partition already defined");
      return false;
    }
  }

  return true;
}

/// 获取模块的所有分区
llvm::SmallVector<ModuleDecl *, 8>
Sema::getModulePartitions(ModuleDecl *Module) {
  llvm::SmallVector<ModuleDecl *, 8> Partitions;

  if (!Module) {
    return Partitions;
  }

  // 从 ModuleManager 获取分区信息
  // TODO: 需要在 ModuleManager 中实现 getModuleInfo 方法
  // ModuleInfo *Info = ModMgr->getModuleInfo(Module->getModuleName());
  // if (!Info) {
  //   return Partitions;
  // }

  // 暂时返回空列表
  // TODO: 实现分区查找逻辑
  return Partitions;
}

/// 检查分区接口单元是否完整
bool Sema::checkPartitionInterfaceComplete(ModuleDecl *PartitionDecl) {
  if (!PartitionDecl || !PartitionDecl->isModulePartition()) {
    return false;
  }

  // 分区接口单元必须导出
  if (!PartitionDecl->isExported()) {
    Diags.report(PartitionDecl->getLocation(), DiagID::err_not_implemented,
                 "partition interface must be exported");
    return false;
  }

  // 检查分区是否有导出符号
  // TODO: 需要在 ModuleManager 中实现 getModuleInfo 方法
  // ModuleInfo *Info = ModMgr->getModuleInfo(
  //     PartitionDecl->getModuleName().str() + ":" +
  //     PartitionDecl->getPartitionName().str());

  // if (!Info || Info->ExportedSymbols.empty()) {
  //   Diags.report(PartitionDecl->getLocation(), DiagID::err_not_implemented,
  //                "partition interface has no exported symbols");
  //   return false;
  // }

  return true;
}

} // namespace blocktype
