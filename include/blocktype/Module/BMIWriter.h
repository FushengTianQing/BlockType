//===--- BMIWriter.h - BMI File Writer -----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the BMIWriter class for writing Binary Module Interface.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Module/BMIFormat.h"
#include "blocktype/Basic/LLVM.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

namespace blocktype {

class ASTContext;
class DiagnosticsEngine;
class ModuleDecl;
class NamedDecl;

/// BMIWriter - BMI 文件写入器
///
/// 将模块序列化到 BMI 文件。
class BMIWriter {
  ASTContext &Context;
  DiagnosticsEngine &Diags;
  llvm::raw_fd_ostream *OS = nullptr;

  /// 字符串表（简化版：直接写入）
  llvm::SmallVector<std::string, 32> StringTable;

public:
  explicit BMIWriter(ASTContext &C, DiagnosticsEngine &D)
      : Context(C), Diags(D) {}

  /// 将模块序列化到 BMI 文件
  /// \param MD 模块声明
  /// \param OutputPath 输出文件路径
  /// \return 成功返回 true
  bool writeModule(ModuleDecl *MD, llvm::StringRef OutputPath);

private:
  /// 写入文件头
  void writeHeader(const BMIHeader &H);

  /// 写入模块元数据
  void writeModuleMetadata(ModuleDecl *MD, BMIHeader &H);

  /// 写入依赖信息
  void writeDependencies(ModuleDecl *MD, BMIHeader &H);

  /// 写入导出符号
  void writeExportedSymbols(ModuleDecl *MD, BMIHeader &H);

  /// 写入字符串并返回偏移
  uint32_t writeString(llvm::StringRef Str);

  /// 收集导出符号
  llvm::SmallVector<NamedDecl *, 16> collectExportedDecls(ModuleDecl *MD);
};

} // namespace blocktype
