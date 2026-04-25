#include "blocktype/IR/IRDebugMetadata.h"

namespace blocktype {
namespace ir {

void DICompileUnit::print(raw_ostream& OS) const {
  OS << "!DICompileUnit(";
  OS << "source: \"" << SourceFile_ << "\", ";
  OS << "producer: \"" << Producer_ << "\", ";
  OS << "language: " << Language_;
  OS << ")";
}

void DIType::print(raw_ostream& OS) const {
  OS << "!DIType(";
  OS << "name: \"" << Name_ << "\", ";
  OS << "size: " << SizeInBits_ << ", ";
  OS << "align: " << AlignInBits_;
  OS << ")";
}

void DISubprogram::print(raw_ostream& OS) const {
  OS << "!DISubprogram(";
  OS << "name: \"" << Name_ << "\"";
  if (Unit_) OS << ", unit: " << Unit_;
  if (Linkage_) OS << ", linkage: " << Linkage_;
  OS << ")";
}

void DILocation::print(raw_ostream& OS) const {
  OS << "!DILocation(";
  OS << "line: " << Line_ << ", ";
  OS << "column: " << Column_;
  if (Scope_) OS << ", scope: " << Scope_;
  OS << ")";
}

} // namespace ir
} // namespace blocktype
