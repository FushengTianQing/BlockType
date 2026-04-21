//===--- BMIReader.h - BMI File Reader -----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the BMIReader class for reading Binary Module Interface.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Module/BMIFormat.h"
#include "blocktype/Basic/LLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace blocktype {

class ASTContext;
class DiagnosticsEngine;
struct ModuleInfo;

/// BMIReader - BMI 文件读取器
///
/// 从 BMI 文件加载模块信息。
class BMIReader {
  ASTContext &Context;
  DiagnosticsEngine &Diags;
  llvm::MemoryBuffer *Buffer = nullptr;

public:
  explicit BMIReader(ASTContext &C, DiagnosticsEngine &D)
      : Context(C), Diags(D) {}

  /// 从 BMI 文件加载模块
  /// \param BMIPath BMI 文件路径
  /// \return 模块信息，失败返回 nullptr
  std::unique_ptr<ModuleInfo> readModule(llvm::StringRef BMIPath);

  /// 从内存缓冲区加载模块
  /// \param Buf 内存缓冲区
  /// \return 模块信息，失败返回 nullptr
  std::unique_ptr<ModuleInfo> readFromBuffer(llvm::MemoryBuffer *Buf);

private:
  /// 读取并验证文件头
  bool readHeader(BMIHeader &H);

  /// 读取模块元数据
  bool readModuleMetadata(ModuleInfo &Info, const BMIHeader &H);

  /// 读取依赖信息
  bool readDependencies(ModuleInfo &Info, const BMIHeader &H);

  /// 读取导出符号
  bool readExportedSymbols(ModuleInfo &Info, const BMIHeader &H);

  /// 读取字符串（从指定偏移）
  llvm::StringRef readString(uint32_t Offset, uint32_t Length);
};

} // namespace blocktype
