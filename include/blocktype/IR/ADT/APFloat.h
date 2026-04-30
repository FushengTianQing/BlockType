#ifndef BLOCKTYPE_IR_ADT_APFLOAT_H
#define BLOCKTYPE_IR_ADT_APFLOAT_H

#include "blocktype/IR/ADT/APInt.h"
#include "blocktype/IR/ADT/raw_ostream.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <string>

namespace blocktype {
namespace ir {

class APFloat {
public:
  enum class Semantics : uint8_t {
    Half = 16,
    Float = 32,
    Double = 64,
    x87Extended = 80,
    Quad = 128,
  };

  enum class Category : uint8_t {
    Normal,
    Zero,
    Infinity,
    NaN,
  };

private:
  Semantics Sem;
  Category Cat;
  bool Sign;
  APInt Significand;
  int32_t Exponent;

  static unsigned getExponentBits(Semantics S) {
    switch (S) {
    case Semantics::Half: return 5;
    case Semantics::Float: return 8;
    case Semantics::Double: return 11;
    case Semantics::x87Extended: return 15;
    case Semantics::Quad: return 15;
    }
    return 11;
  }

  static unsigned getSignificandBits(Semantics S) {
    switch (S) {
    case Semantics::Half: return 10;
    case Semantics::Float: return 23;
    case Semantics::Double: return 52;
    case Semantics::x87Extended: return 63;
    case Semantics::Quad: return 112;
    }
    return 52;
  }

  static int32_t getExponentBias(Semantics S) {
    switch (S) {
    case Semantics::Half: return 15;
    case Semantics::Float: return 127;
    case Semantics::Double: return 1023;
    case Semantics::x87Extended: return 16383;
    case Semantics::Quad: return 16383;
    }
    return 1023;
  }

  static int32_t getMaxExponent(Semantics S) {
    return (1 << getExponentBits(S)) - 1;
  }

  static unsigned getTotalBits(Semantics S) {
    switch (S) {
    case Semantics::Half: return 16;
    case Semantics::Float: return 32;
    case Semantics::Double: return 64;
    case Semantics::x87Extended: return 80;
    case Semantics::Quad: return 128;
    }
    return 64;
  }

  void fromDouble(double V) {
    uint64_t Bits;
    std::memcpy(&Bits, &V, sizeof(Bits));
    Sign = (Bits >> 63) & 1;
    uint64_t RawExp = (Bits >> 52) & 0x7FF;
    uint64_t RawSig = Bits & ((uint64_t(1) << 52) - 1);

    unsigned SigBits = getSignificandBits(Sem);
    unsigned ExpBits = getExponentBits(Sem);
    (void)ExpBits;
    int32_t Bias = getExponentBias(Sem);

    if (RawExp == 0x7FF) {
      if (RawSig != 0) {
        Cat = Category::NaN;
      } else {
        Cat = Category::Infinity;
      }
      Significand = APInt(SigBits + 1, 0);
      Exponent = 0;
      return;
    }

    if (RawExp == 0 && RawSig == 0) {
      Cat = Category::Zero;
      Significand = APInt(SigBits + 1, 0);
      Exponent = 0;
      return;
    }

    Cat = Category::Normal;
    if (RawExp == 0) {
      Exponent = static_cast<int32_t>(1 - 1023) - Bias;
      Significand = APInt(SigBits + 1, RawSig);
    } else {
      Exponent = static_cast<int32_t>(RawExp) - 1023;
      if (SigBits <= 52) {
        uint64_t Sig = RawSig >> (52 - SigBits);
        Significand = APInt(SigBits + 1, Sig);
      } else {
        uint64_t Sig = RawSig << (SigBits - 52);
        Significand = APInt(SigBits + 1, Sig);
      }
      Significand.setBit(SigBits);
    }
  }

  double toDouble() const {
    if (Cat == Category::Zero) return Sign ? -0.0 : 0.0;
    if (Cat == Category::Infinity) return Sign ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    if (Cat == Category::NaN) return std::numeric_limits<double>::quiet_NaN();

    unsigned SigBits = getSignificandBits(Sem);
    int32_t Bias = getExponentBias(Sem);
    (void)Bias;

    uint64_t RawSig = Significand.getZExtValue();
    uint64_t DoubleSig;
    if (SigBits <= 52) {
      DoubleSig = RawSig << (52 - SigBits);
      DoubleSig &= (uint64_t(1) << 52) - 1;
    } else {
      DoubleSig = RawSig >> (SigBits - 52);
    }

    int32_t DoubleExp = Exponent + 1023;
    if (DoubleExp <= 0) {
      DoubleSig = 0;
      DoubleExp = 0;
    }
    if (DoubleExp >= 0x7FF) {
      return Sign ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }

    uint64_t Result = (static_cast<uint64_t>(Sign) << 63)
                    | (static_cast<uint64_t>(DoubleExp) << 52)
                    | DoubleSig;
    double D;
    std::memcpy(&D, &Result, sizeof(D));
    return D;
  }

public:
  APFloat() : Sem(Semantics::Double), Cat(Category::Zero), Sign(false),
              Significand(53, uint64_t(0)), Exponent(0) {}

