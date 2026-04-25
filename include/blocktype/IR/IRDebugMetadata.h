#ifndef BLOCKTYPE_IR_IRDEBUGMETADATA_H
#define BLOCKTYPE_IR_IRDEBUGMETADATA_H

#include <cstdint>
#include <string>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

// ============================================================
// SourceLocation — 源码位置（基础类型，两套调试信息共用）
// ============================================================

struct SourceLocation {
  StringRef Filename;
  unsigned Line = 0;
  unsigned Column = 0;

  bool isValid() const { return !Filename.empty() && Line > 0; }

  bool operator==(const SourceLocation& Other) const {
    return Filename == Other.Filename &&
           Line == Other.Line &&
           Column == Other.Column;
  }
  bool operator!=(const SourceLocation& Other) const { return !(*this == Other); }
};

// ============================================================
// DebugMetadata — 调试元数据基类
// ============================================================

/// 调试元数据基类。独立于 IRMetadata，避免头文件循环依赖。
/// 接口与 IRMetadata 一致（virtual dtor + virtual print），未来可通过
/// adapter 存入 IRModule::Metadata 容器。
class DebugMetadata {
public:
  virtual ~DebugMetadata() = default;
  virtual void print(raw_ostream& OS) const = 0;

  /// 调试元数据类型标识，用于 dyn_cast 等场景
  enum class DebugKind : uint8_t {
    CompileUnit = 0,
    Type = 1,
    Subprogram = 2,
    Location = 3,
  };
  DebugKind getDebugKind() const { return Kind_; }

protected:
  DebugKind Kind_;
  explicit DebugMetadata(DebugKind K) : Kind_(K) {}
};

// ============================================================
// DICompileUnit — 编译单元调试信息
// ============================================================

class DICompileUnit : public DebugMetadata {
  std::string SourceFile_;
  std::string Producer_;
  unsigned Language_;

public:
  explicit DICompileUnit(StringRef Source = "", StringRef Producer = "",
                         unsigned Lang = 0)
    : DebugMetadata(DebugKind::CompileUnit),
      SourceFile_(Source.str()), Producer_(Producer.str()), Language_(Lang) {}

  StringRef getSourceFile() const { return SourceFile_; }
  void setSourceFile(StringRef F) { SourceFile_ = F.str(); }

  StringRef getProducer() const { return Producer_; }
  void setProducer(StringRef P) { Producer_ = P.str(); }

  unsigned getLanguage() const { return Language_; }
  void setLanguage(unsigned L) { Language_ = L; }

  void print(raw_ostream& OS) const override;

  static bool classof(const DebugMetadata* M) {
    return M->getDebugKind() == DebugKind::CompileUnit;
  }
};

// ============================================================
// DIType — 基础类型调试信息
// ============================================================

class DIType : public DebugMetadata {
  std::string Name_;
  uint64_t SizeInBits_;
  uint64_t AlignInBits_;

public:
  explicit DIType(StringRef N = "", uint64_t Size = 0, uint64_t Align = 0)
    : DebugMetadata(DebugKind::Type),
      Name_(N.str()), SizeInBits_(Size), AlignInBits_(Align) {}

  StringRef getName() const { return Name_; }
  void setName(StringRef N) { Name_ = N.str(); }

  uint64_t getSizeInBits() const { return SizeInBits_; }
  void setSizeInBits(uint64_t S) { SizeInBits_ = S; }

  uint64_t getAlignInBits() const { return AlignInBits_; }
  void setAlignInBits(uint64_t A) { AlignInBits_ = A; }

  void print(raw_ostream& OS) const override;

  static bool classof(const DebugMetadata* M) {
    return M->getDebugKind() == DebugKind::Type;
  }
};

// ============================================================
// DISubprogram — 函数/子程序调试信息
// ============================================================

class DISubprogram : public DebugMetadata {
  std::string Name_;
  DICompileUnit* Unit_;
  DISubprogram* Linkage_;

public:
  explicit DISubprogram(StringRef N = "", DICompileUnit* U = nullptr,
                        DISubprogram* L = nullptr)
    : DebugMetadata(DebugKind::Subprogram),
      Name_(N.str()), Unit_(U), Linkage_(L) {}

  StringRef getName() const { return Name_; }
  void setName(StringRef N) { Name_ = N.str(); }

  DICompileUnit* getUnit() const { return Unit_; }
  void setUnit(DICompileUnit* U) { Unit_ = U; }

  DISubprogram* getLinkage() const { return Linkage_; }
  void setLinkage(DISubprogram* L) { Linkage_ = L; }

  void print(raw_ostream& OS) const override;

  static bool classof(const DebugMetadata* M) {
    return M->getDebugKind() == DebugKind::Subprogram;
  }
};

// ============================================================
// DILocation — 源码位置调试信息
// ============================================================

class DILocation : public DebugMetadata {
  unsigned Line_;
  unsigned Column_;
  DISubprogram* Scope_;

public:
  explicit DILocation(unsigned L = 0, unsigned C = 0,
                      DISubprogram* S = nullptr)
    : DebugMetadata(DebugKind::Location),
      Line_(L), Column_(C), Scope_(S) {}

  unsigned getLine() const { return Line_; }
  void setLine(unsigned L) { Line_ = L; }

  unsigned getColumn() const { return Column_; }
  void setColumn(unsigned C) { Column_ = C; }

  DISubprogram* getScope() const { return Scope_; }
  void setScope(DISubprogram* S) { Scope_ = S; }

  void print(raw_ostream& OS) const override;

  static bool classof(const DebugMetadata* M) {
    return M->getDebugKind() == DebugKind::Location;
  }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRDEBUGMETADATA_H
