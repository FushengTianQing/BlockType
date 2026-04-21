//===--- BMIFormat.h - Binary Module Interface Format --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the BMI (Binary Module Interface) file format.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace blocktype {

/// BMI 魔数
constexpr char BMI_MAGIC[] = "BTBMI"; // BlockType Binary Module Interface
constexpr uint32_t BMI_VERSION = 1;

/// BMI 文件头（固定 64 字节）
struct BMIHeader {
  char Magic[6];           // "BTBMI\0"
  uint32_t Version;        // 版本号
  uint32_t ModuleNameOff;  // 模块名偏移
  uint32_t ModuleNameLen;  // 模块名长度
  uint32_t PartitionOff;   // 分区名偏移
  uint32_t PartitionLen;   // 分区名长度
  uint32_t Flags;          // 标志位
  uint32_t NumImports;     // 导入模块数
  uint32_t NumExports;     // 导出符号数
  uint32_t ASTSectionOff;  // AST段偏移
  uint32_t ASTSectionSize; // AST段大小
  uint8_t Reserved[16];    // 保留字段（填充至64字节）
};

static_assert(sizeof(BMIHeader) == 64, "BMIHeader must be 64 bytes");

/// BMI 标志位
enum class BMIFlags : uint32_t {
  None = 0,
  IsExported = 1 << 0,
  IsPartition = 1 << 1,
  IsGlobalFragment = 1 << 2,
  IsPrivateFragment = 1 << 3,
};

/// BMI 符号记录
struct BMISymbol {
  uint32_t NameOff;   // 符号名偏移
  uint32_t NameLen;   // 符号名长度
  uint32_t Kind;      // 符号类型
  uint32_t TypeOff;   // 类型信息偏移
  uint32_t TypeLen;   // 类型信息长度
};

/// BMI 导入记录
struct BMIImport {
  uint32_t ModuleNameOff; // 模块名偏移
  uint32_t ModuleNameLen; // 模块名长度
  uint32_t IsExported;    // 是否导出导入
};

} // namespace blocktype
