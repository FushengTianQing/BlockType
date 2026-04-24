#ifndef BLOCKTYPE_IR_ADT_FOLDINGSET_H
#define BLOCKTYPE_IR_ADT_FOLDINGSET_H

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace blocktype {
namespace ir {

template <typename T>
class FoldingSet {
  std::vector<std::unique_ptr<T>> Entries;

  static bool equals(const T& A, const T& B) { return A.equals(&B); }

public:
  using iterator = typename std::vector<std::unique_ptr<T>>::iterator;
  using const_iterator = typename std::vector<std::unique_ptr<T>>::const_iterator;

  FoldingSet() = default;

  iterator begin() { return Entries.begin(); }
  iterator end() { return Entries.end(); }
  const_iterator begin() const { return Entries.begin(); }
  const_iterator end() const { return Entries.end(); }

  size_t size() const { return Entries.size(); }
  bool empty() const { return Entries.empty(); }

  T* findOrInsert(std::unique_ptr<T> Node) {
    for (auto& E : Entries) {
      if (equals(*E, *Node)) return E.get();
    }
    T* Ptr = Node.get();
    Entries.push_back(std::move(Node));
    return Ptr;
  }

  void clear() { Entries.clear(); }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_ADT_FOLDINGSET_H
