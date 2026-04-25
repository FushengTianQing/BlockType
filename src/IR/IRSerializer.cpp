#include "blocktype/IR/IRSerializer.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRFormatVersion.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRInstruction.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/IRValue.h"

namespace blocktype {
namespace ir {

// ============================================================================
// Helpers — 类型到字符串
// ============================================================================

static std::string typeToString(const IRType* T) {
  if (!T) return "<null>";
  switch (T->getKind()) {
  case IRType::Void:   return "void";
  case IRType::Bool:   return "i1";
  case IRType::Integer:
    return "i" + std::to_string(static_cast<const IRIntegerType*>(T)->getBitWidth());
  case IRType::Float:
    return "f" + std::to_string(static_cast<const IRFloatType*>(T)->getBitWidth());
  case IRType::Pointer: {
    auto* PT = static_cast<const IRPointerType*>(T);
    return typeToString(PT->getPointeeType()) + "*";
  }
  case IRType::Array: {
    auto* AT = static_cast<const IRArrayType*>(T);
    return "[" + std::to_string(AT->getNumElements()) + " x " + typeToString(AT->getElementType()) + "]";
  }
  case IRType::Vector: {
    auto* VT = static_cast<const IRVectorType*>(T);
    return "<" + std::to_string(VT->getNumElements()) + " x " + typeToString(VT->getElementType()) + ">";
  }
  case IRType::Struct: {
    auto* ST = static_cast<const IRStructType*>(T);
    if (ST->getName().empty()) {
      std::string S = "struct { ";
      for (unsigned i = 0; i < ST->getNumFields(); ++i) {
        if (i > 0) S += ", ";
        S += typeToString(ST->getFieldType(i));
      }
      S += " }";
      return S;
    }
    return ST->getName().str();
  }
  case IRType::Function: {
    auto* FT = static_cast<const IRFunctionType*>(T);
    std::string S = typeToString(FT->getReturnType()) + " (";
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
      if (i > 0) S += ", ";
      S += typeToString(FT->getParamType(i));
    }
    if (FT->isVarArg()) {
      if (FT->getNumParams() > 0) S += ", ";
      S += "...";
    }
    S += ")";
    return S;
  }
  case IRType::Opaque:
    return static_cast<const IROpaqueType*>(T)->getName().str();
  }
  return "<unknown>";
}

static const char* opcodeToText(Opcode Op) {
  switch (Op) {
  case Opcode::Ret: return "ret";
  case Opcode::Br: return "br";
  case Opcode::CondBr: return "condbr";
  case Opcode::Switch: return "switch";
  case Opcode::Invoke: return "invoke";
  case Opcode::Unreachable: return "unreachable";
  case Opcode::Resume: return "resume";
  case Opcode::Add: return "add";
  case Opcode::Sub: return "sub";
  case Opcode::Mul: return "mul";
  case Opcode::UDiv: return "udiv";
  case Opcode::SDiv: return "sdiv";
  case Opcode::URem: return "urem";
  case Opcode::SRem: return "srem";
  case Opcode::FAdd: return "fadd";
  case Opcode::FSub: return "fsub";
  case Opcode::FMul: return "fmul";
  case Opcode::FDiv: return "fdiv";
  case Opcode::FRem: return "frem";
  case Opcode::Shl: return "shl";
  case Opcode::LShr: return "lshr";
  case Opcode::AShr: return "ashr";
  case Opcode::And: return "and";
  case Opcode::Or: return "or";
  case Opcode::Xor: return "xor";
  case Opcode::Alloca: return "alloca";
  case Opcode::Load: return "load";
  case Opcode::Store: return "store";
  case Opcode::GEP: return "gep";
  case Opcode::Memcpy: return "memcpy";
  case Opcode::Memset: return "memset";
  case Opcode::Trunc: return "trunc";
  case Opcode::ZExt: return "zext";
  case Opcode::SExt: return "sext";
  case Opcode::FPTrunc: return "fptrunc";
  case Opcode::FPExt: return "fpext";
  case Opcode::FPToSI: return "fptosi";
  case Opcode::FPToUI: return "fptoui";
  case Opcode::SIToFP: return "sitofp";
  case Opcode::UIToFP: return "uitofp";
  case Opcode::PtrToInt: return "ptrtoint";
  case Opcode::IntToPtr: return "inttoptr";
  case Opcode::BitCast: return "bitcast";
  case Opcode::ICmp: return "icmp";
  case Opcode::FCmp: return "fcmp";
  case Opcode::Call: return "call";
  case Opcode::Phi: return "phi";
  case Opcode::Select: return "select";
  case Opcode::ExtractValue: return "extractvalue";
  case Opcode::InsertValue: return "insertvalue";
  default: return "unknown";
  }
}

static const char* icmpPredToText(ICmpPred P) {
  switch (P) {
  case ICmpPred::EQ:  return "eq";
  case ICmpPred::NE:  return "ne";
  case ICmpPred::UGT: return "ugt";
  case ICmpPred::UGE: return "uge";
  case ICmpPred::ULT: return "ult";
  case ICmpPred::ULE: return "ule";
  case ICmpPred::SGT: return "sgt";
  case ICmpPred::SGE: return "sge";
  case ICmpPred::SLT: return "slt";
  case ICmpPred::SLE: return "sle";
  }
  return "unknown";
}

static const char* fcmpPredToText(FCmpPred P) {
  switch (P) {
  case FCmpPred::False: return "false";
  case FCmpPred::OEQ:   return "oeq";
  case FCmpPred::OGT:   return "ogt";
  case FCmpPred::OGE:   return "oge";
  case FCmpPred::OLT:   return "olt";
  case FCmpPred::OLE:   return "ole";
  case FCmpPred::ONE:   return "one";
  case FCmpPred::ORD:   return "ord";
  case FCmpPred::UNO:   return "uno";
  case FCmpPred::UEQ:   return "ueq";
  case FCmpPred::UGT:   return "ugt";
  case FCmpPred::UGE:   return "uge";
  case FCmpPred::ULT:   return "ult";
  case FCmpPred::ULE:   return "ule";
  case FCmpPred::UNE:   return "une";
  case FCmpPred::True:  return "true";
  }
  return "unknown";
}

static std::string valueToString(const IRValue* V) {
  if (!V) return "<null>";
  switch (V->getValueKind()) {
  case ValueKind::ConstantInt: {
    auto* CI = static_cast<const IRConstantInt*>(V);
    return std::to_string(CI->getZExtValue());
  }
  case ValueKind::ConstantFloat: {
    auto* CF = static_cast<const IRConstantFP*>(V);
    return CF->getValue().toString();
  }
  case ValueKind::ConstantNull:
    return "null";
  case ValueKind::ConstantUndef:
    return "undef";
  case ValueKind::ConstantAggregateZero:
    return "zeroinitializer";
  case ValueKind::ConstantFunctionRef: {
    auto* FR = static_cast<const IRConstantFunctionRef*>(V);
    return "@" + FR->getFunction()->getName().str();
  }
  case ValueKind::ConstantGlobalRef: {
    auto* GR = static_cast<const IRConstantGlobalRef*>(V);
    return "@" + GR->getGlobal()->getName().str();
  }
  case ValueKind::InstructionResult:
  case ValueKind::Argument: {
    auto N = V->getName();
    if (!N.empty()) return "%" + N.str();
    return "%" + std::to_string(V->getValueID());
  }
  case ValueKind::BasicBlockRef: {
    auto* BBR = static_cast<const IRBasicBlockRef*>(V);
    auto N = BBR->getBasicBlock()->getName();
    return "%" + N.str();
  }
  case ValueKind::ConstantStruct:
  case ValueKind::ConstantArray:
    return "<aggregate>";
  }
  return "<unknown_value>";
}

// ============================================================================
// IRWriter::writeText
// ============================================================================

static void writeInstructionText(const IRInstruction& I, raw_ostream& OS,
                                  const IRFunction& F) {
  auto Op = I.getOpcode();
  bool hasResult = !I.getType()->isVoid();

  // result assignment
  if (hasResult) {
    auto N = I.getName();
    if (!N.empty()) OS << "%" << N << " = ";
  }

  // opcode-specific formatting
  auto writeTypedBinOp = [&](const char* Name) {
    OS << Name << " " << typeToString(I.getType()) << " "
       << valueToString(I.getOperand(0)) << ", " << valueToString(I.getOperand(1));
  };

  switch (Op) {
  case Opcode::Ret:
    if (I.getNumOperands() == 0) {
      OS << "ret void";
    } else {
      OS << "ret " << typeToString(I.getOperand(0)->getType()) << " " << valueToString(I.getOperand(0));
    }
    break;
  case Opcode::Br:
    OS << "br label %" << static_cast<const IRBasicBlockRef*>(I.getOperand(0))->getBasicBlock()->getName();
    break;
  case Opcode::CondBr:
    OS << "br i1 " << valueToString(I.getOperand(0))
       << ", label %" << static_cast<const IRBasicBlockRef*>(I.getOperand(1))->getBasicBlock()->getName()
       << ", label %" << static_cast<const IRBasicBlockRef*>(I.getOperand(2))->getBasicBlock()->getName();
    break;
  case Opcode::Unreachable:
    OS << "unreachable";
    break;
  // Integer binary ops
  case Opcode::Add: case Opcode::Sub: case Opcode::Mul:
  case Opcode::UDiv: case Opcode::SDiv: case Opcode::URem: case Opcode::SRem:
    writeTypedBinOp(opcodeToText(Op));
    break;
  // Float binary ops
  case Opcode::FAdd: case Opcode::FSub: case Opcode::FMul:
  case Opcode::FDiv: case Opcode::FRem:
    writeTypedBinOp(opcodeToText(Op));
    break;
  // Bitwise ops
  case Opcode::Shl: case Opcode::LShr: case Opcode::AShr:
  case Opcode::And: case Opcode::Or: case Opcode::Xor:
    writeTypedBinOp(opcodeToText(Op));
    break;
  case Opcode::Alloca:
    OS << "alloca " << typeToString(I.getType());
    break;
  case Opcode::Load:
    OS << "load " << typeToString(I.getType()) << ", "
       << typeToString(I.getOperand(0)->getType()) << " " << valueToString(I.getOperand(0));
    break;
  case Opcode::Store:
    OS << "store " << typeToString(I.getOperand(0)->getType()) << " " << valueToString(I.getOperand(0))
       << ", " << typeToString(I.getOperand(1)->getType()) << " " << valueToString(I.getOperand(1));
    break;
  case Opcode::GEP:
    OS << "gep " << typeToString(I.getOperand(0)->getType());
    for (unsigned i = 1; i < I.getNumOperands(); ++i)
      OS << ", " << typeToString(I.getOperand(i)->getType()) << " " << valueToString(I.getOperand(i));
    break;
  // Cast ops
  case Opcode::Trunc: case Opcode::ZExt: case Opcode::SExt:
  case Opcode::FPTrunc: case Opcode::FPExt:
  case Opcode::FPToSI: case Opcode::FPToUI:
  case Opcode::SIToFP: case Opcode::UIToFP:
  case Opcode::PtrToInt: case Opcode::IntToPtr: case Opcode::BitCast:
    OS << opcodeToText(Op) << " " << typeToString(I.getOperand(0)->getType())
       << " " << valueToString(I.getOperand(0)) << " to " << typeToString(I.getType());
    break;
  case Opcode::ICmp:
    // TODO: IRInstruction does not store the predicate — createICmp/FCmp
    // accept a Pred parameter but discard it. Once IRInstruction gains
    // predicate support, use icmpPredToText() here.
    OS << "icmp eq " << typeToString(I.getOperand(0)->getType()) << " "
       << valueToString(I.getOperand(0)) << ", " << valueToString(I.getOperand(1));
    break;
  case Opcode::FCmp:
    // TODO: Same as ICmp — use fcmpPredToText() once predicates are stored.
    OS << "fcmp oeq " << typeToString(I.getOperand(0)->getType()) << " "
       << valueToString(I.getOperand(0)) << ", " << valueToString(I.getOperand(1));
    break;
  case Opcode::Call: {
    auto* O0 = I.getOperand(0);
    if (O0 && O0->getValueKind() == ValueKind::ConstantFunctionRef) {
      auto* Callee = static_cast<const IRConstantFunctionRef*>(O0)->getFunction();
      OS << "call " << typeToString(I.getType()) << " @" << Callee->getName() << "(";
      for (unsigned i = 1; i < I.getNumOperands(); ++i) {
        if (i > 1) OS << ", ";
        OS << typeToString(I.getOperand(i)->getType()) << " " << valueToString(I.getOperand(i));
      }
      OS << ")";
    } else {
      OS << "call <unknown>";
    }
    break;
  }
  case Opcode::Phi:
    OS << "phi " << typeToString(I.getType());
    // Phi operands come in pairs: [value, BB]
    for (unsigned i = 0; i + 1 < I.getNumOperands(); i += 2) {
      if (i > 0) OS << ",";
      OS << " [" << valueToString(I.getOperand(i)) << ", "
         << valueToString(I.getOperand(i + 1)) << "]";
    }
    break;
  case Opcode::Select:
    OS << "select i1 " << valueToString(I.getOperand(0))
       << ", " << typeToString(I.getOperand(1)->getType()) << " " << valueToString(I.getOperand(1))
       << ", " << typeToString(I.getOperand(2)->getType()) << " " << valueToString(I.getOperand(2));
    break;
  default:
    OS << opcodeToText(Op);
    for (unsigned i = 0; i < I.getNumOperands(); ++i) {
      if (i > 0) OS << ", ";
      OS << valueToString(I.getOperand(i));
    }
    break;
  }
}

bool IRWriter::writeText(const IRModule& M, raw_ostream& OS) {
  auto& MutM = const_cast<IRModule&>(M);
  // module header
  OS << "module \"" << M.getName() << "\"";
  if (!M.getTargetTriple().empty())
    OS << " target \"" << M.getTargetTriple() << "\"";
  OS << "\n\n";

  // globals
  for (auto& GV : MutM.getGlobals()) {
    OS << "global @" << GV->getName() << " : " << typeToString(GV->getType());
    if (GV->hasInitializer()) {
      OS << " = " << valueToString(GV->getInitializer());
    }
    OS << "\n";
  }
  if (MutM.getGlobals().size() > 0) OS << "\n";

  // functions
  for (auto& F : MutM.getFunctions()) {
    if (F->isDeclaration()) {
      OS << "declare " << typeToString(F->getReturnType()) << " @" << F->getName()
         << "(";
      auto* FT = F->getFunctionType();
      for (unsigned i = 0; i < FT->getNumParams(); ++i) {
        if (i > 0) OS << ", ";
        OS << typeToString(FT->getParamType(i));
      }
      OS << ")\n\n";
      continue;
    }

    // function definition
    OS << "function " << typeToString(F->getReturnType()) << " @" << F->getName() << "(";
    auto* FT = F->getFunctionType();
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
      if (i > 0) OS << ", ";
      auto* Arg = F->getArg(i);
      OS << typeToString(Arg->getType());
      if (!Arg->getName().empty()) OS << " %" << Arg->getName();
    }
    OS << ") {\n";

    for (auto& BB : F->getBasicBlocks()) {
      OS << BB->getName() << ":\n";
      for (auto& I : BB->getInstList()) {
        OS << "  ";
        writeInstructionText(*I, OS, *F);
        OS << "\n";
      }
    }
    OS << "}\n\n";
  }

