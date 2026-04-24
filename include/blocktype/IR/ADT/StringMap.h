#ifndef BLOCKTYPE_IR_ADT_STRINGMAP_H
#define BLOCKTYPE_IR_ADT_STRINGMAP_H

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace blocktype {
namespace ir {

template <typename ValueT>
class StringMap {
  struct Entry {
    std::string Key;
    ValueT Value;
    Entry(std::string K, ValueT V) : Key(std::move(K)), Value(std::move(V)) {}
  };

  std::vector<std::unique_ptr<Entry>> Entries;

public:
  class iterator {
    typename std::vector<std::unique_ptr<Entry>>::iterator It;

  public:
    iterator(typename std::vector<std::unique_ptr<Entry>>::iterator it) : It(it) {}
    Entry& operator*() { return **It; }
    Entry* operator->() { return It->get(); }
    iterator& operator++() { ++It; return *this; }
    iterator operator++(int) { auto T = *this; ++It; return T; }
    bool operator==(const iterator& O) const { return It == O.It; }
    bool operator!=(const iterator& O) const { return It != O.It; }
  };

  class const_iterator {
    typename std::vector<std::unique_ptr<Entry>>::const_iterator It;

  public:
    const_iterator(typename std::vector<std::unique_ptr<Entry>>::const_iterator it) : It(it) {}
    const Entry& operator*() const { return **It; }
    const Entry* operator->() const { return It->get(); }
    const_iterator& operator++() { ++It; return *this; }
    const_iterator operator++(int) { auto T = *this; ++It; return T; }
    bool operator==(const const_iterator& O) const { return It == O.It; }
    bool operator!=(const const_iterator& O) const { return It != O.It; }
  };

  StringMap() = default;

  iterator begin() { return iterator(Entries.begin()); }
  iterator end() { return iterator(Entries.end()); }
  const_iterator begin() const { return const_iterator(Entries.begin()); }
  const_iterator end() const { return const_iterator(Entries.end()); }

  size_t size() const { return Entries.size(); }
  bool empty() const { return Entries.empty(); }

  iterator find(const std::string& K) {
    for (auto It = Entries.begin(); It != Entries.end(); ++It)
      if ((*It)->Key == K) return iterator(It);
    return end();
  }

  iterator find(std::string_view K) {
    for (auto It = Entries.begin(); It != Entries.end(); ++It)
      if ((*It)->Key == K) return iterator(It);
    return end();
  }

  const_iterator find(const std::string& K) const {
    for (auto It = Entries.begin(); It != Entries.end(); ++It)
      if ((*It)->Key == K) return const_iterator(It);
    return end();
  }

  const_iterator find(std::string_view K) const {
    for (auto It = Entries.begin(); It != Entries.end(); ++It)
      if ((*It)->Key == K) return const_iterator(It);
    return end();
  }

  bool contains(const std::string& K) const { return find(K) != end(); }
  bool contains(std::string_view K) const { return find(K) != end(); }

  ValueT& operator[](const std::string& K) {
    auto It = find(K);
    if (It != end()) return It->Value;
    Entries.push_back(std::make_unique<Entry>(K, ValueT()));
    return Entries.back()->Value;
  }

  std::pair<iterator, bool> insert(std::pair<std::string, ValueT> KV) {
    auto It = find(KV.first);
    if (It != end()) return {It, false};
    Entries.push_back(std::make_unique<Entry>(std::move(KV.first), std::move(KV.second)));
    return {iterator(Entries.end() - 1), true};
  }

  size_t erase(const std::string& K) {
    auto It = find(K);
    if (It == end()) return 0;
    Entries.erase(It.It);
    return 1;
  }

  void clear() { Entries.clear(); }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_ADT_STRINGMAP_H
