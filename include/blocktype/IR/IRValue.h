#ifndef BLOCKTYPE_IR_IRVALUE_H
#define BLOCKTYPE_IR_IRVALUE_H

#include <cassert>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRType.h"

namespace blocktype {
namespace ir {

enum class ValueKind : uint8_t {
  ConstantInt           = 0,
  ConstantFloat         = 1,
  ConstantNull          = 2,
  ConstantUndef         = 3,
  ConstantStruct        = 4,
  ConstantArray         = 5,
  ConstantAggregateZero = 6,
  ConstantFunctionRef   = 7,
  ConstantGlobalRef     = 8,
  InstructionResult     = 9,
  Argument              = 10,
  BasicBlockRef         = 11,
};

enum class Opcode : uint16_t {
  Ret = 0, Br = 1, CondBr = 2, Switch = 3, Invoke = 4, Unreachable = 5, Resume = 6,
  Add = 16, Sub = 17, Mul = 18, UDiv = 19, SDiv = 20, URem = 21, SRem = 22,
  FAdd = 32, FSub = 33, FMul = 34, FDiv = 35, FRem = 36,
  Shl = 48, LShr = 49, AShr = 50, And = 51, Or = 52, Xor = 53,
  Alloca = 64, Load = 65, Store = 66, GEP = 67, Memcpy = 68, Memset = 69,
  Trunc = 80, ZExt = 81, SExt = 82, FPTrunc = 83, FPExt = 84,
  FPToSI = 85, FPToUI = 86, SIToFP = 87, UIToFP = 88,
  PtrToInt = 89, IntToPtr = 90, BitCast = 91,
  ICmp = 96, FCmp = 97,
  Call = 112,
  Phi = 128, Select = 129, ExtractValue = 130, InsertValue = 131,
  ExtractElement = 132, InsertElement = 133, ShuffleVector = 134,
  DbgDeclare = 144, DbgValue = 145, DbgLabel = 146,
  FFICall = 160, FFICheck = 161, FFICoerce = 162, FFIUnwind = 163,
  AtomicLoad = 176, AtomicStore = 177, AtomicRMW = 178, AtomicCmpXchg = 179, Fence = 180,
};

enum class ICmpPred : uint8_t {
  EQ = 0, NE = 1, UGT = 2, UGE = 3, ULT = 4, ULE = 5, SGT = 6, SGE = 7, SLT = 8, SLE = 9,
};

enum class FCmpPred : uint8_t {
  False = 0, OEQ = 1, OGT = 2, OGE = 3, OLT = 4, OLE = 5, ONE = 6, ORD = 7,
  UNO = 8, UEQ = 9, UGT = 10, UGE = 11, ULT = 12, ULE = 13, UNE = 14, True = 15,
};

enum class LinkageKind : uint8_t {
  External = 0, Private = 1, Internal = 2, LinkOnce = 3, LinkOnceODR = 4,
  Weak = 5, WeakODR = 6, Common = 7, Appending = 8, ExternalWeak = 9,
  AvailableExternally = 10,
};

enum class CallingConvention : uint8_t {
  C = 0, Fast = 1, Cold = 2, GHC = 3, Stdcall = 4, Fastcall = 5,
  VectorCall = 6, ThisCall = 7, Swift = 8, WASM = 9, BTInternal = 10,
};

enum class FunctionAttr : uint32_t {
  NoReturn = 1 << 0, NoThrow = 1 << 1, ReadOnly = 1 << 2, WriteOnly = 1 << 3,
  ReadNone = 1 << 4, NoInline = 1 << 5, AlwaysInline = 1 << 6, NoUnwind = 1 << 7,
  Pure = 1 << 8, Const = 1 << 9, Naked = 1 << 10, NoRecurse = 1 << 11,
  WillReturn = 1 << 12, MustProgress = 1 << 13,
};
using FunctionAttrs = uint32_t;

class IRBasicBlock;
class IRFunction;
class IRGlobalVariable;
class IRValue;
class User;

class Use {
  friend class IRValue;
  IRValue* Val = nullptr;
  User* Owner = nullptr;

public:
  Use() = default;
  Use(IRValue* V, User* O);
  IRValue* get() const { return Val; }
  void set(IRValue* V);
  User* getUser() const { return Owner; }
  operator IRValue*() const { return Val; }
};

class IRValue {
protected:
  ValueKind Kind;
  IRType* Ty;
  unsigned ValueID;
  std::string Name;
  std::vector<Use*> UseList;

public:
  IRValue(ValueKind K, IRType* T, unsigned ID, StringRef N = "")
    : Kind(K), Ty(T), ValueID(ID), Name(N.str()) {}
  virtual ~IRValue() = default;

  ValueKind getValueKind() const { return Kind; }
  IRType* getType() const { return Ty; }
  unsigned getValueID() const { return ValueID; }
  StringRef getName() const { return Name; }
  void setName(StringRef N) { Name = N.str(); }
  bool isConstant() const { return static_cast<uint8_t>(Kind) <= 8; }
  bool isInstruction() const { return Kind == ValueKind::InstructionResult; }
  bool isArgument() const { return Kind == ValueKind::Argument; }
  void replaceAllUsesWith(IRValue* New);
  unsigned getNumUses() const { return static_cast<unsigned>(UseList.size()); }
  void addUse(Use* U) { UseList.push_back(U); }
  void removeUse(Use* U);
  virtual void print(std::ostream& OS) const = 0;
};

class User : public IRValue {
protected:
  std::vector<Use> Operands;

public:
  User(ValueKind K, IRType* T, unsigned ID, StringRef N = "")
    : IRValue(K, T, ID, N) {}
  unsigned getNumOperands() const { return static_cast<unsigned>(Operands.size()); }
  IRValue* getOperand(unsigned i) const { return Operands[i].get(); }
  void setOperand(unsigned i, IRValue* V) { Operands[i].set(V); }
  void addOperand(IRValue* V);
  ArrayRef<Use> operands() const { return Operands; }
  IRType* getOperandType(unsigned i) const { return Operands[i].get()->getType(); }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRVALUE_H
