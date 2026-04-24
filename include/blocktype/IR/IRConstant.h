#ifndef BLOCKTYPE_IR_IRCONSTANT_H
#define BLOCKTYPE_IR_IRCONSTANT_H

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRValue.h"

namespace blocktype {
namespace ir {

class APInt {
  uint64_t Val;
  unsigned BitWidth;

  uint64_t mask() const {
    return BitWidth == 64 ? ~uint64_t(0) : ((uint64_t(1) << BitWidth) - 1);
  }

public:
  APInt() : Val(0), BitWidth(0) {}
  APInt(unsigned BW, uint64_t V, bool IsSigned = false)
    : Val(IsSigned ? static_cast<uint64_t>(static_cast<int64_t>(V)) : V),
      BitWidth(BW) {
    Val &= mask();
  }

  static APInt getZero(unsigned BW) { return APInt(BW, 0); }
  static APInt getAllOnes(unsigned BW) { return APInt(BW, ~uint64_t(0)); }
  static APInt getSignedMaxValue(unsigned BW) {
    return APInt(BW, (uint64_t(1) << (BW - 1)) - 1);
  }
  static APInt getSignedMinValue(unsigned BW) {
    return APInt(BW, uint64_t(1) << (BW - 1));
  }

  unsigned getBitWidth() const { return BitWidth; }
  uint64_t getZExtValue() const { return Val & mask(); }
  int64_t getSExtValue() const {
    if (BitWidth == 0) return 0;
    uint64_t SignBit = (Val >> (BitWidth - 1)) & 1;
    if (SignBit && BitWidth < 64) {
      uint64_t SignExt = ~mask();
      return static_cast<int64_t>(Val | SignExt);
    }
    return static_cast<int64_t>(Val);
  }
  bool isZero() const { return (Val & mask()) == 0; }
  bool isAllOnesValue() const { return (Val & mask()) == mask(); }
  bool isNegative() const {
    return BitWidth > 0 && ((Val >> (BitWidth - 1)) & 1) != 0;
  }
  bool isStrictlyPositive() const {
    return !isNegative() && !isZero();
  }

  APInt trunc(unsigned BW) const {
    assert(BW <= BitWidth && "truncate to larger bitwidth");
    return APInt(BW, Val);
  }
  APInt zext(unsigned BW) const {
    assert(BW >= BitWidth && "extend to smaller bitwidth");
    return APInt(BW, Val);
  }
  APInt sext(unsigned BW) const {
    assert(BW >= BitWidth && "extend to smaller bitwidth");
    if (BitWidth == 0) return APInt(BW, 0);
    if (!isNegative()) return APInt(BW, Val);
    uint64_t SignExt = ~mask();
    return APInt(BW, (Val | SignExt) & ((BW == 64) ? ~uint64_t(0) : ((uint64_t(1) << BW) - 1)));
  }

  bool operator==(const APInt& O) const {
    assert(BitWidth == O.BitWidth && "bitwidth mismatch");
    return (Val & mask()) == (O.Val & mask());
  }
  bool operator!=(const APInt& O) const { return !(*this == O); }
  bool ugt(const APInt& O) const {
    assert(BitWidth == O.BitWidth && "bitwidth mismatch");
    return (Val & mask()) > (O.Val & mask());
  }
  bool uge(const APInt& O) const { return !O.ugt(*this); }
  bool ult(const APInt& O) const { return O.ugt(*this); }
  bool ule(const APInt& O) const { return !ugt(O); }
  bool sgt(const APInt& O) const { return !sle(O) && !eq(O); }
  bool sge(const APInt& O) const { return !slt(O); }
  bool slt(const APInt& O) const {
    return getSExtValue() < O.getSExtValue();
  }
  bool sle(const APInt& O) const {
    return getSExtValue() <= O.getSExtValue();
  }
  bool eq(const APInt& O) const { return *this == O; }

  APInt operator+(const APInt& O) const {
    assert(BitWidth == O.BitWidth && "bitwidth mismatch");
    return APInt(BitWidth, Val + O.Val);
  }
  APInt operator-(const APInt& O) const {
    assert(BitWidth == O.BitWidth && "bitwidth mismatch");
    return APInt(BitWidth, Val - O.Val);
  }
  APInt operator*(const APInt& O) const {
    assert(BitWidth == O.BitWidth && "bitwidth mismatch");
    return APInt(BitWidth, Val * O.Val);
  }
  APInt udiv(const APInt& O) const {
    assert(BitWidth == O.BitWidth && !O.isZero() && "division by zero");
    return APInt(BitWidth, (Val & mask()) / (O.Val & mask()));
  }
  APInt sdiv(const APInt& O) const {
    assert(BitWidth == O.BitWidth && !O.isZero() && "division by zero");
    int64_t A = getSExtValue(), B = O.getSExtValue();
    return APInt(BitWidth, static_cast<uint64_t>(A / B));
  }
  APInt urem(const APInt& O) const {
    assert(BitWidth == O.BitWidth && !O.isZero() && "remainder by zero");
    return APInt(BitWidth, (Val & mask()) % (O.Val & mask()));
  }
  APInt srem(const APInt& O) const {
    assert(BitWidth == O.BitWidth && !O.isZero() && "remainder by zero");
    int64_t A = getSExtValue(), B = O.getSExtValue();
    return APInt(BitWidth, static_cast<uint64_t>(A % B));
  }

