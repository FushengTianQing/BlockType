#ifndef BLOCKTYPE_IR_ADT_ARRAYREF_H
#define BLOCKTYPE_IR_ADT_ARRAYREF_H

#include <cstddef>
#include <iterator>
#include <vector>

namespace blocktype {
namespace ir {

template <typename T, unsigned N> class SmallVector;

template <typename T>
class ArrayRef {
  const T* Data = nullptr;
  size_t Len = 0;

public:
  using iterator = const T*;
  using const_iterator = const T*;
  using size_type = size_t;
  using value_type = T;

  ArrayRef() = default;
  ArrayRef(const T* D, size_t L) : Data(D), Len(L) {}
  ArrayRef(const T* Begin, const T* End) : Data(Begin), Len(End - Begin) {}
  ArrayRef(const T& Single) : Data(&Single), Len(1) {}
  ArrayRef(const std::vector<T>& Vec) : Data(Vec.data()), Len(Vec.size()) {}

  template <unsigned N>
  ArrayRef(const SmallVector<T, N>& SV) : Data(SV.data()), Len(SV.size()) {}

  const T* data() const { return Data; }
  size_t size() const { return Len; }
  bool empty() const { return Len == 0; }

  const T& operator[](size_t i) const { return Data[i]; }
  const T& front() const { return Data[0]; }
  const T& back() const { return Data[Len - 1]; }

  const_iterator begin() const { return Data; }
  const_iterator end() const { return Data + Len; }
  const_iterator cbegin() const { return Data; }
  const_iterator cend() const { return Data + Len; }

  ArrayRef slice(size_t Start, size_t Length) const {
    return ArrayRef(Data + Start, Length);
  }

  ArrayRef slice(size_t Start) const {
    return ArrayRef(Data + Start, Len - Start);
  }

  bool operator==(ArrayRef Other) const {
    if (Len != Other.Len) return false;
    for (size_t i = 0; i < Len; ++i)
      if (!(Data[i] == Other.Data[i])) return false;
    return true;
  }

  bool operator!=(ArrayRef Other) const { return !(*this == Other); }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_ADT_ARRAYREF_H
