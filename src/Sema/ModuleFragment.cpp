//===- ModuleFragment.cpp - Module Fragment Support -------------*- C++ -*-===//
//
// Part of the BlockType Project, under the BSD 3-Clause License.
// See the LICENSE file in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// This file implements global and private module fragment support.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Module/ModuleManager.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {

//===------------------------------------------------------------------===//
// 全局模块片段
//===------------------------------------------------------------------===//

/// 进入全局模块片段
void Sema::enterGlobalModuleFragment() {
  // 设置标志，表示正在解析全局模块片段
  InGlobalModuleFragment = true;
}

/// 退出全局模块片段
void Sema::exitGlobalModuleFragment() {
  InGlobalModuleFragment = false;
}

/// 添加声明到全局模块片段
void Sema::addToGlobalModuleFragment(Decl *D) {
  if (!D) {
    return;
  }

  // 获取当前模块
  ModuleDecl *CurrentMod = ModMgr->getCurrentModule();
  if (!CurrentMod) {
    return;
  }

  // 将声明添加到全局模块片段
  // 这些声明不会被导出，但可以在模块内部使用
  CurrentMod->addToGlobalFragment(D);

  // 标记声明为全局模块片段的一部分
  // TODO: 添加 setOwningModule 方法到 Decl
  // D->setOwningModule(CurrentMod);
}

/// 检查是否在全局模块片段中
bool Sema::isInGlobalModuleFragment() const {
  return InGlobalModuleFragment;
}

//===------------------------------------------------------------------===//
// 私有模块片段
//===------------------------------------------------------------------===//

/// 进入私有模块片段
void Sema::enterPrivateModuleFragment() {
  // 设置标志，表示正在解析私有模块片段
  InPrivateModuleFragment = true;
}

/// 退出私有模块片段
void Sema::exitPrivateModuleFragment() {
  InPrivateModuleFragment = false;
}

/// 添加声明到私有模块片段
void Sema::addToPrivateModuleFragment(Decl *D) {
  if (!D) {
    return;
  }

  // 获取当前模块
  ModuleDecl *CurrentMod = ModMgr->getCurrentModule();
  if (!CurrentMod) {
    return;
  }

  // 将声明添加到私有模块片段
  // 这些声明只在当前模块实现单元中可见
  CurrentMod->addToPrivateFragment(D);

  // 标记声明为私有模块片段的一部分
  // TODO: 添加 setOwningModule 方法到 Decl
  // D->setOwningModule(CurrentMod);
}

/// 检查是否在私有模块片段中
bool Sema::isInPrivateModuleFragment() const {
  return InPrivateModuleFragment;
}

//===------------------------------------------------------------------===//
// 模块片段验证
//===------------------------------------------------------------------===//

/// 验证全局模块片段
bool Sema::validateGlobalModuleFragment(ModuleDecl *MD) {
  if (!MD || !MD->isGlobalModuleFragment()) {
    return false;
  }

  // 全局模块片段必须出现在模块声明之前
  // 这在解析时已经检查过了

  // 全局模块片段中的声明不能导出
  llvm::SmallVector<Decl *, 8> GlobalDecls;
  MD->getGlobalFragmentDecls(GlobalDecls);

  for (Decl *D : GlobalDecls) {
    if (auto *ND = dyn_cast<NamedDecl>(D)) {
      if (ND->isExported()) {
        Diags.report(ND->getLocation(), DiagID::err_not_implemented,
                     "declaration in global module fragment cannot be exported");
        return false;
      }
    }
  }

  return true;
}

/// 验证私有模块片段
bool Sema::validatePrivateModuleFragment(ModuleDecl *MD) {
  if (!MD || !MD->isPrivateModuleFragment()) {
    return false;
  }

  // 私有模块片段必须出现在模块实现单元的末尾
  // 这在解析时已经检查过了

  // 私有模块片段中的声明不能导出
  llvm::SmallVector<Decl *, 8> PrivateDecls;
  MD->getPrivateFragmentDecls(PrivateDecls);

  for (Decl *D : PrivateDecls) {
    if (auto *ND = dyn_cast<NamedDecl>(D)) {
      if (ND->isExported()) {
        Diags.report(ND->getLocation(), DiagID::err_not_implemented,
                     "declaration in private module fragment cannot be exported");
        return false;
      }
    }
  }

  return true;
}

/// 检查声明是否在模块片段中
bool Sema::isDeclInModuleFragment(Decl *D) const {
  if (!D) {
    return false;
  }

  // TODO: 添加 isInGlobalModuleFragment 和 isInPrivateModuleFragment 方法到 Decl
  // 检查是否在全局模块片段中
  // if (D->isInGlobalModuleFragment()) {
  //   return true;
  // }

  // 检查是否在私有模块片段中
  // if (D->isInPrivateModuleFragment()) {
  //   return true;
  // }

  return false;
}

/// 获取声明所在的模块片段类型
ModuleFragmentKind Sema::getModuleFragmentKind(Decl *D) const {
  if (!D) {
    return ModuleFragmentKind::None;
  }

  // TODO: 添加 isInGlobalModuleFragment 和 isInPrivateModuleFragment 方法到 Decl
  // if (D->isInGlobalModuleFragment()) {
  //   return ModuleFragmentKind::Global;
  // }

  // if (D->isInPrivateModuleFragment()) {
  //   return ModuleFragmentKind::Private;
  // }

  return ModuleFragmentKind::None;
}

} // namespace blocktype
