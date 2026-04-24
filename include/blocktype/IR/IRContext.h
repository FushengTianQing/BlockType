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
