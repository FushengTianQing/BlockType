#ifndef BLOCKTYPE_IR_IRTYPECONTEXT_H
#define BLOCKTYPE_IR_IRTYPECONTEXT_H

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRType.h"

namespace blocktype {
namespace ir {

class IRTypeContext {
  DenseMap<unsigned, std::unique_ptr<IRIntegerType>> IntTypes;
  DenseMap<unsigned, std::unique_ptr<IRFloatType>> FloatTypes;
  FoldingSet<IRPointerType> PointerTypes;
  FoldingSet<IRArrayType> ArrayTypes;
  FoldingSet<IRVectorType> VectorTypes;
  StringMap<std::unique_ptr<IRStructType>> NamedStructTypes;
  StringMap<std::unique_ptr<IROpaqueType>> OpaqueTypes;
  FoldingSet<IRFunctionType> FunctionTypes;
  IRVoidType VoidType;
  IRIntegerType BoolType;
  unsigned NumTypesCreated = 0;

public:
  IRTypeContext();

  IRVoidType* getVoidType() { return &VoidType; }
  IRIntegerType* getBoolType() { return &BoolType; }
  IRIntegerType* getIntType(unsigned BitWidth);
  IRIntegerType* getInt1Ty()   { return getIntType(1); }
  IRIntegerType* getInt8Ty()   { return getIntType(8); }
  IRIntegerType* getInt16Ty()  { return getIntType(16); }
  IRIntegerType* getInt32Ty()  { return getIntType(32); }
  IRIntegerType* getInt64Ty()  { return getIntType(64); }
  IRIntegerType* getInt128Ty() { return getIntType(128); }
  IRFloatType* getFloatType(unsigned BitWidth);
  IRFloatType* getHalfTy()     { return getFloatType(16); }
  IRFloatType* getFloatTy()    { return getFloatType(32); }
  IRFloatType* getDoubleTy()   { return getFloatType(64); }
  IRFloatType* getFloat80Ty()  { return getFloatType(80); }
  IRFloatType* getFloat128Ty() { return getFloatType(128); }
  IRPointerType* getPointerType(IRType* Pointee, unsigned AddressSpace = 0);
  IRArrayType* getArrayType(IRType* Element, uint64_t Count);
  IRVectorType* getVectorType(IRType* Element, unsigned Count);
  IRStructType* getStructType(StringRef Name, SmallVector<IRType*, 16> Elems, bool Packed = false);
  IRStructType* getAnonStructType(SmallVector<IRType*, 16> Elems, bool Packed = false);
  bool setStructBody(IRStructType* S, SmallVector<IRType*, 16> Elems, bool Packed = false);
  IRFunctionType* getFunctionType(IRType* Ret, SmallVector<IRType*, 8> Params, bool VarArg = false);
  IROpaqueType* getOpaqueType(StringRef Name);
  IRStructType* getStructTypeByName(StringRef Name) const;
  IROpaqueType* getOpaqueTypeByName(StringRef Name) const;
  unsigned getNumTypesCreated() const { return NumTypesCreated; }
  size_t getMemoryUsage() const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRTYPECONTEXT_H
