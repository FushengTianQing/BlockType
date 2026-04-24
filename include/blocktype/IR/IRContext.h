#ifndef BLOCKTYPE_IR_IRCONTEXT_H
#define BLOCKTYPE_IR_IRCONTEXT_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRTypeContext.h"

namespace blocktype {
namespace ir {

class IRModule;

class BumpPtrAllocator {
  struct Block {
    char* Ptr;
    size_t Size;
    std::unique_ptr<Block> Next;
    Block(size_t S) : Ptr(static_cast<char*>(::operator new(S))), Size(S), Next(nullptr) {}
    ~Block() { ::operator delete(Ptr); }
  };
  std::unique_ptr<Block> Head;
  char* Current = nullptr;
  size_t Offset = 0;
  static constexpr size_t DefaultBlockSize = 4096;

public:
  BumpPtrAllocator() { allocBlock(DefaultBlockSize); }

  void* Allocate(size_t Size, size_t Alignment) {
    size_t AlignedOffset = (Offset + Alignment - 1) & ~(Alignment - 1);
    if (AlignedOffset + Size > Head->Size) {
      size_t BlockSize = std::max(Size * 2, DefaultBlockSize);
      allocBlock(BlockSize);
      AlignedOffset = 0;
    }
    void* Mem = Head->Ptr + AlignedOffset;
    Offset = AlignedOffset + Size;
    return Mem;
  }

  size_t getTotalMemory() const {
    size_t Total = 0;
    for (Block* B = Head.get(); B; B = B->Next.get())
      Total += B->Size;
    return Total;
  }

  ~BumpPtrAllocator() = default;

private:
  void allocBlock(size_t Size) {
    auto NewBlock = std::make_unique<Block>(Size);
    NewBlock->Next = std::move(Head);
    Head = std::move(NewBlock);
    Current = Head->Ptr;
    Offset = 0;
  }
};

enum class IRThreadingMode {
  SingleThread,
  MultiInstance,
  SharedReadOnly
};

class IRContext {
  BumpPtrAllocator Allocator;
  std::vector<std::function<void()>> Cleanups;
  IRTypeContext TypeCtx;
  IRThreadingMode ThreadingMode = IRThreadingMode::SingleThread;

public:
  template <typename T, typename... Args>
  T* create(Args&&... args) {
    void* Mem = Allocator.Allocate(sizeof(T), alignof(T));
    T* Node = new (Mem) T(std::forward<Args>(args)...);
    return Node;
  }

  void addCleanup(std::function<void()> Callback) {
    Cleanups.push_back(std::move(Callback));
  }

  StringRef saveString(StringRef Str);
  IRTypeContext& getTypeContext() { return TypeCtx; }
  size_t getMemoryUsage() const { return Allocator.getTotalMemory(); }
  void setThreadingMode(IRThreadingMode Mode) { ThreadingMode = Mode; }
  IRThreadingMode getThreadingMode() const { return ThreadingMode; }
  void sealModule(IRModule& M);

  ~IRContext() {
    for (auto It = Cleanups.rbegin(); It != Cleanups.rend(); ++It)
      (*It)();
  }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRCONTEXT_H
