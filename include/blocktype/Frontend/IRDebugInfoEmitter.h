#ifndef BLOCKTYPE_FRONTEND_IRDEBUGINFOEMITTER_H
#define BLOCKTYPE_FRONTEND_IRDEBUGINFOEMITTER_H

#include "blocktype/IR/IRDebugInfo.h"
#include "blocktype/IR/IRDebugMetadata.h"
#include "blocktype/IR/IRInstruction.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/IR/ADT.h"
#include <memory>

namespace blocktype {
namespace frontend {

/// IR 调试信息发射器
/// 负责将 AST 调试信息转换为 IR 调试元数据
class IRDebugInfoEmitter {
  ir::IRModule& TheModule;
  SourceManager& SM;

  // 调试元数据缓存
  ir::DenseMap<const Decl*, ir::DISubprogram*> SubprogramCache;
  ir::DenseMap<const Type*, ir::DIType*> TypeCache;
  std::unique_ptr<ir::DICompileUnit> CU;

public:
  IRDebugInfoEmitter(ir::IRModule& M, SourceManager& S);

  // === 编译单元 ===

  /// 创建编译单元调试信息
  void emitCompileUnit(const TranslationUnitDecl* TU);

  // === 函数 ===

  /// 为函数创建调试信息
  ir::DISubprogram* emitFunctionDebugInfo(const FunctionDecl* FD);

  /// 为 C++ 方法创建调试信息
  ir::DISubprogram* emitCXXMethodDebugInfo(const CXXMethodDecl* MD);

  // === 类型 ===

  /// 为类型创建调试信息（stub 实现）
  ir::DIType* emitTypeDebugInfo(QualType T);

  /// 为 Record 类型创建调试信息（stub 实现）
  ir::DIType* emitRecordDebugInfo(const RecordDecl* RD);

  /// 为 Enum 类型创建调试信息（stub 实现）
  ir::DIType* emitEnumDebugInfo(const EnumDecl* ED);

  // === 指令级调试信息 ===

  /// 为指令创建调试信息（从 Stmt）
  ir::debug::IRInstructionDebugInfo emitInstructionDebugInfo(const Stmt* S);

  /// 设置指令的调试信息（从 Stmt）
  void setInstructionDebugInfo(ir::IRInstruction* I, const Stmt* S);

  /// 设置指令的源码位置（从 SourceLocation）
  void setInstructionLocation(ir::IRInstruction* I, SourceLocation Loc);

  // === 内联信息 ===

  /// 标记指令为内联
  void markInlined(ir::IRInstruction* I, SourceLocation InlinedAt);

  // === 辅助方法 ===

  /// 将 BlockType SourceLocation 转换为 IR SourceLocation
  ir::SourceLocation convertSourceLocation(SourceLocation Loc);

  /// 获取或创建 DISubprogram
  ir::DISubprogram* getOrCreateSubprogram(const FunctionDecl* FD);

  /// 获取或创建 DIType
  ir::DIType* getOrCreateDIType(QualType T);
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_IRDEBUGINFOEMITTER_H
