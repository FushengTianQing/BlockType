#include "blocktype/IR/DiagnosticCrossRef.h"

#include <sstream>
#include <unordered_set>

namespace blocktype {
namespace diag {

void DiagnosticCrossRef::registerDiag(const StructuredDiagnostic& D) {
  // Check if already registered
  for (auto* Reg : RegisteredDiags) {
    if (Reg == &D)
      return;
  }
  RegisteredDiags.push_back(&D);
}

void DiagnosticCrossRef::addLink(const StructuredDiagnostic& Source,
                                  const StructuredDiagnostic& Target,
                                  ir::StringRef Relation) {
  // Ensure both diagnostics are registered
  registerDiag(Source);
  registerDiag(Target);

  DiagLink Link;
  Link.Source = &Source;
  Link.Target = &Target;
  Link.Relation = Relation.str();
  Links.push_back(Link);
}

ir::SmallVector<DiagnosticCrossRef::DiagLink, 8>
DiagnosticCrossRef::getChain(const StructuredDiagnostic& Root) const {
  ir::SmallVector<DiagLink, 8> Chain;

  // BFS traversal with depth limit
  struct BFSNode {
    const StructuredDiagnostic* Diag;
    unsigned Depth;
  };

  ir::SmallVector<BFSNode, 32> WorkQueue;
  std::unordered_set<const StructuredDiagnostic*> Visited;

  WorkQueue.push_back({&Root, 0});
  Visited.insert(&Root);

  while (!WorkQueue.empty()) {
    BFSNode Current = WorkQueue.front();
    WorkQueue.erase(WorkQueue.begin());

    if (Current.Depth >= MaxChainDepth)
      continue;

    // Find all links from Current.Diag as source
    for (const auto& Link : Links) {
      if (Link.Source == Current.Diag) {
        Chain.push_back(Link);

        if (Visited.find(Link.Target) == Visited.end()) {
          Visited.insert(Link.Target);
          WorkQueue.push_back({Link.Target, Current.Depth + 1});
        }
      }
    }
  }

  return Chain;
}

bool DiagnosticCrossRef::hasCycle() const {
  // Build adjacency: pointer → set of pointers it links to
  std::unordered_map<const StructuredDiagnostic*,
                     std::vector<const StructuredDiagnostic*>>
      Adj;

  for (const auto& Link : Links) {
    Adj[Link.Source].push_back(Link.Target);
  }

  // DFS three-color cycle detection
  enum Color { White, Gray, Black };
  std::unordered_map<const StructuredDiagnostic*, Color> Colors;

  // Initialize all known nodes to White
  for (auto* D : RegisteredDiags) {
    Colors[D] = White;
  }
  for (const auto& Link : Links) {
    Colors[Link.Source] = Colors.count(Link.Source) ? Colors[Link.Source] : White;
    Colors[Link.Target] = Colors.count(Link.Target) ? Colors[Link.Target] : White;
  }

  // Iterative DFS
  for (auto& KV : Colors) {
    if (KV.second != White)
      continue;

    // Stack-based DFS
    struct Frame {
      const StructuredDiagnostic* Node;
      size_t NextIdx;
    };

    ir::SmallVector<Frame, 32> Stack;
    Colors[KV.first] = Gray;
    Stack.push_back({KV.first, 0});

    while (!Stack.empty()) {
      Frame& Top = Stack.back();
      auto& Neighbors = Adj[Top.Node];

      if (Top.NextIdx < Neighbors.size()) {
        const StructuredDiagnostic* Next = Neighbors[Top.NextIdx];
        ++Top.NextIdx;

        auto It = Colors.find(Next);
        if (It == Colors.end() || It->second == White) {
          Colors[Next] = Gray;
          Stack.push_back({Next, 0});
        } else if (It->second == Gray) {
          return true; // Cycle detected
        }
        // Black → already processed, skip
      } else {
        Colors[Top.Node] = Black;
        Stack.pop_back();
      }
    }
  }

  return false;
}

std::string DiagnosticCrossRef::toJSON() const {
  std::ostringstream OS;
  OS << "{\n  \"chain\": [";

  for (size_t i = 0; i < Links.size(); ++i) {
    const auto& Link = Links[i];
    if (i > 0)
      OS << ",";
    OS << "\n    {";
    OS << "\n      \"source\": \"";
    if (Link.Source)
      OS << Link.Source->getMessage();
    OS << "\",";
    OS << "\n      \"target\": \"";
    if (Link.Target)
      OS << Link.Target->getMessage();
    OS << "\",";
    OS << "\n      \"relation\": \"" << Link.Relation << "\"";
    OS << "\n    }";
  }

  if (!Links.empty())
    OS << "\n  ";
  OS << "]\n}";
  return OS.str();
}

} // namespace diag
} // namespace blocktype