  return true;
}

// ============================================================================
// Binary helpers
// ============================================================================

static void writeLE16(std::string& S, uint16_t V) {
  S.push_back(static_cast<char>(V & 0xFF));
  S.push_back(static_cast<char>((V >> 8) & 0xFF));
}
static void writeLE32(std::string& S, uint32_t V) {
  S.push_back(static_cast<char>(V & 0xFF));
  S.push_back(static_cast<char>((V >> 8) & 0xFF));
  S.push_back(static_cast<char>((V >> 16) & 0xFF));
  S.push_back(static_cast<char>((V >> 24) & 0xFF));
}
static void writeLE64(std::string& S, uint64_t V) {
  for (int i = 0; i < 8; ++i)
    S.push_back(static_cast<char>((V >> (i * 8)) & 0xFF));
}

static uint16_t readLE16(const char* D) {
  return static_cast<uint16_t>(static_cast<uint8_t>(D[0])) |
         (static_cast<uint16_t>(static_cast<uint8_t>(D[1])) << 8);
}
static uint32_t readLE32(const char* D) {
  return static_cast<uint32_t>(static_cast<uint8_t>(D[0])) |
         (static_cast<uint32_t>(static_cast<uint8_t>(D[1])) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(D[2])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(D[3])) << 24);
}
static uint64_t readLE64(const char* D) {
  uint64_t V = 0;
  for (int i = 0; i < 8; ++i)
    V |= static_cast<uint64_t>(static_cast<uint8_t>(D[i])) << (i * 8);
  return V;
}

// ============================================================================
// StringTable builder
// ============================================================================

class StringWriter {
  std::vector<std::string> Strings;
  std::unordered_map<std::string, uint32_t> IndexMap;
  std::vector<uint32_t> Offsets; // byte offset of each string's data in the table
  uint32_t CurOff = 4; // start after NumStrings field

  void addInternal(StringRef S) {
    uint32_t Idx = static_cast<uint32_t>(Strings.size());
    Offsets.push_back(CurOff + 4); // +4 to skip the Length prefix
    Strings.push_back(S.str());
    IndexMap[S.str()] = Idx;
    CurOff += 4 + static_cast<uint32_t>(S.size());
  }
public:
  uint32_t add(StringRef S) {
    auto It = IndexMap.find(S.str());
    if (It != IndexMap.end()) return It->second;
    addInternal(S);
    return IndexMap[S.str()];
  }
  // Returns (ByteOffset, Length) — Offset is byte position of string data
  // within the serialized string table (relative to table start).
  std::pair<uint32_t, uint32_t> getRef(StringRef S) {
    uint32_t Idx = add(S);
    return {Offsets[Idx], static_cast<uint32_t>(S.size())};
  }
  void writeTo(std::string& Out) const {
    writeLE32(Out, static_cast<uint32_t>(Strings.size()));
    for (auto& S : Strings) {
      writeLE32(Out, static_cast<uint32_t>(S.size()));
      Out.append(S);
    }
  }
};

// ============================================================================
// Binary type encoding
// ============================================================================