  APFloat(Semantics S, double V) : Sem(S), Cat(Category::Normal), Sign(false),
                                     Significand(getSignificandBits(S) + 1, uint64_t(0)),
                                     Exponent(0) {
    fromDouble(V);
  }

  APFloat(float V) : Sem(Semantics::Float), Cat(Category::Normal), Sign(false),
                     Significand(24, uint64_t(0)), Exponent(0) {
    fromDouble(static_cast<double>(V));
  }

  APFloat(double V) : Sem(Semantics::Double), Cat(Category::Normal), Sign(false),
                      Significand(53, uint64_t(0)), Exponent(0) {
    fromDouble(V);
  }

  APFloat(Semantics S) : Sem(S), Cat(Category::Zero), Sign(false),
                         Significand(getSignificandBits(S) + 1, uint64_t(0)),
                         Exponent(0) {}

  Semantics getSemantics() const { return Sem; }
  unsigned getBitWidth() const { return getTotalBits(Sem); }

  double getRawValue() const { return toDouble(); }

  bool isZero() const { return Cat == Category::Zero; }
  bool isNaN() const { return Cat == Category::NaN; }
  bool isInfinity() const { return Cat == Category::Infinity; }
  bool isNegative() const { return Sign; }
  bool isFinite() const { return Cat == Category::Normal || Cat == Category::Zero; }
  bool isNormal() const { return Cat == Category::Normal; }
  Category getCategory() const { return Cat; }

  bool isNegativeZero() const { return Cat == Category::Zero && Sign; }
  bool isPositiveZero() const { return Cat == Category::Zero && !Sign; }

  static APFloat getZero(Semantics S, bool Negative = false) {
    APFloat R(S);
    R.Sign = Negative;
    return R;
  }

  static APFloat getNaN(Semantics S, bool Negative = false) {
    APFloat R(S);
    R.Cat = Category::NaN;
    R.Sign = Negative;
    return R;
  }

  static APFloat getInfinity(Semantics S, bool Negative = false) {
    APFloat R(S);
    R.Cat = Category::Infinity;
    R.Sign = Negative;
    return R;
  }

  APFloat operator+(const APFloat& O) const {
    if (Sem == O.Sem) {
      return APFloat(Sem, toDouble() + O.toDouble());
    }
    return APFloat(Sem, toDouble() + O.toDouble());
  }

  APFloat operator-(const APFloat& O) const {
    return APFloat(Sem, toDouble() - O.toDouble());
  }

  APFloat operator*(const APFloat& O) const {
    return APFloat(Sem, toDouble() * O.toDouble());
  }

  APFloat operator/(const APFloat& O) const {
    return APFloat(Sem, toDouble() / O.toDouble());
  }

  bool operator==(const APFloat& O) const {
    if (isNaN() || O.isNaN()) return false;
    if (Cat != O.Cat) return false;
    if (Cat == Category::Zero) return Sign == O.Sign;
    return Sign == O.Sign && Exponent == O.Exponent && Significand == O.Significand;
  }

  bool operator!=(const APFloat& O) const { return !(*this == O); }
  bool operator<(const APFloat& O) const { return toDouble() < O.toDouble(); }
  bool operator<=(const APFloat& O) const { return toDouble() <= O.toDouble(); }
  bool operator>(const APFloat& O) const { return toDouble() > O.toDouble(); }
  bool operator>=(const APFloat& O) const { return toDouble() >= O.toDouble(); }

  APFloat abs() const {
    APFloat R = *this;
    R.Sign = false;
    return R;
  }

  APFloat neg() const {
    APFloat R = *this;
    R.Sign = !R.Sign;
    return R;
  }

  APFloat convert(Semantics ToSem) const {
    APFloat Result(ToSem);
    Result.Cat = Cat;
    Result.Sign = Sign;
    if (Cat == Category::Normal) {
      Result.Exponent = Exponent;
      unsigned FromSig = getSignificandBits(Sem);
      unsigned ToSig = getSignificandBits(ToSem);
      if (ToSig >= FromSig) {
        Result.Significand = Significand.zext(ToSig + 1);
      } else {
        Result.Significand = Significand.trunc(ToSig + 1);
      }
    }
    return Result;
  }

