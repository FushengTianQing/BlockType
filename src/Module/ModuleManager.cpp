//===--- ModuleManager.cpp - Module Manager Implementation ---*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Module/ModuleManager.h"
#include "blocktype/Module/BMIReader.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"

namespace blocktype {

ModuleManager::ModuleManager(ASTContext &C, DiagnosticsEngine &D)
    : Context(C), Diags(D) {}

//===--------------------------------------------------------------------===//
// 模块加载
//===--------------------------------------------------------------------===//

ModuleInfo *ModuleManager::loadModule(llvm::StringRef Name) {
  // 检查缓存
  auto It = LoadedModules.find(Name);
  if (It != LoadedModules.end()) {
    return It->second.get();
  }

  // 查找 BMI 文件
  std::string BMIPath = findModuleBMI(Name);
  if (BMIPath.empty()) {
    Diags.report(SourceLocation{}, DiagID::err_pp_file_not_found, Name);
    return nullptr;
  }

  // 读取 BMI 文件
  BMIReader Reader(Context, Diags);
  auto Info = Reader.readModule(BMIPath);
  if (!Info) {
    return nullptr;
  }

  // 缓存模块
  ModuleInfo *Result = Info.get();
  LoadedModules[Name] = std::move(Info);
  return Result;
}

ModuleInfo *ModuleManager::compileModule(llvm::StringRef SourcePath) {
  // 检查源文件是否存在
  if (!llvm::sys::fs::exists(SourcePath)) {
    Diags.report(SourceLocation{}, DiagID::err_pp_file_not_found, SourcePath);
    return nullptr;
  }

  // 构建输出 BMI 文件路径
  llvm::SmallString<256> BMIPath(SourcePath);
  llvm::sys::path::replace_extension(BMIPath, ".bmi");

  // 构建编译命令
  // 使用 blocktype 编译器编译模块
  llvm::SmallVector<llvm::StringRef, 8> Args;
  Args.push_back("blocktype");
  Args.push_back("-emit-bmi");
  Args.push_back("-o");
  Args.push_back(BMIPath);
  Args.push_back(SourcePath);

  // 执行编译
  std::string ErrorMessage;
  int ExitCode = llvm::sys::ExecuteAndWait(
      "blocktype", Args, std::nullopt, {}, 0, 0, &ErrorMessage);

  if (ExitCode != 0) {
    if (!ErrorMessage.empty()) {
      Diags.report(SourceLocation{}, DiagID::err_not_implemented,
                   "module compilation failed: " + ErrorMessage);
    } else {
      Diags.report(SourceLocation{}, DiagID::err_not_implemented,
                   "module compilation failed with exit code " +
                       std::to_string(ExitCode));
    }
    return nullptr;
  }

  // 加载生成的 BMI 文件
  return loadModule(llvm::sys::path::stem(BMIPath).str());
}

std::string ModuleManager::findModuleBMI(llvm::StringRef Name) {
  // 在搜索路径中查找 BMI 文件
  for (const auto &Path : SearchPaths) {
    llvm::SmallString<256> BMIPath(Path);
    llvm::sys::path::append(BMIPath, Name + ".bmi");

    if (llvm::sys::fs::exists(BMIPath)) {
      return BMIPath.str().str();
    }
  }

  // 未找到
  return "";
}

//===--------------------------------------------------------------------===//
// 模块注册
//===--------------------------------------------------------------------===//

void ModuleManager::registerModuleDecl(ModuleDecl *MD) {
  if (!MD)
    return;

  llvm::StringRef Name = MD->getModuleName();
  ModuleDecls[Name] = MD;

  // 创建模块信息
  auto Info = std::make_unique<ModuleInfo>();
  Info->Name = Name;
  Info->Decl = MD;
  Info->IsExported = MD->isExported();
  Info->IsPartition = MD->isModulePartition();
  Info->IsGlobalFragment = MD->isGlobalModuleFragment();
  Info->IsPrivateFragment = MD->isPrivateModuleFragment();

  if (!MD->getPartitionName().empty()) {
    Info->Partition = MD->getPartitionName().str();
  }

  LoadedModules[Name] = std::move(Info);
}

void ModuleManager::registerImportDecl(ImportDecl *ID) {
  if (!ID || !CurrentModule)
    return;

  // 将导入添加到当前模块的依赖列表
  auto It = LoadedModules.find(CurrentModule->getModuleName());
  if (It != LoadedModules.end()) {
    It->second->Imports.push_back(ID->getModuleName().str());
  }
}

ModuleDecl *ModuleManager::getModuleDecl(llvm::StringRef Name) const {
  auto It = ModuleDecls.find(Name);
  return It != ModuleDecls.end() ? It->second : nullptr;
}

//===--------------------------------------------------------------------===//
// 当前模块管理
//===--------------------------------------------------------------------===//

void ModuleManager::setCurrentModule(ModuleDecl *MD) {
  CurrentModule = MD;

  // 确保模块已注册
  if (MD && !isModuleLoaded(MD->getModuleName())) {
    registerModuleDecl(MD);
  }
}

//===--------------------------------------------------------------------===//
// 模块查询
//===--------------------------------------------------------------------===//

bool ModuleManager::isModuleLoaded(llvm::StringRef Name) const {
  return LoadedModules.find(Name) != LoadedModules.end();
}

llvm::SmallVector<ModuleInfo *, 16> ModuleManager::getLoadedModules() const {
  llvm::SmallVector<ModuleInfo *, 16> Result;
  for (const auto &Pair : LoadedModules) {
    Result.push_back(Pair.second.get());
  }
  return Result;
}

llvm::SmallVector<ModuleInfo *, 8>
ModuleManager::getModuleDependencies(llvm::StringRef Name) {
  llvm::SmallVector<ModuleInfo *, 8> Result;

  auto It = LoadedModules.find(Name);
  if (It == LoadedModules.end()) {
    return Result;
  }

  ModuleInfo *Info = It->second.get();

  // 递归收集依赖
  llvm::SmallVector<std::string, 16> WorkList;
  llvm::SmallVector<std::string, 16> Visited;

  WorkList.append(Info->Imports.begin(), Info->Imports.end());

  while (!WorkList.empty()) {
    std::string DepName = WorkList.pop_back_val();

    // 检查是否已访问
    if (std::find(Visited.begin(), Visited.end(), DepName) != Visited.end()) {
      continue;
    }
    Visited.push_back(DepName);

    // 获取依赖模块信息
    auto DepIt = LoadedModules.find(DepName);
    if (DepIt != LoadedModules.end()) {
      Result.push_back(DepIt->second.get());

      // 添加依赖的依赖
      for (const std::string &TransitiveDep : DepIt->second->Imports) {
        if (std::find(Visited.begin(), Visited.end(), TransitiveDep) ==
            Visited.end()) {
          WorkList.push_back(TransitiveDep);
        }
      }
    }
  }

  return Result;
}

ModuleInfo *ModuleManager::getModuleInfo(llvm::StringRef Name) const {
  auto It = LoadedModules.find(Name);
  return It != LoadedModules.end() ? It->second.get() : nullptr;
}

void ModuleManager::registerModuleInfo(ModuleInfo *Info) {
  if (!Info || Info->Name.empty()) {
    return;
  }

  // 检查是否已存在
  if (LoadedModules.find(Info->Name) != LoadedModules.end()) {
    return;
  }

  // 添加到缓存
  LoadedModules[Info->Name] = std::unique_ptr<ModuleInfo>(Info);
}

ModuleInfo *ModuleManager::loadModulePartition(ModuleDecl *MainModule,
                                               llvm::StringRef PartitionName) {
  if (!MainModule) {
    return nullptr;
  }

  // 构建分区名
  std::string FullPartitionName =
      MainModule->getModuleName().str() + ":" + PartitionName.str();

  // 检查是否已加载
  auto It = LoadedModules.find(FullPartitionName);
  if (It != LoadedModules.end()) {
    return It->second.get();
  }

  // 查找分区 BMI 文件
  std::string BMIPath = findModuleBMI(FullPartitionName);
  if (BMIPath.empty()) {
    return nullptr;
  }

  // 加载分区
  return loadModule(FullPartitionName);
}

//===--------------------------------------------------------------------===//
// 搜索路径管理
//===--------------------------------------------------------------------===//

void ModuleManager::addSearchPath(llvm::StringRef Path) {
  SearchPaths.push_back(Path.str());
}

void ModuleManager::setSearchPaths(llvm::ArrayRef<std::string> Paths) {
  SearchPaths.clear();
  for (const auto &Path : Paths) {
    SearchPaths.push_back(Path);
  }
}

//===--------------------------------------------------------------------===//
// 导出符号管理
//===--------------------------------------------------------------------===//

void ModuleManager::markExported(NamedDecl *D) {
  if (!D || !CurrentModule)
    return;

  auto It = LoadedModules.find(CurrentModule->getModuleName());
  if (It != LoadedModules.end()) {
    It->second->ExportedSymbols.push_back(D->getName().str());
  }
}

bool ModuleManager::isExported(NamedDecl *D) const {
  if (!D)
    return false;

  // 查找符号所属模块
  // TODO: 实现完整的符号到模块映射
  return false;
}

llvm::ArrayRef<std::string>
ModuleManager::getExportedSymbols(llvm::StringRef ModuleName) const {
  auto It = LoadedModules.find(ModuleName);
  if (It != LoadedModules.end()) {
    return It->second->ExportedSymbols;
  }
  return {};
}

} // namespace blocktype
