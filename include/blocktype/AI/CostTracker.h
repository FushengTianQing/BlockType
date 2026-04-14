#pragma once

#include "AIInterface.h"
#include "llvm/ADT/DenseMap.h"
#include <chrono>
#include <mutex>
#include <vector>

namespace blocktype {

struct CostRecord {
  AIProvider Provider;
  AITaskType TaskType;
  unsigned TokensUsed;
  double Cost;  // 美元
  std::chrono::steady_clock::time_point Timestamp;
};

class CostTracker {
  std::vector<CostRecord> Records;
  double DailyLimit;
  mutable std::mutex Mutex;
  
public:
  CostTracker(double DailyLimit = 10.0) : DailyLimit(DailyLimit) {}
  
  /// 记录使用
  void record(const CostRecord& Record);
  
  /// 检查是否超过限制
  bool isOverLimit() const;
  
  /// 获取今日成本
  double getTodayCost() const;
  
  /// 获取成本报告
  struct Report {
    double TotalCost;
    double TodayCost;
    unsigned TotalRequests;
    llvm::DenseMap<AIProvider, double> CostByProvider;
    llvm::DenseMap<AITaskType, double> CostByTask;
  };
  Report getReport() const;
  
  /// 设置每日限制
  void setDailyLimit(double Limit) {
    std::lock_guard<std::mutex> Lock(Mutex);
    DailyLimit = Limit;
  }
  
  /// 清空记录
  void clear() {
    std::lock_guard<std::mutex> Lock(Mutex);
    Records.clear();
  }
  
private:
  /// 计算成本（基于 token 数和模型定价）
  double calculateCost(AIProvider Provider, unsigned Tokens) const;
};

} // namespace blocktype
