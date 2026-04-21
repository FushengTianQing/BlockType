//===--- BMIWriter.cpp - BMI File Writer Implementation ------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Module/BMIWriter.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>

namespace blocktype {

bool BMIWriter::writeModule(ModuleDecl *MD, llvm::StringRef OutputPath) {
  if (!MD) {
    return false;
  }

  // 打开输出文件
  std::error_code EC;
  llvm::raw_fd_ostream Out(OutputPath, EC, llvm::sys::fs::OF_None);
  if (EC) {
    Diags.report(SourceLocation{}, DiagID::err_pp_file_not_found, OutputPath);
    return false;
  }

  OS = &Out;

  // 准备文件头
  BMIHeader H;
  std::memset(&H, 0, sizeof(H));
  std::memcpy(H.Magic, BMI_MAGIC, sizeof(BMI_MAGIC));
  H.Version = BMI_VERSION;

  // 写入模块元数据
  writeModuleMetadata(MD, H);

  // 写入依赖信息
  writeDependencies(MD, H);

  // 写入导出符号
  writeExportedSymbols(MD, H);

  // 写入文件头（在文件开头）
  Out.seek(0);
  writeHeader(H);

  return true;
}

void BMIWriter::writeHeader(const BMIHeader &H) {
  OS->write(reinterpret_cast<const char *>(&H), sizeof(H));
}

void BMIWriter::writeModuleMetadata(ModuleDecl *MD, BMIHeader &H) {
  // 写入模块名
  llvm::StringRef ModuleName = MD->getModuleName();
  H.ModuleNameOff = writeString(ModuleName);
  H.ModuleNameLen = ModuleName.size();

  // 写入分区名
  llvm::StringRef Partition = MD->getPartitionName();
  if (!Partition.empty()) {
    H.PartitionOff = writeString(Partition);
    H.PartitionLen = Partition.size();
  }

  // 设置标志位
  H.Flags = static_cast<uint32_t>(BMIFlags::None);
  if (MD->isExported()) {
    H.Flags |= static_cast<uint32_t>(BMIFlags::IsExported);
  }
  if (MD->isModulePartition()) {
    H.Flags |= static_cast<uint32_t>(BMIFlags::IsPartition);
  }
  if (MD->isGlobalModuleFragment()) {
    H.Flags |= static_cast<uint32_t>(BMIFlags::IsGlobalFragment);
  }
  if (MD->isPrivateModuleFragment()) {
    H.Flags |= static_cast<uint32_t>(BMIFlags::IsPrivateFragment);
  }
}

void BMIWriter::writeDependencies(ModuleDecl *MD, BMIHeader &H) {
  // TODO: 收集导入声明
  // 需要遍历 ModuleDecl 的子节点找到所有 ImportDecl
  H.NumImports = 0;

  // TODO: 写入每个导入
  // for (ImportDecl *Import : Imports) {
  //   BMIImport Record;
  //   Record.ModuleNameOff = writeString(Import->getModuleName());
  //   Record.ModuleNameLen = Import->getModuleName().size();
  //   Record.IsExported = Import->isExported();
  //   OS->write(reinterpret_cast<const char*>(&Record), sizeof(Record));
  // }
}

void BMIWriter::writeExportedSymbols(ModuleDecl *MD, BMIHeader &H) {
  // 收集导出符号
  auto ExportedDecls = collectExportedDecls(MD);
  H.NumExports = ExportedDecls.size();

  // 写入每个导出符号
  for (NamedDecl *D : ExportedDecls) {
    BMISymbol Record;
    Record.NameOff = writeString(D->getName());
    Record.NameLen = D->getName().size();
    Record.Kind = static_cast<uint32_t>(D->getKind());
    Record.TypeOff = 0; // TODO: 类型信息
    Record.TypeLen = 0;
    OS->write(reinterpret_cast<const char *>(&Record), sizeof(Record));
  }
}

uint32_t BMIWriter::writeString(llvm::StringRef Str) {
  uint32_t Offset = OS->tell();
  OS->write(Str.data(), Str.size());
  OS->write('\0'); // null 终止符
  return Offset;
}

llvm::SmallVector<NamedDecl *, 16>
BMIWriter::collectExportedDecls(ModuleDecl *MD) {
  llvm::SmallVector<NamedDecl *, 16> Result;

  // TODO: 遍历模块的导出声明
  // 需要遍历 ModuleDecl 的子节点找到所有 ExportDecl
  // 然后提取被导出的声明

  return Result;
}

} // namespace blocktype
