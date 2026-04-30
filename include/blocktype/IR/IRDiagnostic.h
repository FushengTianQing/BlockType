#ifndef BLOCKTYPE_IR_IRDIAGNOSTIC_H
#define BLOCKTYPE_IR_IRDIAGNOSTIC_H

#include <cstdint>
#include <optional>
#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRDebugMetadata.h"

namespace blocktype {
namespace diag {

// ============================================================
// DiagnosticLevel — 诊断严重级别
// ============================================================

enum class DiagnosticLevel : uint8_t {
  Note    = 0,
  Remark  = 1,
  Warning = 2,
  Error   = 3,
  Fatal   = 4,
};

const char* getDiagnosticLevelName(DiagnosticLevel L);

// ============================================================
// DiagnosticGroup — 诊断分组
// ============================================================

enum class DiagnosticGroup : uint16_t {
  TypeMapping            = 0,
  InstructionValidation = 1,
  IRVerification         = 2,
  BackendCodegen         = 3,
  FFIBinding             = 4,
  Serialization          = 5,
};

const char* getDiagnosticGroupName(DiagnosticGroup G);

// ============================================================
// DiagnosticCode — 诊断错误码
// ============================================================

/// 编码规则：0xGGNN — GG=DiagnosticGroup值+1, NN=组内序号
enum class DiagnosticCode : uint32_t {
  Unknown              = 0x0000,

  // TypeMapping (0x01xx)
  TypeMappingFailed    = 0x0100,
  TypeMappingAmbiguous = 0x0101,

  // InstructionValidation (0x02xx)
  InvalidInstruction   = 0x0200,
  InvalidOperand       = 0x0201,

  // IRVerification (0x03xx)
  VerificationFailed   = 0x0300,
  InvalidModule        = 0x0301,

  // BackendCodegen (0x04xx)
  CodegenFailed        = 0x0400,
  UnsupportedType      = 0x0401,

  // FFIBinding (0x05xx)
  FFIBindingFailed     = 0x0500,
  InvalidFFISignature  = 0x0501,

  // Serialization (0x06xx)
  SerializationFailed    = 0x0600,
  DeserializationFailed  = 0x0601,
};

const char* getDiagnosticCodeName(DiagnosticCode C);
DiagnosticGroup getGroupForCode(DiagnosticCode C);

// ============================================================
// StructuredDiagnostic — 单条结构化诊断
// ============================================================

struct StructuredDiagnostic {
  DiagnosticLevel Level = DiagnosticLevel::Note;
  DiagnosticGroup Group = DiagnosticGroup::TypeMapping;
  DiagnosticCode  Code  = DiagnosticCode::Unknown;
  std::string Message;
  ir::SourceLocation Loc;
  ir::SmallVector<std::string, 4> Notes;
  
  // === B-F6 新增字段 ===
  std::string Category;                              // 诊断类别
  std::string FlagName;                              // 命令行标志名（如 "ir"）
  
  // FixIt 提示
  struct FixItHint {
    std::string Replacement;
    std::string Description;
  };
  ir::SmallVector<FixItHint, 2> FixIts;
  
  // IR 相关信息（可选）
  std::optional<uint8_t> IRRelatedDialect;  // dialect::DialectID
  std::optional<uint8_t> IRRelatedOpcode;   // Opcode

  // 虚析构函数，使结构体成为多态类型，支持 dynamic_cast
  virtual ~StructuredDiagnostic() = default;

  DiagnosticLevel getLevel() const { return Level; }
  DiagnosticGroup getGroup() const { return Group; }
  DiagnosticCode  getCode()  const { return Code;  }
  ir::StringRef getMessage() const { return Message; }
  const ir::SourceLocation& getLocation() const { return Loc; }

  void addNote(ir::StringRef N) { Notes.push_back(N.str()); }

  void setLocation(ir::StringRef Filename, unsigned Line, unsigned Column) {
    Loc.Filename = Filename;
    Loc.Line = Line;
    Loc.Column = Column;
  }

  std::string toJSON() const;
  std::string toText() const;
};

// ============================================================
// StructuredDiagEmitter — 诊断发射器抽象基类
// ============================================================

class StructuredDiagEmitter {
public:
  virtual ~StructuredDiagEmitter() = default;
  virtual void emit(const StructuredDiagnostic& D) = 0;
};

// ============================================================
// TextDiagEmitter — 文本格式诊断发射器
// ============================================================

class TextDiagEmitter : public StructuredDiagEmitter {
  ir::raw_ostream& OS;
  bool ColorsEnabled;

public:
  explicit TextDiagEmitter(ir::raw_ostream& OS, bool Colors = false)
    : OS(OS), ColorsEnabled(Colors) {}

  void emit(const StructuredDiagnostic& D) override;

private:
  void emitLevel(ir::raw_ostream& OS, DiagnosticLevel L);
  void emitLocation(ir::raw_ostream& OS, const ir::SourceLocation& Loc);
  void emitNotes(ir::raw_ostream& OS, const ir::SmallVector<std::string, 4>& Notes);
  void emitFixIts(ir::raw_ostream& OS, const ir::SmallVector<StructuredDiagnostic::FixItHint, 2>& FixIts);
};

// ============================================================
// JSONDiagEmitter — JSON 格式诊断发射器
// ============================================================

class JSONDiagEmitter : public StructuredDiagEmitter {
  ir::raw_ostream& OS;
  bool PrettyPrint;

public:
  explicit JSONDiagEmitter(ir::raw_ostream& OS, bool Pretty = false)
    : OS(OS), PrettyPrint(Pretty) {}

  void emit(const StructuredDiagnostic& D) override;

private:
  void emitJSONField(ir::raw_ostream& OS, ir::StringRef Key, ir::StringRef Value, bool Last);
  void emitJSONField(ir::raw_ostream& OS, ir::StringRef Key, unsigned Value, bool Last);
  void emitJSONField(ir::raw_ostream& OS, ir::StringRef Key, bool Value, bool Last);
};

// ============================================================
// DiagnosticGroupManager — 诊断分组开关管理（Pimpl）
// ============================================================

class DiagnosticGroupManager {
public:
  DiagnosticGroupManager();
  ~DiagnosticGroupManager();

  DiagnosticGroupManager(const DiagnosticGroupManager&) = delete;
  DiagnosticGroupManager& operator=(const DiagnosticGroupManager&) = delete;

  void enableGroup(DiagnosticGroup G);
  void disableGroup(DiagnosticGroup G);
  bool isGroupEnabled(DiagnosticGroup G) const;

  void enableAll();
  void disableAll();

private:
  class Impl;
  Impl* Pimpl;
};

} // namespace diag
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRDIAGNOSTIC_H
