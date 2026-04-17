#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// formatMessage — %0, %1, ... parameter substitution
//===----------------------------------------------------------------------===//

std::string DiagnosticsEngine::formatMessage(llvm::StringRef Msg,
                                             llvm::ArrayRef<llvm::StringRef> Args) {
  std::string Result;
  Result.reserve(Msg.size() + 64);

  for (size_t I = 0; I < Msg.size(); ++I) {
    if (Msg[I] == '%' && I + 1 < Msg.size()) {
      char Next = Msg[I + 1];
      if (Next >= '0' && Next <= '9') {
        unsigned ArgIndex = static_cast<unsigned>(Next - '0');
        if (ArgIndex < Args.size()) {
          Result += Args[ArgIndex];
        } else {
          // No argument for this placeholder — keep as-is
          Result += '%';
          Result += Next;
        }
        ++I; // Skip the digit
        continue;
      }
      // Not a %N pattern — output the % literally
      Result += '%';
      continue;
    }
    Result += Msg[I];
  }

  return Result;
}

//===----------------------------------------------------------------------===//
// report overloads
//===----------------------------------------------------------------------===//

void DiagnosticsEngine::report(SourceLocation Loc, DiagLevel Level, llvm::StringRef Message) {
  // SFINAE / overload resolution suppression: silently discard all
  // diagnostics while SuppressCount > 0.
  if (SuppressCount > 0) {
    // Still track suppressed errors so hasNewErrors() works, but do not emit.
    if (Level == DiagLevel::Error || Level == DiagLevel::Fatal)
      ++NumErrors;
    else if (Level == DiagLevel::Warning)
      ++NumWarnings;
    return;
  }

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

void DiagnosticsEngine::report(SourceLocation Loc, DiagID ID, llvm::StringRef Arg0) {
  DiagLevel Level = getDiagnosticLevel(ID);
  llvm::StringRef RawMsg = getDiagnosticMessage(ID, Lang);
  std::string Formatted = formatMessage(RawMsg, {Arg0});
  // If the message had no %0 placeholder, append the arg for backward compat
  if (RawMsg.find("%0") == llvm::StringRef::npos && !Arg0.empty()) {
    Formatted += ": " + Arg0.str();
  }
  report(Loc, Level, Formatted);
}

void DiagnosticsEngine::report(SourceLocation Loc, DiagID ID,
                                llvm::StringRef Arg0, llvm::StringRef Arg1) {
  DiagLevel Level = getDiagnosticLevel(ID);
  llvm::StringRef RawMsg = getDiagnosticMessage(ID, Lang);
  std::string Formatted = formatMessage(RawMsg, {Arg0, Arg1});
  report(Loc, Level, Formatted);
}

void DiagnosticsEngine::report(SourceLocation Loc, DiagID ID,
                               SourceLocation RangeStart, SourceLocation RangeEnd,
                               llvm::StringRef ExtraText) {
  DiagLevel Level = getDiagnosticLevel(ID);

  // SFINAE suppression
  if (SuppressCount > 0) {
    if (Level == DiagLevel::Error || Level == DiagLevel::Fatal)
      ++NumErrors;
    else if (Level == DiagLevel::Warning)
      ++NumWarnings;
    return;
  }

  llvm::StringRef RawMsg = getDiagnosticMessage(ID, Lang);
  std::string Message = ExtraText.empty()
      ? formatMessage(RawMsg, {})
      : formatMessage(RawMsg, {}) + ": " + ExtraText.str();
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

//===----------------------------------------------------------------------===//
// Diagnostic message lookup
//===----------------------------------------------------------------------===//

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

//===----------------------------------------------------------------------===//
// Printing helpers
//===----------------------------------------------------------------------===//

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

  // Print line number with padding
  std::string LineNumStr = std::to_string(Line);
  OS << llvm::format("%5s", LineNumStr.c_str()) << " | ";

  // Print the source line
  StringRef LineText(LineStart, LineEnd - LineStart);
  OS << LineText << "\n";

  // Print caret pointing to the column
  OS << std::string(6, ' ') << "| ";
  OS << std::string(Column - 1, ' ');

  if (OS.has_colors()) {
    OS.changeColor(llvm::raw_ostream::GREEN, true);
  }
  OS << "^\n";
  if (OS.has_colors()) {
    OS.resetColor();
  }
}

void DiagnosticsEngine::printSourceRange(SourceLocation Start, SourceLocation End) {
  if (!SM) return;

  auto [StartLine, StartCol] = SM->getLineAndColumn(Start);
  auto [EndLine, EndCol] = SM->getLineAndColumn(End);

  if (StartLine == 0 || EndLine == 0) return;

  if (StartLine == EndLine) {
    // Single line range - print line and highlight range
    printSourceLine(Start);
    printRangeIndicator(StartCol, EndCol);
  } else {
    // Multi-line range - print start and end lines
    printSourceLine(Start);
    printRangeIndicator(StartCol, SM->getCharacterData(Start).size());
    OS << "...\n";
    printSourceLine(End);
    printRangeIndicator(1, EndCol);
  }
}

void DiagnosticsEngine::printErrorContext(SourceLocation Loc, unsigned ContextLines) {
  if (!SM) return;

  auto [Line, Column] = SM->getLineAndColumn(Loc);
  if (Line == 0) return;

  const FileInfo *FI = SM->getFileInfo(Loc);
  if (!FI) return;

  StringRef Content = FI->getContent();

  // Find all line offsets
  llvm::SmallVector<unsigned, 256> LineOffsets;
  LineOffsets.push_back(0);
  for (size_t i = 0; i < Content.size(); ++i) {
    if (Content[i] == '\n') {
      LineOffsets.push_back(i + 1);
    }
  }

  // Determine range to display
  unsigned StartLine = std::max(1u, Line - ContextLines);
  unsigned EndLine = std::min(static_cast<unsigned>(LineOffsets.size()), Line + ContextLines);

  // Print context lines
  for (unsigned L = StartLine; L <= EndLine; ++L) {
    unsigned LineOffset = LineOffsets[L - 1];
    unsigned LineEnd = (L < LineOffsets.size()) ? LineOffsets[L] - 1 : Content.size();

    StringRef LineText = Content.substr(LineOffset, LineEnd - LineOffset);

    // Print line number with highlight for current line
    std::string LineNumStr = std::to_string(L);
    if (L == Line) {
      if (OS.has_colors()) {
        OS.changeColor(llvm::raw_ostream::CYAN, true);
      }
      OS << " > ";
      if (OS.has_colors()) {
        OS.resetColor();
      }
      OS << llvm::format("%5s", LineNumStr.c_str()) << " | ";
      OS << LineText << "\n";
    } else {
      OS << "   " << llvm::format("%5s", LineNumStr.c_str()) << " | ";
      OS << LineText << "\n";
    }
  }

  // Print caret for error location
  OS << std::string(6, ' ') << "| ";
  OS << std::string(Column - 1, ' ');
  if (OS.has_colors()) {
    OS.changeColor(llvm::raw_ostream::RED, true);
  }
  OS << "^\n";
  if (OS.has_colors()) {
    OS.resetColor();
  }
}

void DiagnosticsEngine::printRangeIndicator(unsigned StartCol, unsigned EndCol, llvm::StringRef Indicator) {
  OS << std::string(6, ' ') << "| ";
  OS << std::string(StartCol - 1, ' ');

  if (OS.has_colors()) {
    OS.changeColor(llvm::raw_ostream::RED, true);
  }

  unsigned Length = EndCol > StartCol ? EndCol - StartCol : 1;
  for (unsigned i = 0; i < Length; ++i) {
    OS << Indicator;
  }
  OS << "\n";

  if (OS.has_colors()) {
    OS.resetColor();
  }
}

} // namespace blocktype
