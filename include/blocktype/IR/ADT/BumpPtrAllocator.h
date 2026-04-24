#ifndef BLOCKTYPE_IR_ADT_BUMP_PTR_ALLOCATOR_H
#define BLOCKTYPE_IR_ADT_BUMP_PTR_ALLOCATOR_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace blocktype {
namespace ir {

class BumpPtrAllocator {
  struct Slab {
    char* Buffer;
    size_t Size;
    Slab(size_t S) : Buffer(static_cast<char*>(::operator new(S))), Size(S) {}
    ~Slab() { ::operator delete(Buffer); }
  };

  std::vector<std::unique_ptr<Slab>> Slabs;
  char* CurrentPtr = nullptr;
  size_t CurrentEnd = 0;
  size_t CurrentOffset = 0;
  size_t TotalAllocated = 0;

  static constexpr size_t DefaultSlabSize = 4096;
  static constexpr size_t MinSlabSize = 512;
  static constexpr size_t MaxSlabSize = 1 << 20;

  size_t nextSlabSize(size_t MinSize) {
    size_t NewSize = DefaultSlabSize;
    if (!Slabs.empty()) {
      size_t LastSize = Slabs.back()->Size;
      NewSize = LastSize * 2;
      if (NewSize > MaxSlabSize) NewSize = MaxSlabSize;
    }
    if (NewSize < MinSize) NewSize = MinSize;
    return NewSize;
  }

  void allocateNewSlab(size_t MinSize) {
    size_t SlabSize = nextSlabSize(MinSize);
    auto NewSlab = std::make_unique<Slab>(SlabSize);
    CurrentPtr = NewSlab->Buffer;
    CurrentEnd = SlabSize;
    CurrentOffset = 0;
    TotalAllocated += SlabSize;
    Slabs.push_back(std::move(NewSlab));
  }

public:
  BumpPtrAllocator() { allocateNewSlab(DefaultSlabSize); }

  BumpPtrAllocator(const BumpPtrAllocator&) = delete;
  BumpPtrAllocator& operator=(const BumpPtrAllocator&) = delete;

  ~BumpPtrAllocator() = default;

  void* Allocate(size_t Size, size_t Alignment) {
    size_t AlignedOffset = (CurrentOffset + Alignment - 1) & ~(Alignment - 1);
    if (AlignedOffset + Size > CurrentEnd) {
      allocateNewSlab(Size + Alignment);
      AlignedOffset = 0;
    }
    void* Mem = CurrentPtr + AlignedOffset;
    CurrentOffset = AlignedOffset + Size;
    return Mem;
  }

  template <typename T, typename... Args>
  T* Allocate(Args&&... args) {
    void* Mem = Allocate(sizeof(T), alignof(T));
    return new (Mem) T(std::forward<Args>(args)...);
  }

  size_t getTotalMemory() const { return TotalAllocated; }

  void Reset() {
    for (auto& S : Slabs) {
      CurrentPtr = S->Buffer;
      CurrentEnd = S->Size;
      CurrentOffset = 0;
      break;
    }
    if (!Slabs.empty()) {
      for (size_t i = Slabs.size() - 1; i > 0; --i)
        Slabs.pop_back();
    }
    TotalAllocated = Slabs.empty() ? 0 : Slabs[0]->Size;
  }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_ADT_BUMP_PTR_ALLOCATOR_H
