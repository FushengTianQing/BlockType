#include "blocktype/IR/IRDebugInfo.h"

// IRInstructionDebugInfo 的所有方法均在头文件中 inline 定义。
// 此文件预留用于 DebugInfoEmitter 的具体实现（如 DWARF5Emitter 等）。
// 当前的 DebugInfoEmitter 是纯接口，无默认实现需要在此提供。
//
// 未来可在此文件中添加：
// - 默认的 DebugInfoEmitter 实现（如 NullDebugInfoEmitter）
// - IRInstructionDebugInfo 的序列化/反序列化辅助函数