static void writeTypeRecord(std::string& Out, const IRType* T, StringWriter& SW) {
  if (!T) { Out.push_back(static_cast<char>(IRType::Void)); return; }
  Out.push_back(static_cast<char>(T->getKind()));
  switch (T->getKind()) {
  case IRType::Void: case IRType::Bool:
    break;
  case IRType::Integer:
    writeLE32(Out, static_cast<const IRIntegerType*>(T)->getBitWidth());
    break;
  case IRType::Float:
    writeLE32(Out, static_cast<const IRFloatType*>(T)->getBitWidth());
    break;
  case IRType::Pointer: {
    auto* PT = static_cast<const IRPointerType*>(T);
    writeTypeRecord(Out, PT->getPointeeType(), SW);
    writeLE32(Out, PT->getAddressSpace());
    break;
  }
  case IRType::Array: {
    auto* AT = static_cast<const IRArrayType*>(T);
    writeLE64(Out, AT->getNumElements());
    writeTypeRecord(Out, AT->getElementType(), SW);
    break;
  }
  case IRType::Struct: {
    auto* ST = static_cast<const IRStructType*>(T);
    auto Ref = SW.getRef(ST->getName());
    writeLE32(Out, Ref.first);
    writeLE32(Out, Ref.second);
    writeLE32(Out, ST->getNumFields());
    for (unsigned i = 0; i < ST->getNumFields(); ++i)
      writeTypeRecord(Out, ST->getFieldType(i), SW);
    Out.push_back(ST->isPacked() ? 1 : 0);
    break;
  }
  case IRType::Function: {
    auto* FT = static_cast<const IRFunctionType*>(T);
    writeTypeRecord(Out, FT->getReturnType(), SW);
    Out.push_back(FT->isVarArg() ? 1 : 0);
    writeLE32(Out, FT->getNumParams());
    for (unsigned i = 0; i < FT->getNumParams(); ++i)
      writeTypeRecord(Out, FT->getParamType(i), SW);
    break;
  }
  case IRType::Vector: {
    auto* VT = static_cast<const IRVectorType*>(T);
    writeLE32(Out, VT->getNumElements());
    writeTypeRecord(Out, VT->getElementType(), SW);
    break;
  }
  case IRType::Opaque: {
    auto Ref = SW.getRef(static_cast<const IROpaqueType*>(T)->getName());
    writeLE32(Out, Ref.first);
    writeLE32(Out, Ref.second);
    break;
  }
  }
}

static void writeConstantRecord(std::string& Out, const IRConstant* C, StringWriter& SW) {
  if (!C) {
    Out.push_back(static_cast<char>(ValueKind::ConstantNull));
    writeTypeRecord(Out, nullptr, SW);
    return;
  }
  Out.push_back(static_cast<char>(C->getValueKind()));
  switch (C->getValueKind()) {
  case ValueKind::ConstantInt: {
    auto* CI = static_cast<const IRConstantInt*>(C);
    writeLE32(Out, CI->getType() ? static_cast<const IRIntegerType*>(CI->getType())->getBitWidth() : 32);
    writeLE64(Out, CI->getZExtValue());
    break;
  }
  case ValueKind::ConstantFloat: {
    auto* CF = static_cast<const IRConstantFP*>(C);
    unsigned BW = static_cast<const IRFloatType*>(CF->getType())->getBitWidth();
    writeLE32(Out, BW);
    auto Bits = CF->getValue().bitcastToAPInt();
    writeLE64(Out, Bits.getZExtValue());
    break;
  }
  case ValueKind::ConstantNull:
    writeTypeRecord(Out, C->getType(), SW);
    break;
  case ValueKind::ConstantUndef:
    writeTypeRecord(Out, C->getType(), SW);
    break;
  case ValueKind::ConstantAggregateZero:
    writeTypeRecord(Out, C->getType(), SW);
    break;
  case ValueKind::ConstantStruct: {
    auto* CS = static_cast<const IRConstantStruct*>(C);
    writeTypeRecord(Out, CS->getType(), SW);
    writeLE32(Out, static_cast<uint32_t>(CS->getElements().size()));
    for (auto* E : CS->getElements())
      writeConstantRecord(Out, E, SW);
    break;
  }
  case ValueKind::ConstantArray: {
    auto* CA = static_cast<const IRConstantArray*>(C);
    writeTypeRecord(Out, CA->getType(), SW);
    writeLE32(Out, static_cast<uint32_t>(CA->getElements().size()));
    for (auto* E : CA->getElements())
      writeConstantRecord(Out, E, SW);
    break;
  }
  default:
    break;
  }
}

// ============================================================================
// IRWriter::writeBitcode
// ============================================================================

bool IRWriter::writeBitcode(const IRModule& M, raw_ostream& OS) {
  auto& MutM = const_cast<IRModule&>(M);
  StringWriter SW;

  // Pre-register all strings
  SW.add(M.getName());
  for (auto& F : MutM.getFunctions()) {
    SW.add(F->getName());
    for (auto& BB : F->getBasicBlocks()) {
      SW.add(BB->getName());
      for (auto& I : BB->getInstList())
        if (!I->getName().empty()) SW.add(I->getName());
    }
  }
  for (auto& GV : MutM.getGlobals())
    SW.add(GV->getName());

  // Build module data
  std::string ModuleData;

  // Module name StringRef at start of module data
  auto ModNameRef = SW.getRef(M.getName());
  writeLE32(ModuleData, ModNameRef.first);
  writeLE32(ModuleData, ModNameRef.second);

  // GlobalSection
  auto& Globals = MutM.getGlobals();
  writeLE32(ModuleData, static_cast<uint32_t>(Globals.size()));
  for (auto& GV : Globals) {
    auto Ref = SW.getRef(GV->getName());
    writeLE32(ModuleData, Ref.first);
    writeLE32(ModuleData, Ref.second);
    writeTypeRecord(ModuleData, GV->getType(), SW);
    ModuleData.push_back(GV->isConstant() ? 1 : 0);
    ModuleData.push_back(GV->hasInitializer() ? 1 : 0);
    if (GV->hasInitializer())
      writeConstantRecord(ModuleData, GV->getInitializer(), SW);
    writeLE32(ModuleData, GV->getAlignment());
    writeLE32(ModuleData, GV->getAddressSpace());
    ModuleData.push_back(static_cast<char>(GV->getLinkage()));
  }

  // FunctionDeclSection — separate declarations from definitions
  std::vector<IRFunction*> Decls, Defs;
  for (auto& F : MutM.getFunctions()) {
    if (F->isDeclaration()) Decls.push_back(F.get());
    else Defs.push_back(F.get());
  }

  writeLE32(ModuleData, static_cast<uint32_t>(Decls.size()));
  for (auto* F : Decls) {
    auto Ref = SW.getRef(F->getName());
    writeLE32(ModuleData, Ref.first);
    writeLE32(ModuleData, Ref.second);
    writeTypeRecord(ModuleData, F->getFunctionType(), SW);
    ModuleData.push_back(static_cast<char>(F->getLinkage()));
    ModuleData.push_back(static_cast<char>(F->getCallingConv()));
  }

  // FunctionSection (definitions)
  writeLE32(ModuleData, static_cast<uint32_t>(Defs.size()));
  for (auto* F : Defs) {
    auto Ref = SW.getRef(F->getName());
    writeLE32(ModuleData, Ref.first);
    writeLE32(ModuleData, Ref.second);
    writeTypeRecord(ModuleData, F->getFunctionType(), SW);
    ModuleData.push_back(static_cast<char>(F->getLinkage()));
    ModuleData.push_back(static_cast<char>(F->getCallingConv()));
    writeLE32(ModuleData, F->getAttributes());
    writeLE32(ModuleData, F->getNumBasicBlocks());
    for (auto& BB : F->getBasicBlocks()) {
      auto BBRef = SW.getRef(BB->getName());
      writeLE32(ModuleData, BBRef.first);
      writeLE32(ModuleData, BBRef.second);
      writeLE32(ModuleData, static_cast<uint32_t>(BB->getInstList().size()));
      for (auto& I : BB->getInstList()) {
        writeLE16(ModuleData, static_cast<uint16_t>(I->getOpcode()));
        ModuleData.push_back(static_cast<char>(I->getDialect()));
        writeTypeRecord(ModuleData, I->getType(), SW);
        auto IRef = SW.getRef(I->getName());
        writeLE32(ModuleData, IRef.first);
        writeLE32(ModuleData, IRef.second);
        writeLE32(ModuleData, I->getNumOperands());
        for (unsigned i = 0; i < I->getNumOperands(); ++i) {
          auto* Op = I->getOperand(i);
          if (!Op) {
            ModuleData.push_back(static_cast<char>(ValueKind::ConstantNull));
            writeTypeRecord(ModuleData, nullptr, SW);
          } else {
            ModuleData.push_back(static_cast<char>(Op->getValueKind()));
            switch (Op->getValueKind()) {
            case ValueKind::ConstantInt: {
              auto* CI = static_cast<const IRConstantInt*>(Op);
              writeLE32(ModuleData, static_cast<const IRIntegerType*>(CI->getType())->getBitWidth());
              writeLE64(ModuleData, CI->getZExtValue());
              break;
            }
            case ValueKind::ConstantFloat: {
              auto* CF = static_cast<const IRConstantFP*>(Op);
              writeLE32(ModuleData, static_cast<const IRFloatType*>(CF->getType())->getBitWidth());
              writeLE64(ModuleData, CF->getValue().bitcastToAPInt().getZExtValue());
              break;
            }
            case ValueKind::ConstantNull:
              writeTypeRecord(ModuleData, Op->getType(), SW);
              break;
            case ValueKind::ConstantUndef:
              writeTypeRecord(ModuleData, Op->getType(), SW);
              break;
            case ValueKind::ConstantAggregateZero:
              writeTypeRecord(ModuleData, Op->getType(), SW);
              break;
            case ValueKind::ConstantFunctionRef: {
              auto* FR = static_cast<const IRConstantFunctionRef*>(Op);
              // Find function index in the full function list
              uint32_t FIdx = 0;
              for (auto& FF : MutM.getFunctions()) {
                if (FF.get() == FR->getFunction()) break;
                ++FIdx;
              }
              writeLE32(ModuleData, FIdx);
              break;
            }
            case ValueKind::ConstantGlobalRef: {
              auto* GR = static_cast<const IRConstantGlobalRef*>(Op);
              uint32_t GIdx = 0;
              for (auto& GV : MutM.getGlobals()) {
                if (GV.get() == GR->getGlobal()) break;
                ++GIdx;
              }
              writeLE32(ModuleData, GIdx);
              break;
            }
            case ValueKind::InstructionResult: {
              // Find BB index and instruction index within the current function
              uint32_t BBIdx = 0, InstIdx = 0;
              for (auto& BB2 : F->getBasicBlocks()) {
                for (auto& I2 : BB2->getInstList()) {
                  if (I2.get() == Op) goto found_inst;
                  ++InstIdx;
                }
                ++BBIdx;
                InstIdx = 0;
              }
              found_inst:
              writeLE32(ModuleData, BBIdx);
              writeLE32(ModuleData, InstIdx);
              break;
            }
            case ValueKind::Argument: {
              // Arguments are not IRValues, so we won't encounter them as operands.
              // Write a placeholder.
              writeLE32(ModuleData, 0);
              break;
            }
            case ValueKind::BasicBlockRef: {
              auto* BBR = static_cast<const IRBasicBlockRef*>(Op);
              uint32_t BBIdx = 0;
              for (auto& BB2 : F->getBasicBlocks()) {
                if (BB2.get() == BBR->getBasicBlock()) break;
                ++BBIdx;
              }
              writeLE32(ModuleData, BBIdx);
              break;
            }
            default:
              break;
            }
          }
        }
      }
    }
  }

  // Build string table
  std::string StrTable;
  SW.writeTo(StrTable);

  // Build header
  IRFileHeader Header;
  std::memcpy(Header.Magic, "BTIR", 4);
  Header.Version = IRFormatVersion::Current();
  Header.Flags = 0;
  Header.ModuleOffset = sizeof(IRFileHeader); // 26
  Header.StringTableOffset = sizeof(IRFileHeader) + static_cast<uint32_t>(ModuleData.size());
  Header.StringTableSize = static_cast<uint32_t>(StrTable.size());

  // Write everything
  OS.write(reinterpret_cast<const char*>(&Header), sizeof(Header));
  OS.write(ModuleData.data(), ModuleData.size());
  OS.write(StrTable.data(), StrTable.size());

  return true;
}

