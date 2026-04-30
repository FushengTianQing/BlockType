#include "blocktype/IR/IRDiagnostic.h"

#include <cstdio>
#include <string>

namespace blocktype {
namespace diag {

// ============================================================
// 名称查找
// ============================================================

const char* getDiagnosticLevelName(DiagnosticLevel L) {
  switch (L) {
  case DiagnosticLevel::Note:    return "note";
  case DiagnosticLevel::Remark:  return "remark";
  case DiagnosticLevel::Warning: return "warning";
  case DiagnosticLevel::Error:   return "error";
  case DiagnosticLevel::Fatal:   return "fatal";
  }
  return "unknown";
}

const char* getDiagnosticGroupName(DiagnosticGroup G) {
  switch (G) {
  case DiagnosticGroup::TypeMapping:            return "TypeMapping";
  case DiagnosticGroup::InstructionValidation: return "InstructionValidation";
  case DiagnosticGroup::IRVerification:         return "IRVerification";
  case DiagnosticGroup::BackendCodegen:         return "BackendCodegen";
  case DiagnosticGroup::FFIBinding:             return "FFIBinding";
  case DiagnosticGroup::Serialization:          return "Serialization";
  }
  return "Unknown";
}

const char* getDiagnosticCodeName(DiagnosticCode C) {
  switch (C) {
  case DiagnosticCode::Unknown:              return "Unknown";
  case DiagnosticCode::TypeMappingFailed:    return "TypeMappingFailed";
  case DiagnosticCode::TypeMappingAmbiguous: return "TypeMappingAmbiguous";
  case DiagnosticCode::InvalidInstruction:   return "InvalidInstruction";
  case DiagnosticCode::InvalidOperand:       return "InvalidOperand";
  case DiagnosticCode::VerificationFailed:   return "VerificationFailed";
  case DiagnosticCode::InvalidModule:        return "InvalidModule";
  case DiagnosticCode::CodegenFailed:        return "CodegenFailed";
  case DiagnosticCode::UnsupportedType:      return "UnsupportedType";
  case DiagnosticCode::FFIBindingFailed:     return "FFIBindingFailed";
  case DiagnosticCode::InvalidFFISignature:  return "InvalidFFISignature";
  case DiagnosticCode::SerializationFailed:  return "SerializationFailed";
  case DiagnosticCode::DeserializationFailed:return "DeserializationFailed";
  }
  return "UnknownCode";
}

DiagnosticGroup getGroupForCode(DiagnosticCode C) {
  uint32_t Val = static_cast<uint32_t>(C);
  uint32_t GroupByte = (Val >> 8) & 0xFF;
  if (GroupByte == 0) return DiagnosticGroup::TypeMapping;
  uint32_t GroupVal = GroupByte - 1;
  if (GroupVal > static_cast<uint32_t>(DiagnosticGroup::Serialization))
    return DiagnosticGroup::TypeMapping;
  return static_cast<DiagnosticGroup>(GroupVal);
}

// ============================================================
// JSON 转义辅助
// ============================================================

static std::string escapeJSON(ir::StringRef S) {
  std::string Result;
  Result.reserve(S.size());
  for (char C : S) {
    switch (C) {
    case '"':  Result += "\\\""; break;
    case '\\': Result += "\\\\"; break;
    case '\n': Result += "\\n";  break;
    case '\r': Result += "\\r";  break;
    case '\t': Result += "\\t";  break;
    default:
      if (static_cast<unsigned char>(C) < 0x20) {
        char Buf[8];
        std::snprintf(Buf, sizeof(Buf), "\\u%04x",
                      static_cast<unsigned char>(C));
        Result += Buf;
      } else {
        Result += C;
      }
    }
  }
  return Result;
}

// ============================================================
// StructuredDiagnostic 输出
// ============================================================

std::string StructuredDiagnostic::toJSON() const {
  std::string OS;
  OS.reserve(256);
  OS += "{\n";
  OS += "  \"level\": \""; OS += getDiagnosticLevelName(Level); OS += "\",\n";
  OS += "  \"group\": \""; OS += getDiagnosticGroupName(Group); OS += "\",\n";
  OS += "  \"code\": \"";  OS += getDiagnosticCodeName(Code);   OS += "\",\n";
  OS += "  \"message\": \""; OS += escapeJSON(Message); OS += "\"";
  if (Loc.isValid()) {
    OS += ",\n  \"location\": {\n";
    OS += "    \"file\": \""; OS += escapeJSON(Loc.Filename); OS += "\",\n";
    OS += "    \"line\": "; OS += std::to_string(Loc.Line);  OS += ",\n";
    OS += "    \"column\": "; OS += std::to_string(Loc.Column); OS += "\n  }";
  }
  if (!Notes.empty()) {
    OS += ",\n  \"notes\": [";
    for (size_t i = 0; i < Notes.size(); ++i) {
      if (i > 0) OS += ",";
      OS += "\n    \""; OS += escapeJSON(ir::StringRef(Notes[i])); OS += "\"";
    }
    OS += "\n  ]";
  }
  OS += "\n}\n";
  return OS;
}

std::string StructuredDiagnostic::toText() const {
  std::string OS;
  OS.reserve(128);
  OS += getDiagnosticLevelName(Level);
  OS += ": ["; OS += getDiagnosticGroupName(Group); OS += "] ";
  OS += getDiagnosticCodeName(Code);
  OS += ": "; OS += Message;
  if (Loc.isValid()) {
    OS += "\n  at "; OS += Loc.Filename.str();
    OS += ":"; OS += std::to_string(Loc.Line);
    OS += ":"; OS += std::to_string(Loc.Column);
  }
  for (const auto& N : Notes) {
    OS += "\n  note: "; OS += N;
  }
  OS += "\n";
  return OS;
}

// ============================================================
// DiagnosticGroupManager Pimpl
// ============================================================

class DiagnosticGroupManager::Impl {
public:
  ir::DenseMap<uint16_t, bool> Overrides;
  bool AllEnabled = true;
};

DiagnosticGroupManager::DiagnosticGroupManager()
    : Pimpl(new Impl()) {}

DiagnosticGroupManager::~DiagnosticGroupManager() {
  delete Pimpl;
}

void DiagnosticGroupManager::enableGroup(DiagnosticGroup G) {
  Pimpl->Overrides[static_cast<uint16_t>(G)] = true;
}

void DiagnosticGroupManager::disableGroup(DiagnosticGroup G) {
  Pimpl->Overrides[static_cast<uint16_t>(G)] = false;
}

bool DiagnosticGroupManager::isGroupEnabled(DiagnosticGroup G) const {
  uint16_t Key = static_cast<uint16_t>(G);
  auto It = Pimpl->Overrides.find(Key);
  if (It != Pimpl->Overrides.end())
    return (*It).second;
  return Pimpl->AllEnabled;
}

void DiagnosticGroupManager::enableAll() {
  Pimpl->Overrides.clear();
  Pimpl->AllEnabled = true;
}

void DiagnosticGroupManager::disableAll() {
  Pimpl->Overrides.clear();
  Pimpl->AllEnabled = false;
}

// ============================================================
// TextDiagEmitter 实现
// ============================================================

void TextDiagEmitter::emit(const StructuredDiagnostic& D) {
  emitLocation(OS, D.getLocation());
  OS << ": ";
  emitLevel(OS, D.getLevel());
  OS << ": ";
  OS << D.getMessage();

  if (!D.FlagName.empty()) {
    OS << " [-W" << D.FlagName << "]";
  }
  OS << "\n";

  emitNotes(OS, D.Notes);
  emitFixIts(OS, D.FixIts);

  OS.flush();
}

void TextDiagEmitter::emitLocation(ir::raw_ostream& OS, const ir::SourceLocation& Loc) {
  if (Loc.isValid()) {
    OS << Loc.Filename << ":" << Loc.Line << ":" << Loc.Column;
  } else {
    OS << "<unknown>";
  }
}

void TextDiagEmitter::emitLevel(ir::raw_ostream& OS, DiagnosticLevel L) {
  if (ColorsEnabled) {
    switch (L) {
      case DiagnosticLevel::Note:    OS << "\033[1;34mnote\033[0m"; break;    // 蓝色
      case DiagnosticLevel::Remark:  OS << "\033[1;36mremark\033[0m"; break;  // 青色
      case DiagnosticLevel::Warning: OS << "\033[1;33mwarning\033[0m"; break;  // 黄色
      case DiagnosticLevel::Error:   OS << "\033[1;31merror\033[0m"; break;    // 红色
      case DiagnosticLevel::Fatal:   OS << "\033[1;35mfatal error\033[0m"; break; // 紫色
    }
  } else {
    switch (L) {
      case DiagnosticLevel::Note:    OS << "note"; break;
      case DiagnosticLevel::Remark:  OS << "remark"; break;
      case DiagnosticLevel::Warning: OS << "warning"; break;
      case DiagnosticLevel::Error:   OS << "error"; break;
      case DiagnosticLevel::Fatal:   OS << "fatal error"; break;
    }
  }
}

void TextDiagEmitter::emitNotes(ir::raw_ostream& OS, const ir::SmallVector<std::string, 4>& Notes) {
  for (const auto& Note : Notes) {
    OS << "  " << Note << "\n";
  }
}

void TextDiagEmitter::emitFixIts(ir::raw_ostream& OS, const ir::SmallVector<StructuredDiagnostic::FixItHint, 2>& FixIts) {
  for (const auto& FI : FixIts) {
    OS << "  FIX-IT: " << FI.Description << "\n";
    OS << "    Replace with: " << FI.Replacement << "\n";
  }
}

// ============================================================
// JSONDiagEmitter 实现
// ============================================================

void JSONDiagEmitter::emit(const StructuredDiagnostic& D) {
  OS << "{";

  emitJSONField(OS, "level", getDiagnosticLevelName(D.getLevel()), false);
  emitJSONField(OS, "code", getDiagnosticCodeName(D.getCode()), false);
  emitJSONField(OS, "message", D.getMessage(), false);

  // location
  OS << "\"location\": {";
  emitJSONField(OS, "file", D.getLocation().Filename, false);
  emitJSONField(OS, "line", D.getLocation().Line, false);
  emitJSONField(OS, "column", D.getLocation().Column, true);
  OS << "}, ";

  // category
  if (!D.Category.empty()) {
    emitJSONField(OS, "category", D.Category, false);
  }

  // flag
  if (!D.FlagName.empty()) {
    emitJSONField(OS, "flag", D.FlagName, false);
  }

  // IR-related info
  if (D.IRRelatedDialect.has_value()) {
    OS << "\"ir_dialect\": " << static_cast<unsigned>(*D.IRRelatedDialect) << ", ";
  }

  if (D.IRRelatedOpcode.has_value()) {
    OS << "\"ir_opcode\": " << static_cast<unsigned>(*D.IRRelatedOpcode) << ", ";
  }

  // notes
  if (!D.Notes.empty()) {
    OS << "\"notes\": [";
    for (size_t i = 0; i < D.Notes.size(); ++i) {
      OS << "\"" << D.Notes[i] << "\"";
      if (i < D.Notes.size() - 1) OS << ", ";
    }
    OS << "], ";
  }

  // fixits
  if (!D.FixIts.empty()) {
    OS << "\"fixits\": [";
    for (size_t i = 0; i < D.FixIts.size(); ++i) {
      const auto& FI = D.FixIts[i];
      OS << "{\"description\": \"" << FI.Description << "\", ";
      OS << "\"replacement\": \"" << FI.Replacement << "\"}";
      if (i < D.FixIts.size() - 1) OS << ", ";
    }
    OS << "]";
  } else {
    OS << "\"fixits\": []";
  }

  OS << "}\n";
  OS.flush();
}

void JSONDiagEmitter::emitJSONField(ir::raw_ostream& OS, ir::StringRef Key, ir::StringRef Value, bool Last) {
  OS << "\"" << Key << "\": \"" << Value << "\"";
  if (!Last) OS << ", ";
}

void JSONDiagEmitter::emitJSONField(ir::raw_ostream& OS, ir::StringRef Key, unsigned Value, bool Last) {
  OS << "\"" << Key << "\": " << Value;
  if (!Last) OS << ", ";
}

void JSONDiagEmitter::emitJSONField(ir::raw_ostream& OS, ir::StringRef Key, bool Value, bool Last) {
  OS << "\"" << Key << "\": " << (Value ? "true" : "false");
  if (!Last) OS << ", ";
}

} // namespace diag
} // namespace blocktype
