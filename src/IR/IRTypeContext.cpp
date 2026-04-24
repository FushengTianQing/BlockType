#include "blocktype/IR/IRTypeContext.h"

#include <cstddef>

namespace blocktype {
namespace ir {

IRTypeContext::IRTypeContext()
  : VoidType(), BoolType(1), NumTypesCreated(0) {
  NumTypesCreated += 2;
}

IRIntegerType* IRTypeContext::getIntType(unsigned BitWidth) {
  auto It = IntTypes.find(BitWidth);
  if (It != IntTypes.end()) return (*It).second.get();
  auto T = std::make_unique<IRIntegerType>(BitWidth);
  auto* Ptr = T.get();
  IntTypes[BitWidth] = std::move(T);
  ++NumTypesCreated;
  return Ptr;
}

IRFloatType* IRTypeContext::getFloatType(unsigned BitWidth) {
  auto It = FloatTypes.find(BitWidth);
  if (It != FloatTypes.end()) return (*It).second.get();
  auto T = std::make_unique<IRFloatType>(BitWidth);
  auto* Ptr = T.get();
  FloatTypes[BitWidth] = std::move(T);
  ++NumTypesCreated;
  return Ptr;
}

IRPointerType* IRTypeContext::getPointerType(IRType* Pointee, unsigned AddressSpace) {
  auto T = std::make_unique<IRPointerType>(Pointee, AddressSpace);
  return PointerTypes.findOrInsert(std::move(T));
}

IRArrayType* IRTypeContext::getArrayType(IRType* Element, uint64_t Count) {
  auto T = std::make_unique<IRArrayType>(Count, Element);
  return ArrayTypes.findOrInsert(std::move(T));
}

IRVectorType* IRTypeContext::getVectorType(IRType* Element, unsigned Count) {
  auto T = std::make_unique<IRVectorType>(Count, Element);
  return VectorTypes.findOrInsert(std::move(T));
}

IRStructType* IRTypeContext::getStructType(StringRef Name,
                                            SmallVector<IRType*, 16> Elems,
                                            bool Packed) {
  std::string Key = Name.str();
  auto It = NamedStructTypes.find(Key);
  if (It != NamedStructTypes.end()) return It->Value.get();
  auto T = std::make_unique<IRStructType>(Name, std::move(Elems), Packed);
  auto* Ptr = T.get();
  NamedStructTypes[Key] = std::move(T);
  ++NumTypesCreated;
  return Ptr;
}

IRStructType* IRTypeContext::getAnonStructType(SmallVector<IRType*, 16> Elems, bool Packed) {
  static unsigned AnonCounter = 0;
  std::string Name = "__anon_struct_" + std::to_string(AnonCounter++);
  return getStructType(Name, std::move(Elems), Packed);
}

bool IRTypeContext::setStructBody(IRStructType* S,
                                   SmallVector<IRType*, 16> Elems,
                                   bool Packed) {
  assert(S && "StructType cannot be null");
  S->setBody(std::move(Elems), Packed);
  return true;
}

IRFunctionType* IRTypeContext::getFunctionType(IRType* Ret,
                                                SmallVector<IRType*, 8> Params,
                                                bool VarArg) {
  auto T = std::make_unique<IRFunctionType>(Ret, Params, VarArg);
  return FunctionTypes.findOrInsert(std::move(T));
}

IROpaqueType* IRTypeContext::getOpaqueType(StringRef Name) {
  std::string Key = Name.str();
  auto It = OpaqueTypes.find(Key);
  if (It != OpaqueTypes.end()) return It->Value.get();
  auto T = std::make_unique<IROpaqueType>(Name);
  auto* Ptr = T.get();
  OpaqueTypes[Key] = std::move(T);
  ++NumTypesCreated;
  return Ptr;
}

IRStructType* IRTypeContext::getStructTypeByName(StringRef Name) const {
  auto It = NamedStructTypes.find(Name.str());
  return It != NamedStructTypes.end() ? It->Value.get() : nullptr;
}

IROpaqueType* IRTypeContext::getOpaqueTypeByName(StringRef Name) const {
  auto It = OpaqueTypes.find(Name.str());
  return It != OpaqueTypes.end() ? It->Value.get() : nullptr;
}

size_t IRTypeContext::getMemoryUsage() const {
  size_t Total = sizeof(IRTypeContext);
  Total += IntTypes.size() * (sizeof(unsigned) + sizeof(std::unique_ptr<IRIntegerType>));
  Total += FloatTypes.size() * (sizeof(unsigned) + sizeof(std::unique_ptr<IRFloatType>));
  Total += PointerTypes.size() * sizeof(std::unique_ptr<IRPointerType>);
  Total += ArrayTypes.size() * sizeof(std::unique_ptr<IRArrayType>);
  Total += VectorTypes.size() * sizeof(std::unique_ptr<IRVectorType>);
  Total += FunctionTypes.size() * sizeof(std::unique_ptr<IRFunctionType>);
  for (auto& P : NamedStructTypes)
    Total += P.Key.size() + sizeof(std::unique_ptr<IRStructType>);
  for (auto& P : OpaqueTypes)
    Total += P.Key.size() + sizeof(std::unique_ptr<IROpaqueType>);
  return Total;
}

} // namespace ir
} // namespace blocktype
