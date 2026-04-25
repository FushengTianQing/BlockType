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

} // namespace diag
} // namespace blocktype
