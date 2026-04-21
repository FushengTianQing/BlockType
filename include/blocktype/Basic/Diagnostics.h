#pragma once

#include "blocktype/Basic/SourceLocation.h"
#include "blocktype/Basic/DiagnosticIDs.h"
#include "blocktype/Basic/FixItHint.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

class SourceManager;

/// Diagnostic language selection
enum class DiagnosticLanguage {
  English,
  Chinese,
  Auto  // Auto-detect from source file
};

/// DiagnosticsEngine - Handles diagnostic reporting with multi-language support
///
/// Supports printf-style %0, %1, ... parameter substitution in diagnostic
/// messages. For example, if a diagnostic message is "use of undeclared
/// identifier '%0'", calling report(Loc, ID, "x") will produce:
///   "use of undeclared identifier 'x'"
class DiagnosticsEngine {
  unsigned NumErrors = 0;
  unsigned NumWarnings = 0;
  llvm::raw_ostream &OS;
  const SourceManager *SM = nullptr;
  DiagnosticLanguage Lang = DiagnosticLanguage::English;

  /// Error suppression nesting count. When > 0, all diagnostics are
  /// silently discarded (SFINAE / overload resolution suppression).
  unsigned SuppressCount = 0;

public:
  explicit DiagnosticsEngine(llvm::raw_ostream &Out = llvm::errs())
    : OS(Out) {}

  /// Set the source manager for location printing
  void setSourceManager(const SourceManager *Manager) { SM = Manager; }

  /// Set the diagnostic language
  void setLanguage(DiagnosticLanguage L) { Lang = L; }

  /// Get the current diagnostic language
  DiagnosticLanguage getLanguage() const { return Lang; }

  //===------------------------------------------------------------------===//
  // report overloads
  //===------------------------------------------------------------------===//

  /// Report a diagnostic with a custom message (no substitution).
  void report(SourceLocation Loc, DiagLevel Level, llvm::StringRef Message);

  /// Report a diagnostic with a diagnostic ID (no parameters).
  void report(SourceLocation Loc, DiagID ID);

  /// Report a diagnostic with one %0 substitution parameter.
  void report(SourceLocation Loc, DiagID ID, llvm::StringRef Arg0);

  /// Report a diagnostic with two %0/%1 substitution parameters.
  void report(SourceLocation Loc, DiagID ID,
              llvm::StringRef Arg0, llvm::StringRef Arg1);

  /// Report a diagnostic with three %0/%1/%2 substitution parameters.
  void report(SourceLocation Loc, DiagID ID,
              llvm::StringRef Arg0, llvm::StringRef Arg1, llvm::StringRef Arg2);

  /// Report a diagnostic with source range (for highlighting).
  void report(SourceLocation Loc, DiagID ID,
              SourceLocation RangeStart, SourceLocation RangeEnd,
              llvm::StringRef ExtraText = "");

  //===------------------------------------------------------------------===//
  // Fix-It Hints support
  //===------------------------------------------------------------------===//

  /// Report a diagnostic with Fix-It hints.
  void report(SourceLocation Loc, DiagID ID,
              llvm::ArrayRef<FixItHint> Hints);

  /// Report a diagnostic with one parameter and Fix-It hints.
  void report(SourceLocation Loc, DiagID ID,
              llvm::StringRef Arg0, llvm::ArrayRef<FixItHint> Hints);

  /// Report a diagnostic with two parameters and Fix-It hints.
  void report(SourceLocation Loc, DiagID ID,
              llvm::StringRef Arg0, llvm::StringRef Arg1,
              llvm::ArrayRef<FixItHint> Hints);

  /// Report a custom diagnostic with Fix-It hints.
  void report(SourceLocation Loc, DiagLevel Level, 
              llvm::StringRef Message, llvm::ArrayRef<FixItHint> Hints);

  unsigned getNumErrors() const { return NumErrors; }
  unsigned getNumWarnings() const { return NumWarnings; }
  bool hasErrorOccurred() const { return NumErrors > 0; }
  void reset() { NumErrors = 0; NumWarnings = 0; }

  //===------------------------------------------------------------------===//
  // SFINAE / Error suppression
  //===------------------------------------------------------------------===//

  /// Enter a diagnostic suppression context. All subsequent report() calls
  /// are silently discarded until popSuppression() is called.
  /// Typically used by SFINAEGuard during template argument deduction and
  /// overload resolution to prevent hard errors in the "immediate context".
  void pushSuppression() { ++SuppressCount; }

  /// Exit a diagnostic suppression context. Restores normal diagnostic
  /// emission when the suppression nesting count reaches zero.
  void popSuppression() { if (SuppressCount > 0) --SuppressCount; }

  /// Returns true if diagnostics are currently being suppressed.
  bool isSuppressed() const { return SuppressCount > 0; }

  /// Returns the current suppression nesting depth.
  unsigned getSuppressCount() const { return SuppressCount; }

  /// Get the severity level for a diagnostic ID
  static DiagLevel getDiagnosticLevel(DiagID ID);

  /// Get the message text for a diagnostic ID (English)
  static const char* getDiagnosticMessage(DiagID ID);

  /// Get the message text for a diagnostic ID in specified language
  static const char* getDiagnosticMessage(DiagID ID, DiagnosticLanguage Lang);

  /// Get severity name in current language
  const char* getSeverityName(DiagLevel Level) const;

  /// Format a message by replacing %0, %1, ... with the provided arguments.
  /// Returns the formatted string.
  static std::string formatMessage(llvm::StringRef Msg,
                                   llvm::ArrayRef<llvm::StringRef> Args);

private:
  void printDiagnostic(SourceLocation Loc, DiagLevel Level, llvm::StringRef Message);
  void printSourceLine(SourceLocation Loc);
  void printSourceRange(SourceLocation Start, SourceLocation End);
  void printErrorContext(SourceLocation Loc, unsigned ContextLines = 2);
  void printRangeIndicator(unsigned StartCol, unsigned EndCol, llvm::StringRef Indicator = "~");
  
  /// Print Fix-It hints after a diagnostic
  void printFixItHints(llvm::ArrayRef<FixItHint> Hints);
  
  /// Print a single Fix-It hint
  void printFixItHint(const FixItHint &Hint);
};

} // namespace blocktype