  APInt operator<<(unsigned Shift) const {
    if (Shift >= BitWidth) return APInt(BitWidth, 0);
    return APInt(BitWidth, Val << Shift);
  }
  APInt operator>>(unsigned Shift) const {
    if (Shift >= BitWidth) return APInt(BitWidth, 0);
    return APInt(BitWidth, Val >> Shift);
  }
  APInt ashr(unsigned Shift) const {
    if (Shift >= BitWidth) return isNegative() ? getAllOnes(BitWidth) : getZero(BitWidth);
    uint64_t Result = Val >> Shift;
    if (isNegative() && BitWidth < 64) {
      uint64_t HighBits = ~mask() >> Shift;
      Result |= HighBits;
    }
    return APInt(BitWidth, Result);
  }
  APInt operator&(const APInt& O) const {
    assert(BitWidth == O.BitWidth && "bitwidth mismatch");
    return APInt(BitWidth, Val & O.Val);
  }
  APInt operator|(const APInt& O) const {
    assert(BitWidth == O.BitWidth && "bitwidth mismatch");
    return APInt(BitWidth, Val | O.Val);
  }
  APInt operator^(const APInt& O) const {
    assert(BitWidth == O.BitWidth && "bitwidth mismatch");
    return APInt(BitWidth, Val ^ O.Val);
  }
  APInt operator~() const {
    return APInt(BitWidth, ~Val);
  }

  std::string toString(unsigned Radix = 10, bool IsSigned = false) const {
    if (BitWidth == 0) return "0";
    if (Radix == 10 && IsSigned) {
      return std::to_string(getSExtValue());
    }
    return std::to_string(getZExtValue());
  }

  void print(std::ostream& OS) const {
    OS << toString(10, true);
  }
};

class APFloat {
public:
  enum class Semantics : uint8_t {
    Half = 16,
    Float = 32,
    Double = 64,
    x87Extended = 80,
    Quad = 128,
  };

private:
  Semantics Sem;
  double Val;

  static double ToDouble(Semantics S, double V) { return V; }

public:
  APFloat() : Sem(Semantics::Double), Val(0.0) {}
  APFloat(Semantics S, double V) : Sem(S), Val(V) {}
  APFloat(float V) : Sem(Semantics::Float), Val(static_cast<double>(V)) {}
  APFloat(double V) : Sem(Semantics::Double), Val(V) {}
  APFloat(Semantics S) : Sem(S), Val(0.0) {}

  static APFloat getZero(Semantics S) { return APFloat(S, 0.0); }
  static APFloat getNaN(Semantics S) { return APFloat(S, std::numeric_limits<double>::quiet_NaN()); }
  static APFloat getInfinity(Semantics S) { return APFloat(S, std::numeric_limits<double>::infinity()); }

  Semantics getSemantics() const { return Sem; }
  unsigned getBitWidth() const { return static_cast<unsigned>(Sem); }
  double getRawValue() const { return Val; }
  bool isZero() const { return Val == 0.0; }
  bool isNaN() const { return std::isnan(Val); }
  bool isInfinity() const { return std::isinf(Val); }
  bool isNegative() const { return std::signbit(Val); }
  bool isFinite() const { return std::isfinite(Val); }

  APFloat operator+(const APFloat& O) const { return APFloat(Sem, Val + O.Val); }
  APFloat operator-(const APFloat& O) const { return APFloat(Sem, Val - O.Val); }
  APFloat operator*(const APFloat& O) const { return APFloat(Sem, Val * O.Val); }
  APFloat operator/(const APFloat& O) const { return APFloat(Sem, Val / O.Val); }

  bool operator==(const APFloat& O) const { return Val == O.Val; }
  bool operator!=(const APFloat& O) const { return Val != O.Val; }
  bool operator<(const APFloat& O) const { return Val < O.Val; }
  bool operator<=(const APFloat& O) const { return Val <= O.Val; }
  bool operator>(const APFloat& O) const { return Val > O.Val; }
  bool operator>=(const APFloat& O) const { return Val >= O.Val; }

  APFloat abs() const { return APFloat(Sem, std::fabs(Val)); }
  APFloat neg() const { return APFloat(Sem, -Val); }

  APFloat convert(Semantics ToSem) const { return APFloat(ToSem, Val); }