// ============================================================================
// Text Parser
// ============================================================================

class TextParser {
  StringRef Src;
  IRTypeContext& TCtx;
  SerializationDiagnostic* Diag;
  unsigned Line = 1;
  unsigned Col = 1;
  size_t Pos = 0;

  void setError(SerializationErrorKind K, const std::string& Msg) {
    if (Diag) *Diag = SerializationDiagnostic(K, Msg, Line, Col);
  }

  char peek() const { return Pos < Src.size() ? Src[Pos] : '\0'; }
  char advance() {
    char C = Src[Pos++];
    if (C == '\n') { ++Line; Col = 1; } else { ++Col; }
    return C;
  }
  bool eof() const { return Pos >= Src.size(); }

  void skipWS() {
    while (!eof()) {
      if (Src[Pos] == ' ' || Src[Pos] == '\t' || Src[Pos] == '\r' || Src[Pos] == '\n') {
        advance();
      } else if (Src[Pos] == ';') {
        // comment to end of line
        while (!eof() && Src[Pos] != '\n') advance();
      } else {
        break;
      }
    }
  }

  bool expect(char C) {
    skipWS();
    if (peek() == C) { advance(); return true; }
    setError(SerializationErrorKind::InvalidFormat,
             std::string("Expected '") + C + "', got '" + peek() + "'");
    return false;
  }

  bool expectKeyword(StringRef KW) {
    skipWS();
    if (Src.slice(Pos).startswith(KW)) {
      for (size_t i = 0; i < KW.size(); ++i) advance();
      return true;
    }
    setError(SerializationErrorKind::InvalidFormat,
             "Expected '" + KW.str() + "'");
    return false;
  }

  std::string readIdent() {
    skipWS();
    std::string Result;
    while (!eof() && (isalnum(Src[Pos]) || Src[Pos] == '_' || Src[Pos] == '.'))
      Result.push_back(advance());
    return Result;
  }

  std::string readString() {
    skipWS();
    if (peek() != '"') return "";
    advance(); // skip opening "
    std::string Result;
    while (!eof() && peek() != '"') Result.push_back(advance());
    if (!eof()) advance(); // skip closing "
    return Result;
  }

  uint64_t readDecimal() {
    skipWS();
    uint64_t V = 0;
    while (!eof() && isdigit(Src[Pos])) {
      V = V * 10 + (advance() - '0');
    }
    return V;
  }

  bool checkKeyword(StringRef KW) {
    skipWS();
    return Src.slice(Pos).startswith(KW);
  }

  IRType* parseType() {
    skipWS();
    // Check for primitive types
    if (Src.slice(Pos).startswith("void")) {
      Pos += 4; Col += 4;
      return TCtx.getVoidType();
    }
    if (Src[Pos] == 'i') {
      Pos++; Col++;
      unsigned BW = static_cast<unsigned>(readDecimal());
      return TCtx.getIntType(BW);
    }
    if (Src[Pos] == 'f') {
      Pos++; Col++;
      unsigned BW = static_cast<unsigned>(readDecimal());
      return TCtx.getFloatType(BW);
    }
    // Pointer type: <T>*
    if (Src[Pos] == '[') {
      // Array type: [N x <T>]
      advance();
      uint64_t N = readDecimal();
      skipWS();
      // expect 'x'
      if (peek() == 'x') advance();
      skipWS();
      auto* ElemTy = parseType();
      skipWS();
      if (peek() == ']') advance();
      return TCtx.getArrayType(ElemTy, N);
    }
    if (Src[Pos] == '<') {
      // Vector type: <N x <T>>
      advance();
      uint64_t N = readDecimal();
      skipWS();
      if (peek() == 'x') advance();
      skipWS();
      auto* ElemTy = parseType();
      skipWS();
      if (peek() == '>') advance();
      return TCtx.getVectorType(ElemTy, static_cast<unsigned>(N));
    }
    if (Src.slice(Pos).startswith("struct")) {
      Pos += 6; Col += 6;
      skipWS();
      if (peek() == '{') {
        advance();
        SmallVector<IRType*, 16> Fields;
        skipWS();
        while (peek() != '}' && !eof()) {
          Fields.push_back(parseType());
          skipWS();
          if (peek() == ',') advance();
          skipWS();
        }
        if (peek() == '}') advance();
        return TCtx.getAnonStructType(std::move(Fields));
      }
      // named struct
      auto Name = readIdent();
      auto* ST = TCtx.getStructTypeByName(Name);
      if (!ST) {
        // create opaque if not exists
        ST = TCtx.getStructType(Name, {});
      }
      return ST;
    }
    // Check for function type: <Ret> (...)  — harder to detect, skip for basic types
    // Check for pointer suffix
    auto* BaseTy = parseBaseOrNamedType();
    skipWS();
    while (peek() == '*') {
      advance();
      BaseTy = TCtx.getPointerType(BaseTy);
      skipWS();
    }
    // Check for function type suffix: (params)
    if (peek() == '(') {
      advance();
      SmallVector<IRType*, 8> Params;
      bool IsVarArg = false;
      skipWS();
      if (peek() != ')') {
        Params.push_back(parseType());
        skipWS();
        while (peek() == ',') {
          advance();
          skipWS();
          if (Src.slice(Pos).startswith("...")) {
            Pos += 3; Col += 3;
            IsVarArg = true;
            break;
          }
          Params.push_back(parseType());
          skipWS();
        }
      }
      if (peek() == ')') advance();
      BaseTy = TCtx.getFunctionType(BaseTy, std::move(Params), IsVarArg);
      skipWS();
      while (peek() == '*') {
        advance();
        BaseTy = TCtx.getPointerType(BaseTy);
        skipWS();
      }
    }
    return BaseTy;
  }

  IRType* parseBaseOrNamedType() {
    skipWS();
    if (Src.slice(Pos).startswith("void")) { Pos += 4; Col += 4; return TCtx.getVoidType(); }
    if (Src[Pos] == 'i') { Pos++; Col++; return TCtx.getIntType(static_cast<unsigned>(readDecimal())); }
    if (Src[Pos] == 'f') { Pos++; Col++; return TCtx.getFloatType(static_cast<unsigned>(readDecimal())); }
    if (Src[Pos] == '[') {
      advance();
      uint64_t N = readDecimal();
      skipWS(); if (peek() == 'x') advance();
      auto* E = parseType();
      skipWS(); if (peek() == ']') advance();
      return TCtx.getArrayType(E, N);
    }
    if (Src[Pos] == '<') {
      advance();
      uint64_t N = readDecimal();
      skipWS(); if (peek() == 'x') advance();
      auto* E = parseType();
      skipWS(); if (peek() == '>') advance();
      return TCtx.getVectorType(E, static_cast<unsigned>(N));
    }
    // named type
    auto Name = readIdent();
    if (Name.empty()) return nullptr;
    if (auto* ST = TCtx.getStructTypeByName(Name)) return ST;
    if (auto* OT = TCtx.getOpaqueTypeByName(Name)) return OT;
    return TCtx.getOpaqueType(Name);
  }

