//===--- CodeViewEmitter.cpp - CodeView Debug Info Emitter ---*- C++ -*-===//
//
// Part of the BlockType Project.
// CodeView debug format emitter for Windows backend (stub implementation).
//
//===--------------------------------------------------------------------===//

#include "blocktype/Backend/DebugInfoEmitter.h"
#include "blocktype/IR/IRModule.h"

namespace blocktype::backend {

bool CodeViewEmitter::emit(const ir::IRModule& M, ir::raw_ostream& OS) {
  // Default: emit using CodeView format
  return emitCodeView(M, OS);
}

bool CodeViewEmitter::emitCodeView(const ir::IRModule& M, ir::raw_ostream& OS) {
  // Stub: CodeView emission is not yet fully implemented.
  // This is a Windows-specific debug format; macOS cannot verify output.
  // Return false to indicate the format is not yet supported.
  (void)M;
  (void)OS;
  return false;
}

bool CodeViewEmitter::emitDWARF5(const ir::IRModule& M, ir::raw_ostream& OS) {
  // CodeView emitter does not support DWARF5
  (void)M;
  (void)OS;
  return false;
}

bool CodeViewEmitter::emitDWARF4(const ir::IRModule& M, ir::raw_ostream& OS) {
  // CodeView emitter does not support DWARF4
  (void)M;
  (void)OS;
  return false;
}

} // namespace blocktype::backend
