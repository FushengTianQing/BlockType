//===--- RegisterAllocator.cpp - Register Allocator Impl ----*- C++ -*-===//

#include "blocktype/Backend/RegisterAllocator.h"

namespace blocktype::backend {

bool TargetRegisterInfo::isCalleeSaved(unsigned Reg) const {
  for (auto R : CalleeSavedRegs_) {
    if (R == Reg) return true;
  }
  return false;
}

bool TargetRegisterInfo::isCallerSaved(unsigned Reg) const {
  for (auto R : CallerSavedRegs_) {
    if (R == Reg) return true;
  }
  return false;
}

unsigned TargetRegisterInfo::getRegClass(unsigned Reg) const {
  auto It = RegClassMap_.find(Reg);
  if (It != RegClassMap_.end()) return (*It).second;
  return 0;  // Default register class
}

// === Concrete register allocators (stubs) ===

class GreedyRegAlloc : public RegisterAllocator {
public:
  ir::StringRef getName() const override { return "greedy"; }
  RegAllocStrategy getStrategy() const override { return RegAllocStrategy::Greedy; }
  bool allocate(TargetFunction& F, const TargetRegisterInfo& TRI) override {
    (void)F; (void)TRI;
    return true;  // stub
  }
};

class BasicRegAlloc : public RegisterAllocator {
public:
  ir::StringRef getName() const override { return "basic"; }
  RegAllocStrategy getStrategy() const override { return RegAllocStrategy::Basic; }
  bool allocate(TargetFunction& F, const TargetRegisterInfo& TRI) override {
    (void)F; (void)TRI;
    return true;  // stub
  }
};

// === Factory ===

std::unique_ptr<RegisterAllocator> RegAllocFactory::create(RegAllocStrategy Strategy) {
  switch (Strategy) {
    case RegAllocStrategy::Greedy:
      return std::make_unique<GreedyRegAlloc>();
    case RegAllocStrategy::Basic:
      return std::make_unique<BasicRegAlloc>();
    default:
      return nullptr;
  }
}

} // namespace blocktype::backend