  // Parse a value reference — returns (IRValue* or nullptr, is_bb_ref)
  // valueMap maps "%name" -> IRValue*
  std::pair<IRValue*, bool> parseValueRef(
      const std::unordered_map<std::string, IRValue*>& ValMap,
      const std::unordered_map<std::string, IRBasicBlock*>& BBMap,
      IRFunction* CurFunc) {
    skipWS();
    if (peek() == '@') {
      // function or global ref — just skip for now
      advance();
      auto Name = readIdent();
      // can't resolve here, return nullptr
      return {nullptr, false};
    }
    if (peek() == '%') {
      advance();
      auto Name = readIdent();
      std::string Key = "%" + Name;
      // Check BB map first
      auto BBIt = BBMap.find(Key);
      if (BBIt != BBMap.end()) {
        // Return as BasicBlockRef
        auto* BBR = new IRBasicBlockRef(BBIt->second);
        return {BBR, true};
      }
      auto It = ValMap.find(Key);
      if (It != ValMap.end()) return {It->second, false};
      // Try as argument
      if (CurFunc) {
        for (unsigned a = 0; a < CurFunc->getNumArgs(); ++a) {
          auto* Arg = CurFunc->getArg(a);
          if (Arg->getName() == Name) {
            return {nullptr, false}; // Arguments aren't IRValue*
          }
        }
      }
      return {nullptr, false};
    }
    // Try to read as integer constant
    if (isdigit(peek())) {
      auto Val = readDecimal();
      auto* CI = new IRConstantInt(TCtx.getInt32Ty(), Val);
      return {CI, false};
    }
    // Keywords
    if (Src.slice(Pos).startswith("null")) { Pos += 4; Col += 4; return {new IRConstantNull(TCtx.getInt32Ty()), false}; }
    if (Src.slice(Pos).startswith("undef")) { Pos += 5; Col += 5; return {new IRConstantUndef(TCtx.getInt32Ty()), false}; }
    if (Src.slice(Pos).startswith("zeroinitializer")) { Pos += 16; Col += 16; return {new IRConstantAggregateZero(TCtx.getInt32Ty()), false}; }
    if (Src.slice(Pos).startswith("true")) { Pos += 4; Col += 4; return {new IRConstantInt(TCtx.getInt1Ty(), 1), false}; }
    if (Src.slice(Pos).startswith("false")) { Pos += 5; Col += 5; return {new IRConstantInt(TCtx.getInt1Ty(), 0), false}; }
    return {nullptr, false};
  }

  Opcode textToOpcode(const std::string& S) {
    if (S == "ret") return Opcode::Ret;
    if (S == "br") return Opcode::Br;
    if (S == "condbr") return Opcode::CondBr;
    if (S == "unreachable") return Opcode::Unreachable;
    if (S == "add") return Opcode::Add;
    if (S == "sub") return Opcode::Sub;
    if (S == "mul") return Opcode::Mul;
    if (S == "udiv") return Opcode::UDiv;
    if (S == "sdiv") return Opcode::SDiv;
    if (S == "urem") return Opcode::URem;
    if (S == "srem") return Opcode::SRem;
    if (S == "fadd") return Opcode::FAdd;
    if (S == "fsub") return Opcode::FSub;
    if (S == "fmul") return Opcode::FMul;
    if (S == "fdiv") return Opcode::FDiv;
    if (S == "frem") return Opcode::FRem;
    if (S == "shl") return Opcode::Shl;
    if (S == "lshr") return Opcode::LShr;
    if (S == "ashr") return Opcode::AShr;
    if (S == "and") return Opcode::And;
    if (S == "or") return Opcode::Or;
    if (S == "xor") return Opcode::Xor;
    if (S == "alloca") return Opcode::Alloca;
    if (S == "load") return Opcode::Load;
    if (S == "store") return Opcode::Store;
    if (S == "gep") return Opcode::GEP;
    if (S == "trunc") return Opcode::Trunc;
    if (S == "zext") return Opcode::ZExt;
    if (S == "sext") return Opcode::SExt;
    if (S == "fptrunc") return Opcode::FPTrunc;
    if (S == "fpext") return Opcode::FPExt;
    if (S == "fptosi") return Opcode::FPToSI;
    if (S == "fptoui") return Opcode::FPToUI;
    if (S == "sitofp") return Opcode::SIToFP;
    if (S == "uitofp") return Opcode::UIToFP;
    if (S == "ptrtoint") return Opcode::PtrToInt;
    if (S == "inttoptr") return Opcode::IntToPtr;
    if (S == "bitcast") return Opcode::BitCast;
    if (S == "icmp") return Opcode::ICmp;
    if (S == "fcmp") return Opcode::FCmp;
    if (S == "call") return Opcode::Call;
    if (S == "phi") return Opcode::Phi;
    if (S == "select") return Opcode::Select;
    if (S == "extractvalue") return Opcode::ExtractValue;
    if (S == "insertvalue") return Opcode::InsertValue;
    return Opcode::Ret; // fallback
  }

public:
  TextParser(StringRef T, IRTypeContext& C, SerializationDiagnostic* D)
    : Src(T), TCtx(C), Diag(D) {}

