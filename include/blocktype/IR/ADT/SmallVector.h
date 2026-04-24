#ifndef BLOCKTYPE_IR_ADT_SMALLVECTOR_H
#define BLOCKTYPE_IR_ADT_SMALLVECTOR_H

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace blocktype {
namespace ir {

template <typename T, unsigned N>
class SmallVector {
  alignas(T) unsigned char InlineStorage[sizeof(T) * N];
  T* BeginX;
  size_t SizeX = 0;
  size_t CapacityX;

  bool isInline() const {
    return BeginX == reinterpret_cast<const T*>(InlineStorage);
  }

  void destroyRange(T* First, T* Last) {
    for (; First != Last; ++First)
      First->~T();
  }

  void moveConstructRange(T* Src, T* SrcEnd, T* Dst) {
    for (; Src != SrcEnd; ++Src, ++Dst)
      new (Dst) T(std::move(*Src));
  }

  void grow(size_t MinSize) {
    size_t NewCap = CapacityX * 2;
    if (NewCap < MinSize) NewCap = MinSize;
    T* NewBuf = static_cast<T*>(::operator new(NewCap * sizeof(T)));
    moveConstructRange(BeginX, BeginX + SizeX, NewBuf);
    destroyRange(BeginX, BeginX + SizeX);
    if (!isInline())
      ::operator delete(BeginX);
    BeginX = NewBuf;
    CapacityX = NewCap;
  }

public:
  using iterator = T*;
  using const_iterator = const T*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using size_type = size_t;
  using value_type = T;
  using reference = T&;
  using const_reference = const T&;

  SmallVector() : BeginX(reinterpret_cast<T*>(InlineStorage)), CapacityX(N) {}

  SmallVector(const SmallVector& Other)
      : BeginX(reinterpret_cast<T*>(InlineStorage)), CapacityX(N) {
    reserve(Other.SizeX);
    for (size_t i = 0; i < Other.SizeX; ++i)
      new (BeginX + i) T(Other.BeginX[i]);
    SizeX = Other.SizeX;
  }

  SmallVector(SmallVector&& Other)
      : BeginX(reinterpret_cast<T*>(InlineStorage)), CapacityX(N) {
    if (Other.isInline()) {
      for (size_t i = 0; i < Other.SizeX; ++i)
        new (BeginX + i) T(std::move(Other.BeginX[i]));
      SizeX = Other.SizeX;
      Other.clear();
    } else {
      BeginX = Other.BeginX;
      SizeX = Other.SizeX;
      CapacityX = Other.CapacityX;
      Other.BeginX = reinterpret_cast<T*>(Other.InlineStorage);
      Other.SizeX = 0;
      Other.CapacityX = N;
    }
  }

  SmallVector(std::initializer_list<T> Init)
      : BeginX(reinterpret_cast<T*>(InlineStorage)), CapacityX(N) {
    reserve(Init.size());
    for (const auto& V : Init)
      new (BeginX + SizeX++) T(V);
  }

  explicit SmallVector(size_t Count, const T& Val = T())
      : BeginX(reinterpret_cast<T*>(InlineStorage)), CapacityX(N) {
    reserve(Count);
    for (size_t i = 0; i < Count; ++i)
      new (BeginX + SizeX++) T(Val);
  }

  SmallVector& operator=(const SmallVector& Other) {
    if (this != &Other) {
      clear();
      reserve(Other.SizeX);
      for (size_t i = 0; i < Other.SizeX; ++i)
        new (BeginX + i) T(Other.BeginX[i]);
      SizeX = Other.SizeX;
    }
    return *this;
  }

  SmallVector& operator=(SmallVector&& Other) {
    if (this != &Other) {
      clear();
      if (!isInline())
        ::operator delete(BeginX);
      if (Other.isInline()) {
        BeginX = reinterpret_cast<T*>(InlineStorage);
        CapacityX = N;
        for (size_t i = 0; i < Other.SizeX; ++i)
          new (BeginX + i) T(std::move(Other.BeginX[i]));
        SizeX = Other.SizeX;
        Other.clear();
      } else {
        BeginX = Other.BeginX;
        SizeX = Other.SizeX;
        CapacityX = Other.CapacityX;
        Other.BeginX = reinterpret_cast<T*>(Other.InlineStorage);
        Other.SizeX = 0;
        Other.CapacityX = N;
      }
    }
    return *this;
  }

  ~SmallVector() {
    clear();
    if (!isInline())
      ::operator delete(BeginX);
  }

  size_t size() const { return SizeX; }
  size_t capacity() const { return CapacityX; }
  bool empty() const { return SizeX == 0; }

  T* data() { return BeginX; }
  const T* data() const { return BeginX; }

  T& operator[](size_t i) { return BeginX[i]; }
  const T& operator[](size_t i) const { return BeginX[i]; }

  T& front() { return BeginX[0]; }
  const T& front() const { return BeginX[0]; }
  T& back() { return BeginX[SizeX - 1]; }
  const T& back() const { return BeginX[SizeX - 1]; }

  iterator begin() { return BeginX; }
  iterator end() { return BeginX + SizeX; }
  const_iterator begin() const { return BeginX; }
  const_iterator end() const { return BeginX + SizeX; }
  const_iterator cbegin() const { return BeginX; }
  const_iterator cend() const { return BeginX + SizeX; }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
  const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }

  void push_back(const T& Val) {
    if (SizeX >= CapacityX) grow(SizeX + 1);
    new (BeginX + SizeX) T(Val);
    ++SizeX;
  }

  void push_back(T&& Val) {
    if (SizeX >= CapacityX) grow(SizeX + 1);
    new (BeginX + SizeX) T(std::move(Val));
    ++SizeX;
  }

  template <typename... Args>
  T& emplace_back(Args&&... args) {
    if (SizeX >= CapacityX) grow(SizeX + 1);
    new (BeginX + SizeX) T(std::forward<Args>(args)...);
    return BeginX[SizeX++];
  }

  void pop_back() {
    --SizeX;
    (BeginX + SizeX)->~T();
  }

  void clear() {
    destroyRange(BeginX, BeginX + SizeX);
    SizeX = 0;
  }

  void resize(size_t Count) {
    if (Count < SizeX) {
      destroyRange(BeginX + Count, BeginX + SizeX);
      SizeX = Count;
    } else if (Count > SizeX) {
      reserve(Count);
      for (size_t i = SizeX; i < Count; ++i)
        new (BeginX + i) T();
      SizeX = Count;
    }
  }

  void resize(size_t Count, const T& Val) {
    if (Count < SizeX) {
      destroyRange(BeginX + Count, BeginX + SizeX);
      SizeX = Count;
    } else if (Count > SizeX) {
      reserve(Count);
      for (size_t i = SizeX; i < Count; ++i)
        new (BeginX + i) T(Val);
      SizeX = Count;
    }
  }

  void reserve(size_t Count) {
    if (Count > CapacityX) grow(Count);
  }

  iterator insert(iterator Pos, const T& Val) {
    size_t Idx = Pos - BeginX;
    if (SizeX >= CapacityX) grow(SizeX + 1);
    if (Idx < SizeX) {
      new (BeginX + SizeX) T(std::move(BeginX[SizeX - 1]));
      for (size_t i = SizeX - 1; i > Idx; --i)
        BeginX[i] = std::move(BeginX[i - 1]);
      BeginX[Idx] = Val;
    } else {
      new (BeginX + Idx) T(Val);
    }
    ++SizeX;
    return BeginX + Idx;
  }

  iterator erase(iterator Pos) {
    size_t Idx = Pos - BeginX;
    (BeginX + Idx)->~T();
    for (size_t i = Idx; i < SizeX - 1; ++i) {
      new (BeginX + i) T(std::move(BeginX[i + 1]));
      (BeginX + i + 1)->~T();
    }
    --SizeX;
    return BeginX + Idx;
  }

  iterator erase(iterator First, iterator Last) {
    size_t Idx = First - BeginX;
    size_t Count = Last - First;
    destroyRange(BeginX + Idx, BeginX + Idx + Count);
    for (size_t i = Idx + Count; i < SizeX; ++i) {
      new (BeginX + i - Count) T(std::move(BeginX[i]));
      (BeginX + i)->~T();
    }
    SizeX -= Count;
    return BeginX + Idx;
  }

  void swap(SmallVector& Other) {
    if (isInline() && Other.isInline()) {
      SmallVector Tmp(std::move(Other));
      Other = std::move(*this);
      *this = std::move(Tmp);
    } else if (isInline()) {
      T* OtherBuf = Other.BeginX;
      size_t OtherSz = Other.SizeX;
      size_t OtherCap = Other.CapacityX;
      Other.BeginX = reinterpret_cast<T*>(Other.InlineStorage);
      Other.CapacityX = N;
      for (size_t i = 0; i < OtherSz; ++i)
        new (Other.BeginX + i) T(std::move(OtherBuf[i]));
      Other.SizeX = OtherSz;
      destroyRange(BeginX, BeginX + SizeX);
      BeginX = OtherBuf;
      CapacityX = OtherCap;
      Other.SizeX = 0;
    } else if (Other.isInline()) {
      Other.swap(*this);
    } else {
      std::swap(BeginX, Other.BeginX);
      std::swap(SizeX, Other.SizeX);
      std::swap(CapacityX, Other.CapacityX);
    }
  }

  bool operator==(const SmallVector& Other) const {
    if (SizeX != Other.SizeX) return false;
    for (size_t i = 0; i < SizeX; ++i)
      if (!(BeginX[i] == Other.BeginX[i])) return false;
    return true;
  }

  bool operator!=(const SmallVector& Other) const { return !(*this == Other); }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_ADT_SMALLVECTOR_H
