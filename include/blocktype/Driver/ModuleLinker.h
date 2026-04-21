//===- ModuleLinker.h - Module Linking Interface ---------------*- C++ -*-===//
//
// Part of the BlockType Project, under the BSD 3-Clause License.
// See the LICENSE file in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// This file defines the ModuleLinker interface, which handles linking of
// compiled modules into executables or libraries.
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_DRIVER_MODULELINKER_H
#define BLOCKTYPE_DRIVER_MODULELINKER_H

#include "blocktype/Module/ModuleManager.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace blocktype {

class DiagnosticsEngine;

/// ModuleLinker - 负责链接编译后的模块
///
/// 链接策略：
/// - 每个模块编译为一个目标文件（.o）
/// - 按依赖顺序链接目标文件
/// - 支持生成可执行文件或库文件
class ModuleLinker {
  ModuleManager &ModMgr;
  DiagnosticsEngine &Diags;

  /// 目标文件路径映射（模块名 -> 目标文件路径）
  llvm::StringMap<std::string> ObjectFiles;

  /// 链接器路径（默认使用系统链接器）
  std::string LinkerPath;

public:
  explicit ModuleLinker(ModuleManager &MM, DiagnosticsEngine &D);

  //===------------------------------------------------------------------===//
  // 模块链接
  //===------------------------------------------------------------------===//

  /// 链接模块
  /// \param ModuleNames 要链接的模块名列表
  /// \param OutputPath 输出文件路径
  /// \return 成功返回 true
  bool linkModules(llvm::ArrayRef<llvm::StringRef> ModuleNames,
                   llvm::StringRef OutputPath);

  /// 链接单个模块（及其依赖）
  /// \param ModuleName 模块名
  /// \param OutputPath 输出文件路径
  /// \return 成功返回 true
  bool linkModuleWithDependencies(llvm::StringRef ModuleName,
                                  llvm::StringRef OutputPath);

  //===------------------------------------------------------------------===//
  // 目标文件管理
  //===------------------------------------------------------------------===//

  /// 注册模块的目标文件
  /// \param ModuleName 模块名
  /// \param ObjectPath 目标文件路径
  void registerObjectFile(llvm::StringRef ModuleName,
                          llvm::StringRef ObjectPath);

  /// 获取模块的目标文件路径
  /// \param ModuleName 模块名
  /// \return 目标文件路径，未注册返回空
  std::string getObjectFile(llvm::StringRef ModuleName) const;

  /// 检查模块是否有目标文件
  bool hasObjectFile(llvm::StringRef ModuleName) const;

  //===------------------------------------------------------------------===//
  // 链接器配置
  //===------------------------------------------------------------------===//

  /// 设置链接器路径
  void setLinkerPath(llvm::StringRef Path) { LinkerPath = Path.str(); }

  /// 获取链接器路径
  llvm::StringRef getLinkerPath() const { return LinkerPath; }

  /// 设置链接标志
  void setLinkerFlags(llvm::ArrayRef<std::string> Flags);

  /// 获取链接标志
  llvm::ArrayRef<std::string> getLinkerFlags() const { return LinkerFlags; }

  //===------------------------------------------------------------------===//
  // 输出类型
  //===------------------------------------------------------------------===//

  /// 输出类型枚举
  enum class OutputType {
    Executable,  ///< 可执行文件
    SharedLibrary, ///< 共享库
    StaticLibrary  ///< 静态库
  };

  /// 设置输出类型
  void setOutputType(OutputType Type) { OutType = Type; }

  /// 获取输出类型
  OutputType getOutputType() const { return OutType; }

private:
  //===------------------------------------------------------------------===//
  // 内部实现
  //===------------------------------------------------------------------===//

  /// 收集模块及其依赖的目标文件
  /// \param ModuleNames 模块名列表
  /// \return 目标文件路径列表（按链接顺序）
  llvm::SmallVector<std::string, 16>
  collectObjectFiles(llvm::ArrayRef<llvm::StringRef> ModuleNames);

  /// 调用系统链接器
  /// \param ObjectFiles 目标文件列表
  /// \param OutputPath 输出文件路径
  /// \return 成功返回 true
  bool invokeLinker(llvm::ArrayRef<std::string> ObjectFiles,
                    llvm::StringRef OutputPath);

  /// 构建链接器命令行
  /// \param ObjectFiles 目标文件列表
  /// \param OutputPath 输出文件路径
  /// \return 完整的链接器命令行
  /// 链接标志
  llvm::SmallVector<std::string, 8> LinkerFlags;

  /// 输出类型
  OutputType OutType = OutputType::Executable;
};

} // namespace blocktype

#endif // BLOCKTYPE_DRIVER_MODULELINKER_H
