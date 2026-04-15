#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

void DiagnosticsEngine::report(SourceLocation Loc, DiagLevel Level, llvm::StringRef Message) {
  printDiagnostic(Loc, Level, Message);
  
  if (Level == DiagLevel::Error || Level == DiagLevel::Fatal)
    ++NumErrors;
  else if (Level == DiagLevel::Warning)
    ++NumWarnings;
}

void DiagnosticsEngine::report(SourceLocation Loc, DiagID ID) {
  DiagLevel Level = getDiagnosticLevel(ID);
  llvm::StringRef Message = getDiagnosticMessage(ID, Lang);
  report(Loc, Level, Message);
}

void DiagnosticsEngine::report(SourceLocation Loc, DiagID ID, llvm::StringRef ExtraText) {
  DiagLevel Level = getDiagnosticLevel(ID);
  std::string Message = std::string(getDiagnosticMessage(ID, Lang)) + ": " + ExtraText.str();
  report(Loc, Level, Message);
}

void DiagnosticsEngine::report(SourceLocation Loc, DiagID ID, 
                               SourceLocation RangeStart, SourceLocation RangeEnd,
                               llvm::StringRef ExtraText) {
  DiagLevel Level = getDiagnosticLevel(ID);
  std::string Message = std::string(getDiagnosticMessage(ID, Lang));
  if (!ExtraText.empty()) {
    Message += ": " + ExtraText.str();
  }
  printDiagnostic(Loc, Level, Message);
  
  // Print source line with highlighting if SourceManager is available
  if (SM && Loc.isValid()) {
    printSourceLine(Loc);
  }
  
  if (Level == DiagLevel::Error || Level == DiagLevel::Fatal)
    ++NumErrors;
  else if (Level == DiagLevel::Warning)
    ++NumWarnings;
}

DiagLevel DiagnosticsEngine::getDiagnosticLevel(DiagID ID) {
  switch (ID) {
#define DIAG(ID, Level, EnText, ZhText) \
    case DiagID::ID: \
      return DiagLevel::Level;
#include "blocktype/Basic/DiagnosticIDs.def"
#undef DIAG
    default:
      return DiagLevel::Error;
  }
}

const char* DiagnosticsEngine::getDiagnosticMessage(DiagID ID) {
  return getDiagnosticMessage(ID, DiagnosticLanguage::English);
}

const char* DiagnosticsEngine::getDiagnosticMessage(DiagID ID, DiagnosticLanguage Lang) {
  // English messages
  static const char* EnglishMessages[] = {
#define DIAG(ID, Level, EnText, ZhText) EnText,
#include "blocktype/Basic/DiagnosticIDs.def"
#undef DIAG
    "unknown diagnostic"
  };
  
  // Chinese messages
  static const char* ChineseMessages[] = {
#define DIAG(ID, Level, EnText, ZhText) ZhText,
#include "blocktype/Basic/DiagnosticIDs.def"
#undef DIAG
    "未知诊断"
  };
  
  unsigned Index = static_cast<unsigned>(ID);
  if (Index >= static_cast<unsigned>(DiagID::NUM_DIAGNOSTICS)) {
    Index = static_cast<unsigned>(DiagID::NUM_DIAGNOSTICS);
  }
  
  switch (Lang) {
    case DiagnosticLanguage::Chinese:
      return ChineseMessages[Index];
    case DiagnosticLanguage::English:
    case DiagnosticLanguage::Auto:
    default:
      return EnglishMessages[Index];
  }
}

const char* DiagnosticsEngine::getSeverityName(DiagLevel Level) const {
  if (Lang == DiagnosticLanguage::Chinese) {
    switch (Level) {
      case DiagLevel::Ignored: return "忽略";
      case DiagLevel::Note:    return "备注";
      case DiagLevel::Remark:  return "注记";
      case DiagLevel::Warning: return "警告";
      case DiagLevel::Error:   return "错误";
      case DiagLevel::Fatal:   return "致命错误";
    }
  } else {
    switch (Level) {
      case DiagLevel::Ignored: return "ignored";
      case DiagLevel::Note:    return "note";
      case DiagLevel::Remark:  return "remark";
      case DiagLevel::Warning: return "warning";
      case DiagLevel::Error:   return "error";
      case DiagLevel::Fatal:   return "fatal error";
    }
  }
  return "unknown";
}

void DiagnosticsEngine::printDiagnostic(SourceLocation Loc, DiagLevel Level, llvm::StringRef Message) {
  // Print location
  if (SM && Loc.isValid()) {
    SM->printLocation(OS, Loc);
  } else if (Loc.isValid()) {
    OS << "<input>";
  } else {
    OS << "<unknown>";
  }

  // Print severity with color
  if (OS.has_colors()) {
    switch (Level) {
      case DiagLevel::Error:
      case DiagLevel::Fatal:
        OS.changeColor(llvm::raw_ostream::RED, true);
        break;
      case DiagLevel::Warning:
        OS.changeColor(llvm::raw_ostream::MAGENTA, true);
        break;
      case DiagLevel::Note:
      case DiagLevel::Remark:
        OS.changeColor(llvm::raw_ostream::CYAN);
        break;
      case DiagLevel::Ignored:
        break;
    }
  }
  
  OS << ": " << getSeverityName(Level) << ": ";
  
  if (OS.has_colors()) {
    OS.resetColor();
  }

  // Print message
  OS << Message << "\n";
}

void DiagnosticsEngine::printSourceLine(SourceLocation Loc) {
  if (!SM) return;

  auto [Line, Column] = SM->getLineAndColumn(Loc);
  if (Line == 0 || Column == 0) return;

  // Get file info
  const FileInfo *FI = SM->getFileInfo(Loc);
  if (!FI) return;

  // Get the full file content
  StringRef Content = FI->getContent();
  unsigned Offset = Loc.getOffset();
  if (Offset >= Content.size()) return;

  // Find line start
  const char *LineStart = Content.data() + Offset;
  while (LineStart > Content.data() && LineStart[-1] != '\n') {
    --LineStart;
  }

  // Find line end
  const char *LineEnd = Content.data() + Offset;
  while (LineEnd < Content.data() + Content.size() && *LineEnd != '\n' && *LineEnd != '\r') {
    ++LineEnd;
  }

  // Print line number
  std::string LineNumStr = std::to_string(Line);
  OS << LineNumStr << " | ";

  // Print the source line
  StringRef LineText(LineStart, LineEnd - LineStart);
  OS << LineText << "\n";

  // Print caret pointing to the column
  OS << std::string(LineNumStr.size(), ' ') << " | ";
  OS << std::string(Column - 1, ' ');

  if (OS.has_colors()) {
    OS.changeColor(llvm::raw_ostream::GREEN, true);
  }
  OS << "^\n";
  if (OS.has_colors()) {
    OS.resetColor();
  }
}

} // namespace blocktype