  APInt bitcastToAPInt() const {
    uint64_t Bits;
    std::memcpy(&Bits, &Val, sizeof(Bits));
    return APInt(64, Bits);
  }

  static APFloat bitcastFromAPInt(const APInt& Bits) {
    double D;
    uint64_t V = Bits.getZExtValue();
    std::memcpy(&D, &V, sizeof(D));
    return APFloat(Semantics::Double, D);
  }

  std::string toString() const {
    if (isNaN()) return "NaN";
    if (isInfinity()) return isNegative() ? "-Inf" : "+Inf";
    return std::to_string(Val);
  }

  void print(std::ostream& OS) const { OS << toString(); }
};

class IRConstant : public IRValue {
public:
  IRConstant(ValueKind K, IRType* T, unsigned ID) : IRValue(K, T, ID) {}
  static bool classof(const IRValue* V) { return V->isConstant(); }
};

class IRConstantInt : public IRConstant {
  APInt Value;

public:
  IRConstantInt(IRIntegerType* Ty, const APInt& V)
    : IRConstant(ValueKind::ConstantInt, Ty, 0), Value(V) {}
  IRConstantInt(IRIntegerType* Ty, uint64_t V)
    : IRConstant(ValueKind::ConstantInt, Ty, 0), Value(Ty->getBitWidth(), V) {}
  const APInt& getValue() const { return Value; }
  uint64_t getZExtValue() const { return Value.getZExtValue(); }
  int64_t getSExtValue() const { return Value.getSExtValue(); }
  bool isZero() const { return Value.isZero(); }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantInt; }
  void print(std::ostream& OS) const override;
};

class IRConstantFP : public IRConstant {
  APFloat Value;

public:
  IRConstantFP(IRFloatType* Ty, const APFloat& V)
    : IRConstant(ValueKind::ConstantFloat, Ty, 0), Value(V) {}
  const APFloat& getValue() const { return Value; }
  bool isZero() const { return Value.isZero(); }
  bool isNaN() const { return Value.isNaN(); }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantFloat; }
  void print(std::ostream& OS) const override;
};

class IRConstantNull : public IRConstant {
public:
  explicit IRConstantNull(IRType* Ty) : IRConstant(ValueKind::ConstantNull, Ty, 0) {}
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantNull; }
  void print(std::ostream& OS) const override;
};

class IRConstantUndef : public IRConstant {
  static std::unordered_map<IRType*, IRConstantUndef*>& getCache() {
    static std::unordered_map<IRType*, IRConstantUndef*> Cache;
    return Cache;
  }

public:
  explicit IRConstantUndef(IRType* Ty) : IRConstant(ValueKind::ConstantUndef, Ty, 0) {}
  static IRConstantUndef* get(IRType* Ty) {
    auto& Cache = getCache();
    auto It = Cache.find(Ty);
    if (It != Cache.end()) return It->second;
    auto* U = new IRConstantUndef(Ty);
    Cache[Ty] = U;
    return U;
  }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantUndef; }
  void print(std::ostream& OS) const override;
};

class IRConstantAggregateZero : public IRConstant {
public:
  explicit IRConstantAggregateZero(IRType* Ty) : IRConstant(ValueKind::ConstantAggregateZero, Ty, 0) {}
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantAggregateZero; }
  void print(std::ostream& OS) const override;
};

class IRConstantStruct : public IRConstant {
  SmallVector<IRConstant*, 16> Elements;

public:
  IRConstantStruct(IRStructType* Ty, SmallVector<IRConstant*, 16> Elems)
    : IRConstant(ValueKind::ConstantStruct, Ty, 0), Elements(std::move(Elems)) {}
  ArrayRef<IRConstant*> getElements() const { return Elements; }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantStruct; }
  void print(std::ostream& OS) const override;
};

class IRConstantArray : public IRConstant {
  SmallVector<IRConstant*, 16> Elements;

public:
  IRConstantArray(IRArrayType* Ty, SmallVector<IRConstant*, 16> Elems)
    : IRConstant(ValueKind::ConstantArray, Ty, 0), Elements(std::move(Elems)) {}
  ArrayRef<IRConstant*> getElements() const { return Elements; }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantArray; }
  void print(std::ostream& OS) const override;
};

class IRConstantFunctionRef : public IRConstant {
  IRFunction* Func;

public:
  explicit IRConstantFunctionRef(IRFunction* F);
  IRFunction* getFunction() const { return Func; }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantFunctionRef; }
  void print(std::ostream& OS) const override;
};

class IRConstantGlobalRef : public IRConstant {
  IRGlobalVariable* Global;

public:
  explicit IRConstantGlobalRef(IRGlobalVariable* G);
  IRGlobalVariable* getGlobal() const { return Global; }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantGlobalRef; }
  void print(std::ostream& OS) const override;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRCONSTANT_H
