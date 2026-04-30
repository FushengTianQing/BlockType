#include "blocktype/IR/DependencyGraph.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRInstruction.h"

#include <cstring>

namespace blocktype {
namespace ir {

// ============================================================================
// FNV-1a 64-bit 哈希实现
// ============================================================================

static constexpr uint64_t FNV64OffsetBasis = 14695981039346656037ULL;
static constexpr uint64_t FNV64Prime = 1099511628211ULL;

Fingerprint computeFingerprint(StringRef Data) {
  uint64_t Hash = FNV64OffsetBasis;
  for (size_t i = 0; i < Data.size(); ++i) {
    Hash ^= static_cast<uint64_t>(static_cast<unsigned char>(Data[i]));
    Hash *= FNV64Prime;
  }
  return static_cast<Fingerprint>(Hash);
}

Fingerprint computeFingerprint(const IRModule& M) {
  uint64_t Hash = FNV64OffsetBasis;

  // 混入模块名
  StringRef Name = M.getName();
  for (size_t i = 0; i < Name.size(); ++i) {
    Hash ^= static_cast<uint64_t>(static_cast<unsigned char>(Name[i]));
    Hash *= FNV64Prime;
  }

  // 混入 TargetTriple
  StringRef Triple = M.getTargetTriple();
  for (size_t i = 0; i < Triple.size(); ++i) {
    Hash ^= static_cast<uint64_t>(static_cast<unsigned char>(Triple[i]));
    Hash *= FNV64Prime;
  }

  // 混入每个函数的指纹
  for (const auto& Fn : M.getFunctions()) {
    Fingerprint FnFP = computeFingerprint(*Fn);
    Hash ^= FnFP;
    Hash *= FNV64Prime;
  }

  // 混入全局变量数量
  Hash ^= static_cast<uint64_t>(M.getGlobals().size());
  Hash *= FNV64Prime;

  return static_cast<Fingerprint>(Hash);
}

Fingerprint computeFingerprint(const IRFunction& F) {
  uint64_t Hash = FNV64OffsetBasis;

  // 混入函数名
  StringRef Name = F.getName();
  for (size_t i = 0; i < Name.size(); ++i) {
    Hash ^= static_cast<uint64_t>(static_cast<unsigned char>(Name[i]));
    Hash *= FNV64Prime;
  }

  // 混入参数数量
  Hash ^= static_cast<uint64_t>(F.getNumArgs());
  Hash *= FNV64Prime;

  // 混入每个基本块中的指令
  for (const auto& BB : F.getBasicBlocks()) {
    for (const auto& Inst : BB->getInstList()) {
      Hash ^= static_cast<uint64_t>(static_cast<uint16_t>(Inst->getOpcode()));
      Hash *= FNV64Prime;
      Hash ^= static_cast<uint64_t>(Inst->getNumOperands());
      Hash *= FNV64Prime;
    }
  }

  return static_cast<Fingerprint>(Hash);
}

// ============================================================================
// DependencyGraph 实现
// ============================================================================

void DependencyGraph::recordDependency(QueryID Dependent, ArrayRef<QueryID> Deps) {
  // 确保被依赖者也在 Dependencies 表中
  if (!Dependencies.contains(Dependent)) {
    Dependencies[Dependent] = SmallVector<QueryID, 4>();
  }

  for (QueryID Dep : Deps) {
    // 添加正向依赖：Dependent -> Dep
    auto& DepList = Dependencies[Dependent];
    bool Found = false;
    for (QueryID Existing : DepList) {
      if (Existing == Dep) { Found = true; break; }
    }
    if (!Found) {
      DepList.push_back(Dep);
    }

    // 添加反向依赖：Dep -> Dependent
    if (!Dependencies.contains(Dep)) {
      Dependencies[Dep] = SmallVector<QueryID, 4>();
    }
    auto& RevList = Dependents[Dep];
    Found = false;
    for (QueryID Existing : RevList) {
      if (Existing == Dependent) { Found = true; break; }
    }
    if (!Found) {
      RevList.push_back(Dependent);
    }
  }

  // 确保被依赖者也有 Dependents 表项
  for (QueryID Dep : Deps) {
    if (!Dependents.contains(Dep)) {
      Dependents[Dep] = SmallVector<QueryID, 4>();
    }
  }

  // 确保 Dependent 也有 Dependents 表项
  if (!Dependents.contains(Dependent)) {
    Dependents[Dependent] = SmallVector<QueryID, 4>();
  }
}

SmallVector<QueryID, 4> DependencyGraph::getDependencies(QueryID ID) const {
  auto It = Dependencies.find(ID);
  if (It != Dependencies.end()) {
    return (*It).second;
  }
  return SmallVector<QueryID, 4>();
}

SmallVector<QueryID, 4> DependencyGraph::getDependents(QueryID ID) const {
  auto It = Dependents.find(ID);
  if (It != Dependents.end()) {
    return (*It).second;
  }
  return SmallVector<QueryID, 4>();
}

void DependencyGraph::setFingerprint(QueryID ID, Fingerprint FP) {
  Fingerprints[ID] = FP;
}

Fingerprint DependencyGraph::getFingerprint(QueryID ID) const {
  auto It = Fingerprints.find(ID);
  if (It != Fingerprints.end()) {
    return (*It).second;
  }
  return 0;
}

bool DependencyGraph::hasFingerprintChanged(QueryID ID, Fingerprint CurrentFP) const {
  auto It = Fingerprints.find(ID);
  if (It == Fingerprints.end()) {
    return true; // 无历史指纹，视为已变化
  }
  return (*It).second != CurrentFP;
}

SmallVector<QueryID, 16> DependencyGraph::getTransitiveDependents(QueryID ID) const {
  SmallVector<QueryID, 16> Result;
  DenseMap<QueryID, bool> Visited;

  // BFS 遍历 Dependents 图
  SmallVector<QueryID, 16> WorkList;
  auto DirectDeps = getDependents(ID);
  for (QueryID D : DirectDeps) {
    WorkList.push_back(D);
  }

  while (!WorkList.empty()) {
    QueryID Current = WorkList.back();
    WorkList.pop_back();

    if (Visited.contains(Current)) continue;
    Visited[Current] = true;
    Result.push_back(Current);

    auto NextLevel = getDependents(Current);
    for (QueryID D : NextLevel) {
      if (!Visited.contains(D)) {
        WorkList.push_back(D);
      }
    }
  }

  return Result;
}

bool DependencyGraph::hasCycle() const {
  // DFS 三色标记法：0=white, 1=gray, 2=black
  DenseMap<QueryID, int> Color;

  // 收集所有节点
  SmallVector<QueryID, 64> AllNodes;
  for (auto It = Dependencies.begin(); It != Dependencies.end(); ++It) {
    AllNodes.push_back((*It).first);
  }

  // 使用栈帧保存 (节点, 下一个要访问的依赖索引)
  struct Frame {
    QueryID Node;
    size_t DepIdx;
  };

  for (QueryID Start : AllNodes) {
    if (Color.contains(Start) && Color[Start] == 2) continue;

    SmallVector<Frame, 64> Stack;
    Color[Start] = 1; // gray
    Stack.push_back({Start, 0});

    while (!Stack.empty()) {
      Frame& Top = Stack.back();
      auto Deps = getDependencies(Top.Node);

      if (Top.DepIdx < Deps.size()) {
        QueryID Dep = Deps[Top.DepIdx];
        ++Top.DepIdx;

        if (!Color.contains(Dep) || Color[Dep] == 0) {
          // white → 进入
          Color[Dep] = 1;
          Stack.push_back({Dep, 0});
        } else if (Color[Dep] == 1) {
          // gray → 后向边，存在循环
          return true;
        }
        // black → 已完成，跳过
      } else {
        // 所有依赖已处理完毕
        Color[Top.Node] = 2; // black
        Stack.pop_back();
      }
    }
  }

  return false;
}

} // namespace ir
} // namespace blocktype
