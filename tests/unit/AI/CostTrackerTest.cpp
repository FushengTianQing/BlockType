#include "blocktype/AI/CostTracker.h"
#include <gtest/gtest.h>
#include <chrono>

using namespace blocktype;

class CostTrackerTest : public ::testing::Test {
protected:
  CostTracker Tracker{10.0};  // $10 每日限制
  
  CostRecord createRecord(AIProvider Provider, AITaskType Task, 
                          unsigned Tokens, double Cost) {
    CostRecord Record;
    Record.Provider = Provider;
    Record.TaskType = Task;
    Record.TokensUsed = Tokens;
    Record.Cost = Cost;
    Record.Timestamp = std::chrono::steady_clock::now();
    return Record;
  }
};

TEST_F(CostTrackerTest, BasicCostTracking) {
  auto Record = createRecord(AIProvider::OpenAI, AITaskType::ErrorFix, 1000, 0.03);
  
  Tracker.record(Record);
  
  auto TodayCost = Tracker.getTodayCost();
  EXPECT_NEAR(TodayCost, 0.03, 0.001);
}

TEST_F(CostTrackerTest, MultipleRecords) {
  Tracker.record(createRecord(AIProvider::OpenAI, AITaskType::ErrorFix, 1000, 0.03));
  Tracker.record(createRecord(AIProvider::Claude, AITaskType::CodeCompletion, 2000, 0.06));
  Tracker.record(createRecord(AIProvider::Qwen, AITaskType::Translation, 500, 0.01));
  
  auto TodayCost = Tracker.getTodayCost();
  EXPECT_NEAR(TodayCost, 0.10, 0.001);
}

TEST_F(CostTrackerTest, DailyLimitCheck) {
  // 添加接近限制的记录
  Tracker.record(createRecord(AIProvider::OpenAI, AITaskType::ErrorFix, 100000, 9.99));
  
  EXPECT_FALSE(Tracker.isOverLimit());
  
  // 超过限制
  Tracker.record(createRecord(AIProvider::OpenAI, AITaskType::ErrorFix, 1000, 0.02));
  
  EXPECT_TRUE(Tracker.isOverLimit());
}

TEST_F(CostTrackerTest, CostReport) {
  Tracker.record(createRecord(AIProvider::OpenAI, AITaskType::ErrorFix, 1000, 0.03));
  Tracker.record(createRecord(AIProvider::OpenAI, AITaskType::CodeCompletion, 2000, 0.06));
  Tracker.record(createRecord(AIProvider::Claude, AITaskType::CodeCompletion, 3000, 0.09));
  
  auto Report = Tracker.getReport();
  
  EXPECT_NEAR(Report.TotalCost, 0.18, 0.001);
  EXPECT_EQ(Report.TotalRequests, 3);
  
  // 检查按提供者统计
  EXPECT_NEAR(Report.CostByProvider[AIProvider::OpenAI], 0.09, 0.001);
  EXPECT_NEAR(Report.CostByProvider[AIProvider::Claude], 0.09, 0.001);
  
  // 检查按任务类型统计
  EXPECT_NEAR(Report.CostByTask[AITaskType::ErrorFix], 0.03, 0.001);
  EXPECT_NEAR(Report.CostByTask[AITaskType::CodeCompletion], 0.15, 0.001);
}

TEST_F(CostTrackerTest, SetDailyLimit) {
  Tracker.setDailyLimit(5.0);
  
  Tracker.record(createRecord(AIProvider::OpenAI, AITaskType::ErrorFix, 1000, 4.99));
  EXPECT_FALSE(Tracker.isOverLimit());
  
  Tracker.record(createRecord(AIProvider::OpenAI, AITaskType::ErrorFix, 1000, 0.02));
  EXPECT_TRUE(Tracker.isOverLimit());
}

TEST_F(CostTrackerTest, ClearRecords) {
  Tracker.record(createRecord(AIProvider::OpenAI, AITaskType::ErrorFix, 1000, 0.03));
  Tracker.record(createRecord(AIProvider::Claude, AITaskType::CodeCompletion, 2000, 0.06));
  
  EXPECT_NEAR(Tracker.getTodayCost(), 0.09, 0.001);
  
  Tracker.clear();
  
  EXPECT_NEAR(Tracker.getTodayCost(), 0.0, 0.001);
}
