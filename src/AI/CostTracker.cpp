#include "blocktype/AI/CostTracker.h"
#include <algorithm>

namespace blocktype {

void CostTracker::record(const CostRecord& Record) {
  std::lock_guard<std::mutex> Lock(Mutex);
  Records.push_back(Record);
}

bool CostTracker::isOverLimit() const {
  return getTodayCost() > DailyLimit;
}

double CostTracker::getTodayCost() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  
  auto Now = std::chrono::steady_clock::now();
  auto DayStart = Now - std::chrono::hours(24);
  
  double Cost = 0.0;
  for (const auto& R : Records) {
    if (R.Timestamp >= DayStart) {
      Cost += R.Cost;
    }
  }
  
  return Cost;
}

double CostTracker::calculateCost(AIProvider Provider, unsigned Tokens) const {
  // 价格（美元/1K tokens）
  double PricePer1K = 0.0;
  
  switch (Provider) {
    case AIProvider::OpenAI:
      PricePer1K = 0.03;  // GPT-4
      break;
    case AIProvider::Claude:
      PricePer1K = 0.015; // Claude 3.5 Sonnet
      break;
    case AIProvider::Qwen:
      PricePer1K = 0.004; // 通义千问
      break;
    case AIProvider::Local:
      PricePer1K = 0.0;   // 本地模型免费
      break;
  }
  
  return (Tokens / 1000.0) * PricePer1K;
}

CostTracker::Report CostTracker::getReport() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  
  Report R;
  R.TotalCost = 0.0;
  R.TodayCost = 0.0;
  R.TotalRequests = Records.size();
  
  auto Now = std::chrono::steady_clock::now();
  auto DayStart = Now - std::chrono::hours(24);
  
  for (const auto& Record : Records) {
    R.TotalCost += Record.Cost;
    
    if (Record.Timestamp >= DayStart) {
      R.TodayCost += Record.Cost;
    }
    
    R.CostByProvider[Record.Provider] += Record.Cost;
    R.CostByTask[Record.TaskType] += Record.Cost;
  }
  
  return R;
}

} // namespace blocktype