  std::unique_ptr<IRModule> parse() {
    skipWS();

    // module header: module "name" [target "triple"]
    if (!expectKeyword("module")) return nullptr;
    skipWS();
    auto ModName = readString();
    skipWS();

    StringRef TargetTriple;
    if (checkKeyword("target")) {
      Pos += 6; Col += 6;
      skipWS();
      auto TT = readString();
      TargetTriple = TT;
    }

    auto M = std::make_unique<IRModule>(ModName, TCtx, TargetTriple);

    // Parse body
    while (!eof()) {
      skipWS();
      if (eof()) break;

      if (checkKeyword("global")) {
        Pos += 6; Col += 6;
        skipWS();
        if (peek() == '@') advance();
        auto GVName = readIdent();
        skipWS();
        if (peek() == ':') advance();
        skipWS();
        auto* GVType = parseType();
        auto* GV = M->getOrInsertGlobal(GVName, GVType);
        skipWS();
        if (peek() == '=') {
          advance();
          skipWS();
          // skip initializer value (we don't reconstruct it for now)
          // Read the constant value text
          auto Val = readIdent(); // e.g. "42", "null", etc
          (void)Val;
        }
      } else if (checkKeyword("declare")) {
        Pos += 7; Col += 7;
        skipWS();
        auto* RetTy = parseType();
        skipWS();
        if (peek() == '@') advance();
        auto FName = readIdent();
        skipWS();
        if (peek() == '(') advance();
        SmallVector<IRType*, 8> ParamTypes;
        skipWS();
        while (peek() != ')' && !eof()) {
          ParamTypes.push_back(parseType());
          skipWS();
          if (peek() == ',') advance();
          skipWS();
        }
        if (peek() == ')') advance();
        auto* FT = TCtx.getFunctionType(RetTy, std::move(ParamTypes));
        M->getOrInsertFunction(FName, FT);
      } else if (checkKeyword("function")) {
        Pos += 8; Col += 8;
        skipWS();
        auto* RetTy = parseType();
        skipWS();
        if (peek() == '@') advance();
        auto FName = readIdent();
        skipWS();
        if (peek() == '(') advance();
        SmallVector<IRType*, 8> ParamTypes;
        SmallVector<std::string, 8> ParamNames;
        skipWS();
        while (peek() != ')' && !eof()) {
          auto* PT = parseType();
          ParamTypes.push_back(PT);
          skipWS();
          // optional param name: %name
          std::string PName;
          if (peek() == '%') {
            advance();
            PName = readIdent();
          }
          ParamNames.push_back(PName);
          skipWS();
          if (peek() == ',') advance();
          skipWS();
        }
        if (peek() == ')') advance();
        skipWS();

        auto* FT = TCtx.getFunctionType(RetTy, std::move(ParamTypes));
        auto* F = M->getOrInsertFunction(FName, FT);

        // Build value map and BB map for resolving references
        std::unordered_map<std::string, IRValue*> ValMap;
        std::unordered_map<std::string, IRBasicBlock*> BBMap;

        // Save position before body for two-pass parse
        auto SavedPos = Pos;
        auto SavedLine = Line;
        auto SavedCol = Col;

        // First pass: just collect BB names
        expect('{');
        while (!eof()) {
          skipWS();
          if (peek() == '}') break;
          // Read BB label
          auto BBName = readIdent();
          if (BBName.empty()) break;
          skipWS();
          if (peek() == ':') advance();
          // Skip all instructions in this BB (until next label or })
          while (!eof()) {
            skipWS();
            if (peek() == '}' || peek() == '\0') break;
            // Check if this looks like a BB label (IDENT followed by :)
            auto CheckPos = Pos;
            auto CheckLine = Line;
            auto CheckCol = Col;
            std::string MaybeLabel;
            while (CheckPos < Src.size() && (isalnum(Src[CheckPos]) || Src[CheckPos] == '_' || Src[CheckPos] == '.')) {
              MaybeLabel.push_back(Src[CheckPos]);
              ++CheckPos;
            }
            // Skip whitespace
            while (CheckPos < Src.size() && (Src[CheckPos] == ' ' || Src[CheckPos] == '\t'))
              ++CheckPos;
            if (!MaybeLabel.empty() && CheckPos < Src.size() && Src[CheckPos] == ':') {
              // This is a BB label, stop current BB
              break;
            }
            // Not a label — skip rest of line
            while (!eof() && peek() != '\n') advance();
          }
        }
        if (peek() == '}') advance();

        // Second pass: create BBs and instructions
        Pos = SavedPos;
        Line = SavedLine;
        Col = SavedCol;

        expect('{');
        while (!eof()) {
          skipWS();
          if (peek() == '}') { advance(); break; }

          // BB label
          auto BBName = readIdent();
          if (BBName.empty()) break;
          skipWS();
          if (peek() == ':') advance();

          auto* BB = F->addBasicBlock(BBName);
          BBMap["%" + BBName] = BB;

          // Parse instructions until next BB label or }
          while (!eof()) {
            skipWS();
            if (peek() == '}' || peek() == '\0') break;

            // Check if this is a BB label
            {
              auto CheckPos = Pos;
              std::string MaybeLabel;
              while (CheckPos < Src.size() && (isalnum(Src[CheckPos]) || Src[CheckPos] == '_' || Src[CheckPos] == '.')) {
                MaybeLabel.push_back(Src[CheckPos]);
                ++CheckPos;
              }
              while (CheckPos < Src.size() && (Src[CheckPos] == ' ' || Src[CheckPos] == '\t'))
                ++CheckPos;
              if (!MaybeLabel.empty() && CheckPos < Src.size() && Src[CheckPos] == ':') {
                break; // It's a BB label, end of current BB's instructions
              }
            }

            // Parse instruction
            std::string InstName;
            bool HasResult = false;
            if (peek() == '%') {
              auto SaveP = Pos;
              advance();
              auto Name = readIdent();
              skipWS();
              if (peek() == '=') {
                advance();
                InstName = Name;
                HasResult = true;
              } else {
                // Not an assignment — could be %bbref as operand of br
                Pos = SaveP;
              }
            }

            skipWS();
            // Read opcode keyword
            auto OpStr = readIdent();
            auto Op = textToOpcode(OpStr);
            skipWS();

            // Create instruction based on opcode
            IRInstruction* Inst = nullptr;
            auto addBinaryInst = [&](IRType* Ty) {
              // read operand1 type + value, operand2 type + value
              auto* Ty1 = parseType();
              skipWS();
              auto [Op1, _] = parseValueRef(ValMap, BBMap, F);
              skipWS();
              if (peek() == ',') advance();
              skipWS();
              auto* Ty2 = parseType();
              skipWS();
              auto [Op2, __] = parseValueRef(ValMap, BBMap, F);
              auto I = std::make_unique<IRInstruction>(Op, Ty1, 0, dialect::DialectID::Core, InstName);
              if (Op1) I->addOperand(Op1);
              if (Op2) I->addOperand(Op2);
              Inst = BB->push_back(std::move(I));
            };

            switch (Op) {
            case Opcode::Ret: {
              skipWS();
              if (checkKeyword("void")) {
                Pos += 4; Col += 4;
                auto I = std::make_unique<IRInstruction>(Opcode::Ret, TCtx.getVoidType(), 0);
                Inst = BB->push_back(std::move(I));
              } else {
                auto* Ty = parseType();
                skipWS();
                auto [V, _] = parseValueRef(ValMap, BBMap, F);
                auto I = std::make_unique<IRInstruction>(Opcode::Ret, Ty, 0);
                if (V) I->addOperand(V);
                Inst = BB->push_back(std::move(I));
              }
              break;
            }
            case Opcode::Br: {
              skipWS();
              if (checkKeyword("label")) { Pos += 5; Col += 5; }
              skipWS();
              if (peek() == '%') advance();
              auto DestName = readIdent();
              skipWS();
              // Find or create BB ref
              IRBasicBlock* DestBB = BBMap["%" + DestName];
              auto I = std::make_unique<IRInstruction>(Opcode::Br, TCtx.getVoidType(), 0);
              if (DestBB) {
                auto* BBR = new IRBasicBlockRef(DestBB);
                I->addOperand(BBR);
              }
              Inst = BB->push_back(std::move(I));
              break;
            }
            case Opcode::CondBr: {
              // br i1 %cond, label %true, label %false
              skipWS();
              if (checkKeyword("i1")) { Pos += 2; Col += 2; }
              skipWS();
              auto [Cond, _] = parseValueRef(ValMap, BBMap, F);
              skipWS();
              if (peek() == ',') advance();
              skipWS();
              if (checkKeyword("label")) { Pos += 5; Col += 5; }
              skipWS();
              if (peek() == '%') advance();
              auto TrueName = readIdent();
              skipWS();
              if (peek() == ',') advance();
              skipWS();
              if (checkKeyword("label")) { Pos += 5; Col += 5; }
              skipWS();
              if (peek() == '%') advance();
              auto FalseName = readIdent();
              auto I = std::make_unique<IRInstruction>(Opcode::CondBr, TCtx.getVoidType(), 0);
              if (Cond) I->addOperand(Cond);
              auto* TrueBB = BBMap["%" + TrueName];
              auto* FalseBB = BBMap["%" + FalseName];
              if (TrueBB) I->addOperand(new IRBasicBlockRef(TrueBB));
              if (FalseBB) I->addOperand(new IRBasicBlockRef(FalseBB));
              Inst = BB->push_back(std::move(I));
              break;
            }
            case Opcode::Unreachable: {
              auto I = std::make_unique<IRInstruction>(Opcode::Unreachable, TCtx.getVoidType(), 0);
              Inst = BB->push_back(std::move(I));
              break;
            }
            // Binary ops
            case Opcode::Add: case Opcode::Sub: case Opcode::Mul:
            case Opcode::UDiv: case Opcode::SDiv: case Opcode::URem: case Opcode::SRem:
            case Opcode::FAdd: case Opcode::FSub: case Opcode::FMul:
            case Opcode::FDiv: case Opcode::FRem:
            case Opcode::Shl: case Opcode::LShr: case Opcode::AShr:
            case Opcode::And: case Opcode::Or: case Opcode::Xor:
              addBinaryInst(TCtx.getInt32Ty());
              break;
            case Opcode::Alloca: {
              auto* Ty = parseType();
              auto* PtrTy = TCtx.getPointerType(Ty);
              auto I = std::make_unique<IRInstruction>(Opcode::Alloca, PtrTy, 0, dialect::DialectID::Core, InstName);
              Inst = BB->push_back(std::move(I));
              break;
            }
            case Opcode::Load: {
              auto* ResTy = parseType();
              skipWS();
              if (peek() == ',') advance();
              skipWS();
              auto* PtrTy = parseType();
              skipWS();
              auto [Ptr, _] = parseValueRef(ValMap, BBMap, F);
              auto I = std::make_unique<IRInstruction>(Opcode::Load, ResTy, 0, dialect::DialectID::Core, InstName);
              if (Ptr) I->addOperand(Ptr);
              Inst = BB->push_back(std::move(I));
              break;
            }
            case Opcode::Store: {
              auto* ValTy = parseType();
              skipWS();
              auto [Val, _] = parseValueRef(ValMap, BBMap, F);
              skipWS();
              if (peek() == ',') advance();
              skipWS();
              auto* PtrTy = parseType();
              skipWS();
              auto [Ptr, __] = parseValueRef(ValMap, BBMap, F);
              auto I = std::make_unique<IRInstruction>(Opcode::Store, TCtx.getVoidType(), 0);
              if (Val) I->addOperand(Val);
              if (Ptr) I->addOperand(Ptr);
              Inst = BB->push_back(std::move(I));
              break;
            }
            case Opcode::Call: {
              auto* ResTy = parseType();
              skipWS();
              if (peek() == '@') advance();
              auto CalleeName = readIdent();
              skipWS();
              if (peek() == '(') advance();
              SmallVector<IRValue*, 8> Args;
              skipWS();
              while (peek() != ')' && !eof()) {
                auto* ArgTy = parseType();
                skipWS();
                auto [Arg, _] = parseValueRef(ValMap, BBMap, F);
                if (Arg) Args.push_back(Arg);
                skipWS();
                if (peek() == ',') advance();
                skipWS();
              }
              if (peek() == ')') advance();
              // Find callee function
              auto* Callee = M->getFunction(CalleeName);
              auto I = std::make_unique<IRInstruction>(Opcode::Call, ResTy, 0, dialect::DialectID::Core, InstName);
              if (Callee) {
                auto* CFR = new IRConstantFunctionRef(Callee);
                I->addOperand(CFR);
              }
              for (auto* A : Args) I->addOperand(A);
              Inst = BB->push_back(std::move(I));
              break;
            }
            case Opcode::ICmp: {
              // icmp <pred> <type> <lhs>, <rhs>
              auto PredStr = readIdent(); // skip predicate
              (void)PredStr;
              skipWS();
              auto* Ty = parseType();
              skipWS();
              auto [L, _] = parseValueRef(ValMap, BBMap, F);
              skipWS();
              if (peek() == ',') advance();
              skipWS();
              auto [R, __] = parseValueRef(ValMap, BBMap, F);
              auto I = std::make_unique<IRInstruction>(Opcode::ICmp, TCtx.getInt1Ty(), 0, dialect::DialectID::Core, InstName);
              if (L) I->addOperand(L);
              if (R) I->addOperand(R);
              Inst = BB->push_back(std::move(I));
              break;
            }
            case Opcode::Select: {
              // select i1 <cond>, <type> <tval>, <type> <fval>
              if (checkKeyword("i1")) { Pos += 2; Col += 2; }
              skipWS();
              auto [Cond, _] = parseValueRef(ValMap, BBMap, F);
              skipWS();
              if (peek() == ',') advance();
              skipWS();
              auto* TrueTy = parseType();
              skipWS();
              auto [TV, __] = parseValueRef(ValMap, BBMap, F);
              skipWS();
              if (peek() == ',') advance();
              skipWS();
              auto* FalseTy = parseType();
              skipWS();
              auto [FV, ___] = parseValueRef(ValMap, BBMap, F);
              auto I = std::make_unique<IRInstruction>(Opcode::Select, TrueTy, 0, dialect::DialectID::Core, InstName);
              if (Cond) I->addOperand(Cond);
              if (TV) I->addOperand(TV);
              if (FV) I->addOperand(FV);
              Inst = BB->push_back(std::move(I));
              break;
            }
            default:
              // Skip rest of line for unknown instructions
              break;
            }

            // Register the instruction in ValMap if it has a name
            if (Inst && HasResult && !InstName.empty()) {
              ValMap["%" + InstName] = Inst;
            }

            // Skip to end of line
            while (!eof() && peek() != '\n') advance();
          }
        }
        // Unknown construct, skip to next line
        while (!eof() && peek() != '\n') advance();
      }
    }

    return M;
  }
};

