//===--- BMIReader.cpp - BMI File Reader Implementation ------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Module/BMIReader.h"
#include "blocktype/Module/ModuleManager.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstring>

namespace blocktype {

std::unique_ptr<ModuleInfo>
BMIReader::readModule(llvm::StringRef BMIPath) {
  // 读取文件
  auto BufferOrError = llvm::MemoryBuffer::getFile(BMIPath);
  if (!BufferOrError) {
    Diags.report(SourceLocation{}, DiagID::err_pp_file_not_found, BMIPath);
    return nullptr;
  }

  auto Buf = std::move(BufferOrError.get());
  Buffer = Buf.get();

  return readFromBuffer(Buffer);
}

std::unique_ptr<ModuleInfo>
BMIReader::readFromBuffer(llvm::MemoryBuffer *Buf) {
  Buffer = Buf;

  // 读取文件头
  BMIHeader H;
  if (!readHeader(H)) {
    return nullptr;
  }

  // 创建模块信息
  auto Info = std::make_unique<ModuleInfo>();

  // 读取模块元数据
  if (!readModuleMetadata(*Info, H)) {
    return nullptr;
  }

  // 读取依赖信息
  if (!readDependencies(*Info, H)) {
    return nullptr;
  }

  // 读取导出符号
  if (!readExportedSymbols(*Info, H)) {
    return nullptr;
  }

  return Info;
}

bool BMIReader::readHeader(BMIHeader &H) {
  // 检查文件大小
  if (Buffer->getBufferSize() < sizeof(BMIHeader)) {
    Diags.report(SourceLocation{}, DiagID::err_not_implemented, "BMI file too small");
    return false;
  }

  // 读取文件头
  std::memcpy(&H, Buffer->getBufferStart(), sizeof(H));

  // 验证魔数
  if (std::memcmp(H.Magic, BMI_MAGIC, sizeof(BMI_MAGIC)) != 0) {
    Diags.report(SourceLocation{}, DiagID::err_not_implemented, "invalid BMI magic number");
    return false;
  }

  // 验证版本
  if (H.Version > BMI_VERSION) {
    Diags.report(SourceLocation{}, DiagID::err_not_implemented, "unsupported BMI version");
    return false;
  }

  return true;
}

bool BMIReader::readModuleMetadata(ModuleInfo &Info, const BMIHeader &H) {
  // 读取模块名
  Info.Name = readString(H.ModuleNameOff, H.ModuleNameLen);
  if (Info.Name.empty()) {
    Diags.report(SourceLocation{}, DiagID::err_not_implemented, "empty module name in BMI");
    return false;
  }

  // 读取分区名
  if (H.PartitionLen > 0) {
    Info.Partition = readString(H.PartitionOff, H.PartitionLen);
    Info.IsPartition = true;
  }

  // 解析标志位
  Info.IsExported = H.Flags & static_cast<uint32_t>(BMIFlags::IsExported);
  Info.IsGlobalFragment =
      H.Flags & static_cast<uint32_t>(BMIFlags::IsGlobalFragment);
  Info.IsPrivateFragment =
      H.Flags & static_cast<uint32_t>(BMIFlags::IsPrivateFragment);

  return true;
}

bool BMIReader::readDependencies(ModuleInfo &Info, const BMIHeader &H) {
  // 读取导入记录
  const char *Ptr = Buffer->getBufferStart() + sizeof(BMIHeader);

  for (uint32_t I = 0; I < H.NumImports; ++I) {
    BMIImport Record;
    std::memcpy(&Record, Ptr, sizeof(Record));
    Ptr += sizeof(Record);

    llvm::StringRef ModuleName = readString(Record.ModuleNameOff,
                                              Record.ModuleNameLen);
    if (!ModuleName.empty()) {
      Info.Imports.push_back(ModuleName);
    }
  }

  return true;
}

bool BMIReader::readExportedSymbols(ModuleInfo &Info, const BMIHeader &H) {
  // 读取导出符号记录
  const char *Ptr = Buffer->getBufferStart() + sizeof(BMIHeader) +
                    H.NumImports * sizeof(BMIImport);

  for (uint32_t I = 0; I < H.NumExports; ++I) {
    BMISymbol Record;
    std::memcpy(&Record, Ptr, sizeof(Record));
    Ptr += sizeof(Record);

    llvm::StringRef SymbolName = readString(Record.NameOff, Record.NameLen);
    if (!SymbolName.empty()) {
      Info.ExportedSymbols.push_back(SymbolName);
    }
  }

  return true;
}

llvm::StringRef BMIReader::readString(uint32_t Offset, uint32_t Length) {
  if (Offset + Length > Buffer->getBufferSize()) {
    return "";
  }

  return llvm::StringRef(Buffer->getBufferStart() + Offset, Length);
}

} // namespace blocktype
