#ifndef BLOCKTYPE_IR_ADT_DENSEMAP_H
#define BLOCKTYPE_IR_ADT_DENSEMAP_H

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace blocktype {
namespace ir {

template <typename KeyT, typename ValueT>
class DenseMap {
  struct Bucket {
    KeyT Key;
    ValueT Value;
    enum State : uint8_t { Empty, Occupied, Tombstone } State = Empty;
  };

  Bucket* Buckets = nullptr;
  size_t NumBuckets = 0;
  size_t NumEntries = 0;
  size_t NumTombstones = 0;

  static size_t getHash(const KeyT& K) { return std::hash<KeyT>()(K); }

  static bool isEqual(const KeyT& A, const KeyT& B) { return A == B; }

  size_t lookupBucketFor(const KeyT& K) const {
    if (NumBuckets == 0) return 0;
    size_t H = getHash(K) % NumBuckets;
    size_t FirstTomb = NumBuckets;
    for (size_t Probe = 0; Probe < NumBuckets; ++Probe) {
      size_t Idx = (H + Probe) % NumBuckets;
      auto& B = Buckets[Idx];
      if (B.State == Bucket::Empty) {
        if (FirstTomb != NumBuckets) return FirstTomb;
        return Idx;
      }
      if (B.State == Bucket::Tombstone) {
        if (FirstTomb == NumBuckets) FirstTomb = Idx;
        continue;
      }
      if (isEqual(B.Key, K)) return Idx;
    }
    return FirstTomb;
  }

  void grow() {
    size_t NewSize = NumBuckets == 0 ? 16 : NumBuckets * 2;
    Bucket* OldBuckets = Buckets;
    size_t OldNum = NumBuckets;

    Buckets = new Bucket[NewSize]();
    NumBuckets = NewSize;
    NumEntries = 0;
    NumTombstones = 0;

    for (size_t i = 0; i < OldNum; ++i) {
      if (OldBuckets[i].State == Bucket::Occupied) {
        insertInto(Buckets, NewSize, std::move(OldBuckets[i].Key),
                   std::move(OldBuckets[i].Value));
        OldBuckets[i].Key.~KeyT();
        OldBuckets[i].Value.~ValueT();
      }
    }

    delete[] OldBuckets;
  }

  void insertInto(Bucket* Buf, size_t Cap, KeyT K, ValueT V) {
    size_t H = getHash(K) % Cap;
    for (size_t Probe = 0; Probe < Cap; ++Probe) {
      size_t Idx = (H + Probe) % Cap;
      auto& B = Buf[Idx];
      if (B.State != Bucket::Occupied) {
        new (&B.Key) KeyT(std::move(K));
        new (&B.Value) ValueT(std::move(V));
        B.State = Bucket::Occupied;
        ++NumEntries;
        return;
      }
    }
    assert(false && "DenseMap insertInto: should not reach here");
  }

public:
  class iterator {
    Bucket* B;
    Bucket* End;

    void advance() {
      while (B != End && B->State != Bucket::Occupied) ++B;
    }

  public:
    iterator(Bucket* b, Bucket* e) : B(b), End(e) { advance(); }
    std::pair<KeyT&, ValueT&> operator*() { return {B->Key, B->Value}; }
    std::pair<const KeyT&, const ValueT&> operator*() const { return {B->Key, B->Value}; }
    iterator& operator++() { ++B; advance(); return *this; }
    iterator operator++(int) { auto T = *this; ++B; advance(); return T; }
    bool operator==(const iterator& O) const { return B == O.B; }
    bool operator!=(const iterator& O) const { return B != O.B; }
    Bucket* getBucket() const { return B; }
  };

  class const_iterator {
    const Bucket* B;
    const Bucket* End;

    void advance() {
      while (B != End && B->State != Bucket::Occupied) ++B;
    }

  public:
    const_iterator(const Bucket* b, const Bucket* e) : B(b), End(e) { advance(); }
    std::pair<const KeyT&, const ValueT&> operator*() const { return {B->Key, B->Value}; }
    const_iterator& operator++() { ++B; advance(); return *this; }
    const_iterator operator++(int) { auto T = *this; ++B; advance(); return T; }
    bool operator==(const const_iterator& O) const { return B == O.B; }
    bool operator!=(const const_iterator& O) const { return B != O.B; }
  };

  DenseMap() = default;

  DenseMap(const DenseMap&) = delete;
  DenseMap& operator=(const DenseMap&) = delete;

  DenseMap(DenseMap&& Other)
      : Buckets(Other.Buckets), NumBuckets(Other.NumBuckets),
        NumEntries(Other.NumEntries), NumTombstones(Other.NumTombstones) {
    Other.Buckets = nullptr;
    Other.NumBuckets = 0;
    Other.NumEntries = 0;
    Other.NumTombstones = 0;
  }

  DenseMap& operator=(DenseMap&& Other) {
    if (this != &Other) {
      delete[] Buckets;
      Buckets = Other.Buckets;
      NumBuckets = Other.NumBuckets;
      NumEntries = Other.NumEntries;
      NumTombstones = Other.NumTombstones;
      Other.Buckets = nullptr;
      Other.NumBuckets = 0;
      Other.NumEntries = 0;
      Other.NumTombstones = 0;
    }
    return *this;
  }

  ~DenseMap() { delete[] Buckets; }

  iterator begin() { return iterator(Buckets, Buckets + NumBuckets); }
  iterator end() { return iterator(Buckets + NumBuckets, Buckets + NumBuckets); }
  const_iterator begin() const { return const_iterator(Buckets, Buckets + NumBuckets); }
  const_iterator end() const { return const_iterator(Buckets + NumBuckets, Buckets + NumBuckets); }

  size_t size() const { return NumEntries; }
  bool empty() const { return NumEntries == 0; }

  iterator find(const KeyT& K) {
    if (NumBuckets == 0) return end();
    size_t Idx = lookupBucketFor(K);
    if (Buckets[Idx].State == Bucket::Occupied && isEqual(Buckets[Idx].Key, K))
      return iterator(Buckets + Idx, Buckets + NumBuckets);
    return end();
  }

  const_iterator find(const KeyT& K) const {
    if (NumBuckets == 0) return end();
    size_t Idx = lookupBucketFor(K);
    if (Buckets[Idx].State == Bucket::Occupied && isEqual(Buckets[Idx].Key, K))
      return const_iterator(Buckets + Idx, Buckets + NumBuckets);
    return end();
  }

  bool contains(const KeyT& K) const { return find(K) != end(); }

  ValueT& operator[](const KeyT& K) {
    auto It = find(K);
    if (It != end()) return It.getBucket()->Value;

    if ((NumEntries + NumTombstones) * 4 >= NumBuckets * 3) grow();

    size_t Idx = lookupBucketFor(K);
    auto& B = Buckets[Idx];
    new (&B.Key) KeyT(K);
    new (&B.Value) ValueT();
    B.State = Bucket::Occupied;
    ++NumEntries;
    return B.Value;
  }

  std::pair<iterator, bool> insert(std::pair<KeyT, ValueT> KV) {
    auto It = find(KV.first);
    if (It != end()) return {It, false};

    if ((NumEntries + NumTombstones) * 4 >= NumBuckets * 3) grow();

    size_t Idx = lookupBucketFor(KV.first);
    auto& B = Buckets[Idx];
    new (&B.Key) KeyT(std::move(KV.first));
    new (&B.Value) ValueT(std::move(KV.second));
    B.State = Bucket::Occupied;
    ++NumEntries;
    return {iterator(Buckets + Idx, Buckets + NumBuckets), true};
  }

  size_t erase(const KeyT& K) {
    auto It = find(K);
    if (It == end()) return 0;
    auto* B = It.getBucket();
    B->Key.~KeyT();
    B->Value.~ValueT();
    B->State = Bucket::Tombstone;
    --NumEntries;
    ++NumTombstones;
    return 1;
  }

  void clear() {
    for (size_t i = 0; i < NumBuckets; ++i) {
      if (Buckets[i].State == Bucket::Occupied) {
        Buckets[i].Key.~KeyT();
        Buckets[i].Value.~ValueT();
      }
      Buckets[i].State = Bucket::Empty;
    }
    NumEntries = 0;
    NumTombstones = 0;
  }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_ADT_DENSEMAP_H
