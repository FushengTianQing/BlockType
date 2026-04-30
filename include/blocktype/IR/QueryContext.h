#ifndef BLOCKTYPE_IR_QUERYCONTEXT_H
#define BLOCKTYPE_IR_QUERYCONTEXT_H

#include <any>
#include <cstdint>
#include <functional>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/DependencyGraph.h"

namespace blocktype {
namespace ir {

class IRTypeContext;
class IRModule;
class IRFunction;
class IRType;
class IRValue;

using QueryID = uint64_t;

/// 查询结果：包装不同类型的 IR 查询返回值。
class QueryResult {
public:
  enum Kind { Module, Function, Type, Value, Void };

private:
  Kind ResultKind;
  std::any Data;

  QueryResult(Kind K, std::any D) : ResultKind(K), Data(std::move(D)) {}

public:
  QueryResult() : ResultKind(Void), Data() {}

  static QueryResult makeModule(IRModule* M) {
    return QueryResult(Module, std::any(M));
  }
  static QueryResult makeFunction(IRFunction* F) {
    return QueryResult(Function, std::any(F));
  }
  static QueryResult makeType(IRType* T) {
    return QueryResult(Type, std::any(T));
  }
  static QueryResult makeValue(IRValue* V) {
    return QueryResult(Value, std::any(V));
  }
  static QueryResult makeVoid() {
    return QueryResult(Void, std::any());
  }

  IRModule* getAsModule() const {
    if (ResultKind == Module && Data.has_value()) {
      return std::any_cast<IRModule*>(Data);
    }
    return nullptr;
  }
  IRFunction* getAsFunction() const {
    if (ResultKind == Function && Data.has_value()) {
      return std::any_cast<IRFunction*>(Data);
    }
    return nullptr;
  }
  IRType* getAsType() const {
    if (ResultKind == Type && Data.has_value()) {
      return std::any_cast<IRType*>(Data);
    }
    return nullptr;
  }
  IRValue* getAsValue() const {
    if (ResultKind == Value && Data.has_value()) {
      return std::any_cast<IRValue*>(Data);
    }
    return nullptr;
  }

  bool isValid() const { return Data.has_value(); }
  Kind getKind() const { return ResultKind; }
};

/// 查询上下文：提供缓存和依赖追踪的查询框架。
class QueryContext {
  IRTypeContext& TypeCtx;
  DenseMap<QueryID, QueryResult> Cache;
  DependencyGraph DepGraph;
  IRModule* TargetModule = nullptr;
  uint64_t NextQueryID = 1;

public:
  explicit QueryContext(IRTypeContext& Ctx);

  /// 执行查询：先查缓存，命中返回；未命中则计算并缓存。
  QueryResult query(QueryID ID, std::function<QueryResult()> Compute);

  /// 失效一个查询及其所有传递依赖者。
  void invalidate(QueryID ID);

  /// 失效所有缓存。
  void invalidateAll();

  DependencyGraph& getDependencyGraph() { return DepGraph; }
  void setTargetModule(IRModule* M) { TargetModule = M; }
  IRModule* getTargetModule() const { return TargetModule; }
  size_t getCacheSize() const { return Cache.size(); }
  void clearCache() { Cache.clear(); }

  /// 分配下一个唯一 QueryID。
  QueryID allocateQueryID() { return NextQueryID++; }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_QUERYCONTEXT_H
