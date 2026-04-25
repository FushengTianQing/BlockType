#ifndef BLOCKTYPE_IR_IRDEBUGFWD_H
#define BLOCKTYPE_IR_IRDEBUGFWD_H

/// @file IRDebugFwd.h
/// 调试信息前向声明。
/// 如需完整类型定义，请直接 include IRDebugInfo.h 或 IRDebugMetadata.h。
/// 此文件仅提供前向声明，用于不需要完整类型的场景（如指针声明）。

namespace blocktype {
namespace ir {

// 基础调试信息前向声明（完整定义在 IRDebugMetadata.h）
class DebugMetadata;
class DICompileUnit;
class DIType;
class DISubprogram;
class DILocation;
struct SourceLocation;

namespace debug {

// 升级调试信息前向声明（完整定义在 IRDebugInfo.h）
class IRInstructionDebugInfo;
class DebugInfoEmitter;
struct DIType;

} // namespace debug
} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRDEBUGFWD_H