std::unique_ptr<IRModule> IRReader::parseText(
    StringRef Text, IRTypeContext& Ctx, SerializationDiagnostic* Diag) {
  TextParser P(Text, Ctx, Diag);
  return P.parse();
}

// ============================================================================
// Binary Parser
// ============================================================================

class BinaryParser {
  const char* Data;
  size_t Size;
  size_t Pos;
  IRTypeContext& TCtx;
  SerializationDiagnostic* Diag;

  void setError(SerializationErrorKind K, const std::string& Msg) {
    if (Diag) *Diag = SerializationDiagnostic(K, Msg);
  }

  bool hasBytes(size_t N) const { return Pos + N <= Size; }

  uint8_t readU8() { return static_cast<uint8_t>(Data[Pos++]); }
  uint16_t readU16() { auto V = readLE16(Data + Pos); Pos += 2; return V; }
  uint32_t readU32() { auto V = readLE32(Data + Pos); Pos += 4; return V; }
  uint64_t readU64() { auto V = readLE64(Data + Pos); Pos += 8; return V; }

  // Read string from string table using byte offset
  std::string readStrFromTable(const char* TableStart, size_t TableSize) {
    auto Off = readU32();
    auto Len = readU32();
    if (Off + Len <= TableSize) {
      return std::string(TableStart + Off, Len);
    }
    return "";
  }

  IRType* readType(const char* TableStart, size_t TableSize) {
    auto Kind = static_cast<IRType::Kind>(readU8());
    switch (Kind) {
    case IRType::Void: return TCtx.getVoidType();
    case IRType::Bool: return TCtx.getInt1Ty();
    case IRType::Integer: return TCtx.getIntType(readU32());
    case IRType::Float: return TCtx.getFloatType(readU32());
    case IRType::Pointer: {
      auto* Pointee = readType(TableStart, TableSize);
      auto AS = readU32();
      return TCtx.getPointerType(Pointee, AS);
    }
    case IRType::Array: {
      auto N = readU64();
      auto* Elem = readType(TableStart, TableSize);
      return TCtx.getArrayType(Elem, N);
    }
    case IRType::Struct: {
      auto Name = readStrFromTable(TableStart, TableSize);
      auto NumFields = readU32();
      SmallVector<IRType*, 16> Fields;
      for (unsigned i = 0; i < NumFields; ++i)
        Fields.push_back(readType(TableStart, TableSize));
      auto IsPacked = readU8();
      (void)IsPacked;
      if (!Name.empty()) {
        if (auto* ST = TCtx.getStructTypeByName(Name)) return ST;
        return TCtx.getStructType(Name, std::move(Fields));
      }
      return TCtx.getAnonStructType(std::move(Fields));
    }
    case IRType::Function: {
      auto* Ret = readType(TableStart, TableSize);
      auto IsVarArg = readU8();
      auto NumParams = readU32();
      SmallVector<IRType*, 8> Params;
      for (unsigned i = 0; i < NumParams; ++i)
        Params.push_back(readType(TableStart, TableSize));
      return TCtx.getFunctionType(Ret, std::move(Params), IsVarArg != 0);
    }
    case IRType::Vector: {
      auto N = readU32();
      auto* Elem = readType(TableStart, TableSize);
      return TCtx.getVectorType(Elem, N);
    }
    case IRType::Opaque: {
      auto Name = readStrFromTable(TableStart, TableSize);
      return TCtx.getOpaqueType(Name.empty() ? "opaque" : Name);
    }
    default:
      return TCtx.getVoidType();
    }
  }

public:
  BinaryParser(const char* D, size_t S, IRTypeContext& C, SerializationDiagnostic* Di)
    : Data(D), Size(S), Pos(0), TCtx(C), Diag(Di) {}

