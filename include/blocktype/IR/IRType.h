#ifndef BLOCKTYPE_IR_IRTYPE_H
#define BLOCKTYPE_IR_IRTYPE_H

#include <cassert>
#include <cstdint>
#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/TargetLayout.h"

namespace blocktype {
namespace ir {

namespace dialect {

enum class DialectID : uint8_t {
  Core     = 0,
  Cpp      = 1,
  Target   = 2,
  Debug    = 3,
  Metadata = 4,
};

} // namespace dialect

class IRType {
public:
  enum Kind : uint8_t {
    Void = 0, Bool = 1, Integer = 2, Float = 3,
    Pointer = 4, Array = 5, Struct = 6, Function = 7,
    Vector = 8, Opaque = 9
  };

  virtual ~IRType() = default;

  Kind getKind() const { return KindVal; }
  dialect::DialectID getDialect() const { return DialectVal; }

  virtual bool equals(const IRType* Other) const = 0;
  virtual std::string toString() const = 0;
  virtual uint64_t getSizeInBits(const TargetLayout& Layout) const = 0;
  virtual uint64_t getAlignInBits(const TargetLayout& Layout) const = 0;

  bool isVoid() const { return KindVal == Void; }
  bool isBool() const { return KindVal == Bool; }
  bool isInteger() const { return KindVal == Integer; }
  bool isFloat() const { return KindVal == Float; }
  bool isPointer() const { return KindVal == Pointer; }
  bool isArray() const { return KindVal == Array; }
  bool isStruct() const { return KindVal == Struct; }
  bool isFunction() const { return KindVal == Function; }
  bool isVector() const { return KindVal == Vector; }
  bool isOpaque() const { return KindVal == Opaque; }

  static bool classof(const IRType* T) { return true; }

  IRType(const IRType&) = delete;
  IRType& operator=(const IRType&) = delete;

protected:
  IRType(Kind K, dialect::DialectID D = dialect::DialectID::Core)
    : KindVal(K), DialectVal(D) {}
  Kind KindVal;
  dialect::DialectID DialectVal;
};

class IRVoidType : public IRType {
public:
  IRVoidType() : IRType(Void) {}
  bool equals(const IRType* Other) const override { return Other->isVoid(); }
  std::string toString() const override { return "void"; }
  uint64_t getSizeInBits(const TargetLayout&) const override { return 0; }
  uint64_t getAlignInBits(const TargetLayout&) const override { return 0; }
  static bool classof(const IRType* T) { return T->getKind() == Void; }
};

class IRIntegerType : public IRType {
  unsigned BitWidth;
public:
  explicit IRIntegerType(unsigned BW)
    : IRType(Integer), BitWidth(BW) {
    assert((BW == 1 || BW == 8 || BW == 16 || BW == 32 || BW == 64 || BW == 128)
           && "Invalid integer bit width");
  }
  unsigned getBitWidth() const { return BitWidth; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout&) const override { return BitWidth; }
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Integer; }
};

class IRFloatType : public IRType {
  unsigned BitWidth;
public:
  explicit IRFloatType(unsigned BW)
    : IRType(Float), BitWidth(BW) {
    assert((BW == 16 || BW == 32 || BW == 64 || BW == 80 || BW == 128)
           && "Invalid float bit width");
  }
  unsigned getBitWidth() const { return BitWidth; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout&) const override { return BitWidth; }
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Float; }
};

class IRPointerType : public IRType {
  IRType* PointeeType;
  unsigned AddressSpace;
public:
  IRPointerType(IRType* P, unsigned AS = 0)
    : IRType(Pointer), PointeeType(P), AddressSpace(AS) {
    assert(P && "PointeeType cannot be null");
  }
  IRType* getPointeeType() const { return PointeeType; }
  unsigned getAddressSpace() const { return AddressSpace; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout& Layout) const override;
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Pointer; }
};

class IRArrayType : public IRType {
  uint64_t NumElements;
  IRType* ElementType;
public:
  IRArrayType(uint64_t N, IRType* E)
    : IRType(Array), NumElements(N), ElementType(E) {
    assert(E && "ElementType cannot be null");
    assert(N > 0 && "Array must have at least 1 element");
  }
  uint64_t getNumElements() const { return NumElements; }
  IRType* getElementType() const { return ElementType; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout& Layout) const override;
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Array; }
};

class IRStructType : public IRType {
  friend class IRTypeContext;
  std::string Name;
  SmallVector<IRType*, 16> FieldTypes;
  bool IsPacked;
  mutable bool IsLayoutComputed = false;
  mutable SmallVector<uint64_t, 16> FieldOffsets;
public:
  IRStructType(StringRef N, SmallVector<IRType*, 16> F, bool P = false)
    : IRType(Struct), Name(N.str()), FieldTypes(std::move(F)), IsPacked(P) {}
  StringRef getName() const { return Name; }
  ArrayRef<IRType*> getElements() const { return FieldTypes; }
  unsigned getNumFields() const { return static_cast<unsigned>(FieldTypes.size()); }
  IRType* getFieldType(unsigned i) const { return FieldTypes[i]; }
  bool isPacked() const { return IsPacked; }
  void setBody(SmallVector<IRType*, 16> Elems, bool Packed = false) {
    FieldTypes = std::move(Elems);
    IsPacked = Packed;
    IsLayoutComputed = false;
    FieldOffsets.clear();
  }
  uint64_t getFieldOffset(unsigned i, const TargetLayout& Layout) const;
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout& Layout) const override;
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Struct; }
};

class IRFunctionType : public IRType {
  IRType* ReturnType;
  SmallVector<IRType*, 8> ParamTypes;
  bool IsVarArg;
public:
  IRFunctionType(IRType* R, SmallVector<IRType*, 8> P, bool VA = false)
    : IRType(Function), ReturnType(R), ParamTypes(std::move(P)), IsVarArg(VA) {
    assert(R && "ReturnType cannot be null");
  }
  IRType* getReturnType() const { return ReturnType; }
  ArrayRef<IRType*> getParamTypes() const { return ParamTypes; }
  unsigned getNumParams() const { return static_cast<unsigned>(ParamTypes.size()); }
  IRType* getParamType(unsigned i) const { return ParamTypes[i]; }
  bool isVarArg() const { return IsVarArg; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout&) const override { return 0; }
  uint64_t getAlignInBits(const TargetLayout&) const override { return 0; }
  static bool classof(const IRType* T) { return T->getKind() == Function; }
};

class IRVectorType : public IRType {
  unsigned NumElements;
  IRType* ElementType;
public:
  IRVectorType(unsigned N, IRType* E)
    : IRType(Vector), NumElements(N), ElementType(E) {
    assert(E && "ElementType cannot be null");
    assert((N == 2 || N == 4 || N == 8 || N == 16 || N == 32 || N == 64)
           && "Vector NumElements must be power of 2");
  }
  unsigned getNumElements() const { return NumElements; }
  IRType* getElementType() const { return ElementType; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout& Layout) const override;
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Vector; }
};

class IROpaqueType : public IRType {
  std::string Name;
public:
  explicit IROpaqueType(StringRef N)
    : IRType(Opaque), Name(N.str()) {}
  StringRef getName() const { return Name; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout&) const override;
  uint64_t getAlignInBits(const TargetLayout&) const override;
  static bool classof(const IRType* T) { return T->getKind() == Opaque; }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRTYPE_H
