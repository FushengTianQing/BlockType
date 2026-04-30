//===--- IRDebugInfoEmitter.cpp - IR Debug Info Emitter -----------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/IRDebugInfoEmitter.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRInstruction.h"

namespace blocktype {
namespace frontend {

IRDebugInfoEmitter::IRDebugInfoEmitter(ir::IRModule& M, SourceManager& S)
  : TheModule(M), SM(S) {}

void IRDebugInfoEmitter::emitCompileUnit(const TranslationUnitDecl* TU) {
  CU = std::make_unique<ir::DICompileUnit>();

  // 设置源文件路径（使用第一个文件）
  if (SM.getNumFiles() > 0) {
    const FileInfo* MainFile = SM.getFileInfo(1u);
    if (MainFile) {
      CU->setSourceFile(MainFile->getFilename().str());
    }
  }

  CU->setProducer("BlockType Compiler 1.0.0");
  CU->setLanguage(4);  // DW_LANG_C_plus_plus
}

ir::DISubprogram* IRDebugInfoEmitter::emitFunctionDebugInfo(const FunctionDecl* FD) {
  if (!FD) return nullptr;

  // 检查缓存
  auto It = SubprogramCache.find(FD);
  if (It != SubprogramCache.end()) {
    return It->second;
  }

  // 创建 DISubprogram（llvm::StringRef -> std::string -> ir::StringRef）
  auto* SP = new ir::DISubprogram(ir::StringRef(FD->getName().str()));

  // 设置编译单元
  if (CU) {
    SP->setUnit(CU.get());
  }

  // 缓存
  SubprogramCache[FD] = SP;

  return SP;
}

ir::DISubprogram* IRDebugInfoEmitter::emitCXXMethodDebugInfo(const CXXMethodDecl* MD) {
  return emitFunctionDebugInfo(MD);
}

ir::DIType* IRDebugInfoEmitter::emitTypeDebugInfo(QualType T) {
  // Stub: 后续 Phase 完善
  return nullptr;
}

ir::DIType* IRDebugInfoEmitter::emitRecordDebugInfo(const RecordDecl* RD) {
  // Stub: 后续 Phase 完善
  return nullptr;
}

ir::DIType* IRDebugInfoEmitter::emitEnumDebugInfo(const EnumDecl* ED) {
  // Stub: 后续 Phase 完善
  return nullptr;
}

ir::debug::IRInstructionDebugInfo IRDebugInfoEmitter::emitInstructionDebugInfo(const Stmt* S) {
  ir::debug::IRInstructionDebugInfo DI;

  if (!S) return DI;

  // 设置源码位置
  SourceLocation Loc = S->getLocation();
  ir::SourceLocation IRLoc = convertSourceLocation(Loc);
  DI.setLocation(IRLoc);

  return DI;
}

void IRDebugInfoEmitter::setInstructionDebugInfo(ir::IRInstruction* I, const Stmt* S) {
  if (!I || !S) return;

  auto DI = emitInstructionDebugInfo(S);
  I->setDebugInfo(DI);
}

void IRDebugInfoEmitter::setInstructionLocation(ir::IRInstruction* I, SourceLocation Loc) {
  if (!I) return;

  ir::SourceLocation IRLoc = convertSourceLocation(Loc);

  ir::debug::IRInstructionDebugInfo NewDI;
  NewDI.setLocation(IRLoc);
  I->setDebugInfo(NewDI);
}

void IRDebugInfoEmitter::markInlined(ir::IRInstruction* I, SourceLocation InlinedAt) {
  if (!I) return;

  const auto* DI = I->getDebugInfo();
  if (DI) {
    ir::debug::IRInstructionDebugInfo NewDI;
    NewDI.setLocation(DI->getLocation());
    NewDI.setInlined(true);
    NewDI.setInlinedAt(convertSourceLocation(InlinedAt));
    I->setDebugInfo(NewDI);
  }
}

ir::SourceLocation IRDebugInfoEmitter::convertSourceLocation(SourceLocation Loc) {
  ir::SourceLocation IRLoc;

  if (Loc.isInvalid()) {
    return IRLoc;
  }

  // 获取文件信息
  const FileInfo* FI = SM.getFileInfo(Loc);
  if (FI) {
    // llvm::StringRef -> std::string -> ir::StringRef
    IRLoc.Filename = ir::StringRef(FI->getFilename().str());

    // 获取行号和列号
    auto LineCol = SM.getLineAndColumn(Loc);
    IRLoc.Line = LineCol.first;
    IRLoc.Column = LineCol.second;
  }

  return IRLoc;
}

ir::DISubprogram* IRDebugInfoEmitter::getOrCreateSubprogram(const FunctionDecl* FD) {
  return emitFunctionDebugInfo(FD);
}

ir::DIType* IRDebugInfoEmitter::getOrCreateDIType(QualType T) {
  return emitTypeDebugInfo(T);
}

} // namespace frontend
} // namespace blocktype