  std::unique_ptr<IRModule> parse() {
    if (Size < sizeof(IRFileHeader)) {
      setError(SerializationErrorKind::InvalidFormat, "Data too small for header");
      return nullptr;
    }

    // Check magic
    if (Data[0] != 'B' || Data[1] != 'T' || Data[2] != 'I' || Data[3] != 'R') {
      setError(SerializationErrorKind::InvalidFormat, "Invalid magic number");
      return nullptr;
    }

    // Read header
    IRFileHeader Header;
    std::memcpy(&Header, Data, sizeof(Header));

    // Check version
    auto Current = IRFormatVersion::Current();
    if (!Header.Version.isCompatibleWith(Current)) {
      setError(SerializationErrorKind::VersionMismatch,
               "Version mismatch: file has " + Header.Version.toString());
      return nullptr;
    }

    auto ModOff = Header.ModuleOffset;
    auto StrOff = Header.StringTableOffset;
    auto StrSize = Header.StringTableSize;

    if (ModOff > Size || StrOff > Size || StrOff + StrSize > Size) {
      setError(SerializationErrorKind::TruncatedData, "Offsets out of range");
      return nullptr;
    }

    const char* TableStart = Data + StrOff;
    size_t TableSize = StrSize;

    // Skip header — parse module data starting at ModOff
    Pos = ModOff;

    // Read module name (StringRef at start of module data)
    auto ModName = readStrFromTable(TableStart, TableSize);
    auto M = std::make_unique<IRModule>(ModName.empty() ? "module" : ModName, TCtx);

    // GlobalSection
    if (!hasBytes(4)) { setError(SerializationErrorKind::TruncatedData, "Truncated globals"); return nullptr; }
    auto NumGlobals = readU32();
    for (uint32_t i = 0; i < NumGlobals; ++i) {
      if (!hasBytes(8)) { setError(SerializationErrorKind::TruncatedData, "Truncated global entry"); return nullptr; }
      auto GVName = readStrFromTable(TableStart, TableSize);
      auto* GTy = readType(TableStart, TableSize);
      readU8(); // isConstant
      auto HasInit = readU8();
      if (HasInit) {
        // Skip constant record
        auto CK = static_cast<ValueKind>(readU8());
        switch (CK) {
        case ValueKind::ConstantInt: readU32(); readU64(); break;
        case ValueKind::ConstantFloat: readU32(); readU64(); break;
        case ValueKind::ConstantNull: case ValueKind::ConstantUndef:
        case ValueKind::ConstantAggregateZero:
          readType(TableStart, TableSize); break;
        default: break;
        }
      }
      readU32(); // alignment
      readU32(); // address space
      readU8(); // linkage
      M->getOrInsertGlobal(GVName.empty() ? ("g" + std::to_string(i)) : GVName, GTy);
    }

    // FunctionDeclSection
    if (!hasBytes(4)) { setError(SerializationErrorKind::TruncatedData, "Truncated decl section"); return nullptr; }
    auto NumDecls = readU32();
    for (uint32_t i = 0; i < NumDecls; ++i) {
      if (!hasBytes(8)) { setError(SerializationErrorKind::TruncatedData, "Truncated decl entry"); return nullptr; }
      auto FName = readStrFromTable(TableStart, TableSize);
      auto* FT = readType(TableStart, TableSize);
      readU8(); // linkage
      readU8(); // calling conv
      if (FT->isFunction()) {
        auto* FTy = static_cast<IRFunctionType*>(FT);
        M->getOrInsertFunction(FName.empty() ? ("decl" + std::to_string(i)) : FName, FTy);
      }
    }

    // FunctionSection
    if (!hasBytes(4)) { setError(SerializationErrorKind::TruncatedData, "Truncated func section"); return nullptr; }
    auto NumDefs = readU32();
    for (uint32_t i = 0; i < NumDefs; ++i) {
      if (!hasBytes(8)) { setError(SerializationErrorKind::TruncatedData, "Truncated func entry"); return nullptr; }
      auto FName = readStrFromTable(TableStart, TableSize);
      auto* FT = readType(TableStart, TableSize);
      readU8(); // linkage
      readU8(); // calling conv
      readU32(); // attrs
      auto NumBBs = readU32();

      IRFunction* F = nullptr;
      if (FT->isFunction()) {
        auto* FTy = static_cast<IRFunctionType*>(FT);
        F = M->getOrInsertFunction(FName.empty() ? ("fn" + std::to_string(i)) : FName, FTy);
      }
      if (!F) {
        // Skip remaining data for this function
        for (uint32_t b = 0; b < NumBBs; ++b) {
          readStrFromTable(TableStart, TableSize); // bb name
          auto NumInsts = readU32();
          for (uint32_t ii = 0; ii < NumInsts; ++ii) {
            readU16(); readU8(); // opcode, dialect
            readType(TableStart, TableSize); // result type
            readStrFromTable(TableStart, TableSize); // inst name
            auto NumOps = readU32();
            for (uint32_t o = 0; o < NumOps; ++o) {
              auto VK = static_cast<ValueKind>(readU8());
              switch (VK) {
              case ValueKind::ConstantInt: readU32(); readU64(); break;
              case ValueKind::ConstantFloat: readU32(); readU64(); break;
              case ValueKind::ConstantNull: case ValueKind::ConstantUndef:
              case ValueKind::ConstantAggregateZero:
                readType(TableStart, TableSize); break;
              case ValueKind::ConstantFunctionRef:
              case ValueKind::ConstantGlobalRef:
                readU32(); break; // index only (4 bytes)
              case ValueKind::InstructionResult:
                readU32(); readU32(); break; // BBIdx + InstIdx (8 bytes)
              case ValueKind::Argument:
              case ValueKind::BasicBlockRef:
                readU32(); break;
              default: break;
              }
            }
          }
        }
        continue;
      }

      // First pass: collect BB names and create BBs, record NumInsts
      struct BBInfo { IRBasicBlock* BB; uint32_t NumInsts; };
      SmallVector<BBInfo, 8> BBs;
      auto SavedPos = Pos;
      for (uint32_t b = 0; b < NumBBs; ++b) {
        auto BBName = readStrFromTable(TableStart, TableSize);
        auto NInsts = readU32();
        auto* BB = F->addBasicBlock(BBName.empty() ? ("bb" + std::to_string(b)) : BBName);
        BBs.push_back({BB, NInsts});
        // Skip instruction data
        for (uint32_t ii = 0; ii < NInsts; ++ii) {
          readU16(); readU8(); // opcode, dialect
          readType(TableStart, TableSize);
          readStrFromTable(TableStart, TableSize); // name
          auto NOps = readU32();
          for (uint32_t o = 0; o < NOps; ++o) {
            auto VK = static_cast<ValueKind>(readU8());
            switch (VK) {
            case ValueKind::ConstantInt: readU32(); readU64(); break;
            case ValueKind::ConstantFloat: readU32(); readU64(); break;
            case ValueKind::ConstantNull: case ValueKind::ConstantUndef:
            case ValueKind::ConstantAggregateZero:
              readType(TableStart, TableSize); break;
            case ValueKind::ConstantFunctionRef:
            case ValueKind::ConstantGlobalRef:
              readU32(); break;
            case ValueKind::InstructionResult:
              readU32(); readU32(); break;
            case ValueKind::Argument:
            case ValueKind::BasicBlockRef:
              readU32(); break;
            default: break;
            }
          }
        }
      }

      // Second pass: re-parse to create instructions with resolved references
      Pos = SavedPos;
      std::vector<IRInstruction*> AllInsts;
      for (uint32_t b = 0; b < NumBBs; ++b) {
        readStrFromTable(TableStart, TableSize); // re-read bb name
        auto NumInsts = readU32();
        auto* BB = BBs[b].BB;

        for (uint32_t ii = 0; ii < NumInsts; ++ii) {
          if (!hasBytes(4)) break;
          auto Op = static_cast<Opcode>(readU16());
          readU8(); // dialect
          auto* ResTy = readType(TableStart, TableSize);
          auto InstName = readStrFromTable(TableStart, TableSize);
          auto NumOps = readU32();

          SmallVector<IRValue*, 8> Operands;
          for (uint32_t o = 0; o < NumOps; ++o) {
            auto VK = static_cast<ValueKind>(readU8());
            switch (VK) {
            case ValueKind::ConstantInt: {
              auto BW = readU32();
              auto V = readU64();
              auto* Ty = TCtx.getIntType(BW);
              Operands.push_back(new IRConstantInt(static_cast<IRIntegerType*>(Ty), V));
              break;
            }
            case ValueKind::ConstantFloat: {
              auto BW = readU32();
              auto Bits = readU64();
              auto* Ty = TCtx.getFloatType(BW);
              APInt API(BW, Bits);
              APFloat APF = APFloat::bitcastFromAPInt(API);
              Operands.push_back(new IRConstantFP(static_cast<IRFloatType*>(Ty), APF));
              break;
            }
            case ValueKind::ConstantNull:
              readType(TableStart, TableSize);
              Operands.push_back(new IRConstantNull(ResTy));
              break;
            case ValueKind::ConstantUndef:
              readType(TableStart, TableSize);
              Operands.push_back(new IRConstantUndef(ResTy));
              break;
            case ValueKind::ConstantAggregateZero:
              readType(TableStart, TableSize);
              Operands.push_back(new IRConstantAggregateZero(ResTy));
              break;
            case ValueKind::ConstantFunctionRef: {
              auto FIdx = readU32();
              uint32_t CurIdx = 0;
              IRFunction* RefFunc = nullptr;
              for (auto& FF : M->getFunctions()) {
                if (CurIdx == FIdx) { RefFunc = FF.get(); break; }
                ++CurIdx;
              }
              if (RefFunc)
                Operands.push_back(new IRConstantFunctionRef(RefFunc));
              else
                Operands.push_back(nullptr);
              break;
            }
            case ValueKind::ConstantGlobalRef: {
              auto GIdx = readU32();
              uint32_t CurIdx = 0;
              IRGlobalVariable* RefGV = nullptr;
              for (auto& GV : M->getGlobals()) {
                if (CurIdx == GIdx) { RefGV = GV.get(); break; }
                ++CurIdx;
              }
              if (RefGV)
                Operands.push_back(new IRConstantGlobalRef(RefGV));
              else
                Operands.push_back(nullptr);
              break;
            }
            case ValueKind::InstructionResult: {
              auto BBIdx = readU32();
              auto InstIdx = readU32();
              size_t FlatIdx = 0;
              for (uint32_t bb = 0; bb < BBIdx && bb < BBs.size(); ++bb)
                FlatIdx += BBs[bb].NumInsts;
              FlatIdx += InstIdx;
              if (FlatIdx < AllInsts.size())
                Operands.push_back(AllInsts[FlatIdx]);
              else
                Operands.push_back(nullptr);
              break;
            }
            case ValueKind::Argument: {
              auto AIdx = readU32();
              (void)AIdx;
              // IRArgument is not an IRValue, cannot create operand reference
              Operands.push_back(nullptr);
              break;
            }
            case ValueKind::BasicBlockRef: {
              auto BBIdx = readU32();
              if (BBIdx < BBs.size())
                Operands.push_back(new IRBasicBlockRef(BBs[BBIdx].BB));
              else
                Operands.push_back(nullptr);
              break;
            }
            default:
              break;
            }
          }

          auto Inst = std::make_unique<IRInstruction>(Op, ResTy, 0,
              dialect::DialectID::Core, InstName);
          for (auto* O : Operands) if (O) Inst->addOperand(O);
          auto* RawInst = BB->push_back(std::move(Inst));
          AllInsts.push_back(RawInst);
        }
      }
    }

    return M;
  }
};

std::unique_ptr<IRModule> IRReader::parseBitcode(
    StringRef Data, IRTypeContext& Ctx, SerializationDiagnostic* Diag) {
  BinaryParser P(Data.data(), Data.size(), Ctx, Diag);
  return P.parse();
}

// ============================================================================
// IRReader::readFile
// ============================================================================

std::unique_ptr<IRModule> IRReader::readFile(
    StringRef Path, IRTypeContext& Ctx, SerializationDiagnostic* Diag) {
  // Read entire file
  std::ifstream File(std::string(Path), std::ios::binary | std::ios::ate);
  if (!File.is_open()) {
    if (Diag) *Diag = SerializationDiagnostic(
        SerializationErrorKind::IOError, "Cannot open file: " + Path.str());
    return nullptr;
  }
  auto FileSize = File.tellg();
  if (FileSize == 0) {
    if (Diag) *Diag = SerializationDiagnostic(
        SerializationErrorKind::IOError, "Empty file");
    return nullptr;
  }
  File.seekg(0, std::ios::beg);
  std::string Content(FileSize, '\0');
  File.read(Content.data(), FileSize);
  File.close();

  // Check magic number
  if (Content.size() >= 4 &&
      Content[0] == 'B' && Content[1] == 'T' &&
      Content[2] == 'I' && Content[3] == 'R') {
    return parseBitcode(StringRef(Content.data(), Content.size()), Ctx, Diag);
  }

  return parseText(StringRef(Content.data(), Content.size()), Ctx, Diag);
}

} // namespace ir
} // namespace blocktype