  APInt bitcastToAPInt() const {
    unsigned Total = getTotalBits(Sem);
    unsigned ExpBits = getExponentBits(Sem);
    unsigned SigBits = getSignificandBits(Sem);
    int32_t Bias = getExponentBias(Sem);

    if (Cat == Category::Zero) {
      APInt Result(Total, uint64_t(0));
      if (Sign) Result.setBit(Total - 1);
      return Result;
    }
    if (Cat == Category::Infinity) {
      APInt Result(Total, uint64_t(0));
      if (Sign) Result.setBit(Total - 1);
      for (unsigned i = 0; i < ExpBits; ++i)
        Result.setBit(Total - 1 - ExpBits + i);
      return Result;
    }
    if (Cat == Category::NaN) {
      APInt Result(Total, uint64_t(0));
      if (Sign) Result.setBit(Total - 1);
      for (unsigned i = 0; i < ExpBits; ++i)
        Result.setBit(Total - 1 - ExpBits + i);
      if (Total <= 64) {
        Result = Result | APInt(Total, uint64_t(1));
      } else {
        Result.setBit(0);
      }
      return Result;
    }

    APInt Result(Total, uint64_t(0));
    if (Sign) Result.setBit(Total - 1);

    uint64_t EncodedExp = static_cast<uint64_t>(Exponent + Bias);
    for (unsigned i = 0; i < ExpBits; ++i) {
      if ((EncodedExp >> i) & 1)
        Result.setBit(Total - 1 - ExpBits + i);
    }

    APInt SigVal = Significand;
    if (Sem != Semantics::x87Extended) {
      SigVal.clearBit(getSignificandBits(Sem));
    }

    APInt ShiftedSig = SigVal << (Total - 1 - ExpBits - SigBits);
    Result = Result | ShiftedSig.trunc(Total);

    return Result;
  }

  static APFloat bitcastFromAPInt(const APInt& Bits) {
    unsigned BW = Bits.getBitWidth();
    Semantics S;
    switch (BW) {
    case 16: S = Semantics::Half; break;
    case 32: S = Semantics::Float; break;
    case 64: S = Semantics::Double; break;
    case 80: S = Semantics::x87Extended; break;
    case 128: S = Semantics::Quad; break;
    default: S = Semantics::Double; break;
    }

    APFloat Result(S);
    Result.Sign = Bits[BW - 1];

    unsigned ExpBits = getExponentBits(S);
    unsigned SigBits = getSignificandBits(S);
    int32_t Bias = getExponentBias(S);

    uint64_t EncodedExp = 0;
    for (unsigned i = 0; i < ExpBits; ++i) {
      if (Bits[BW - 1 - ExpBits + i])
        EncodedExp |= (uint64_t(1) << i);
    }

    APInt SigBitsVal = Bits.trunc(SigBits + (S == Semantics::x87Extended ? 1 : 0));
    Result.Significand = APInt(SigBits + 1, uint64_t(0));

    if (EncodedExp == 0) {
      if (SigBitsVal.isZero()) {
        Result.Cat = Category::Zero;
        return Result;
      }
      Result.Cat = Category::Normal;
      Result.Exponent = static_cast<int32_t>(1 - Bias);
      Result.Significand = SigBitsVal.zext(SigBits + 1);
      return Result;
    }

    if (EncodedExp == static_cast<uint64_t>((1 << ExpBits) - 1)) {
      bool AllSigZero = true;
      for (unsigned i = 0; i < SigBits; ++i) {
        if (Bits[i]) { AllSigZero = false; break; }
      }
      Result.Cat = AllSigZero ? Category::Infinity : Category::NaN;
      return Result;
    }

    Result.Cat = Category::Normal;
    Result.Exponent = static_cast<int32_t>(EncodedExp) - Bias;
    Result.Significand = APInt(SigBits + 1, uint64_t(0));
    Result.Significand.setBit(SigBits);
    for (unsigned i = 0; i < SigBits; ++i) {
      if (Bits[i]) Result.Significand.setBit(i);
    }
    return Result;
  }

  std::string toString() const {
    if (isNaN()) return "NaN";
    if (isInfinity()) return Sign ? "-Inf" : "+Inf";
    if (isZero()) return Sign ? "-0.0" : "0.0";
    return std::to_string(toDouble());
  }

  void print(raw_ostream& OS) const { OS << toString(); }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_ADT_APFLOAT_H
