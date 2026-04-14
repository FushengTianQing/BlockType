#include "blocktype/Config/Version.h"
#include "blocktype/AI/AIOrchestrator.h"
#include "blocktype/AI/Providers/OpenAIProvider.h"
#include "blocktype/AI/Providers/ClaudeProvider.h"
#include "blocktype/AI/Providers/LocalProvider.h"
#include "blocktype/AI/Providers/QwenProvider.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include <memory>

using namespace llvm;
using namespace blocktype;

// 命令行选项
static cl::OptionCategory BlockTypeCategory("BlockType Options");

static cl::list<std::string> InputFiles(cl::Positional, cl::desc("<input files>"), cl::cat(BlockTypeCategory));

// AI 功能选项
static cl::opt<bool> AIAssist("ai-assist", cl::desc("Enable AI-assisted compilation"), cl::cat(BlockTypeCategory));

static cl::opt<std::string> AIProviderName(
  "ai-provider",
  cl::desc("AI provider to use (openai, claude, local, qwen)"),
  cl::value_desc("provider"),
  cl::init("local"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<bool> AICache(
  "ai-cache",
  cl::desc("Enable AI response caching"),
  cl::init(true),
  cl::cat(BlockTypeCategory)
);

static cl::opt<double> AICostLimit(
  "ai-cost-limit",
  cl::desc("Maximum daily cost in USD"),
  cl::value_desc("dollars"),
  cl::init(10.0),
  cl::cat(BlockTypeCategory)
);

static cl::opt<std::string> AIModel(
  "ai-model",
  cl::desc("AI model to use"),
  cl::value_desc("model"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<std::string> OllamaEndpoint(
  "ollama-endpoint",
  cl::desc("Ollama API endpoint"),
  cl::value_desc("url"),
  cl::init("http://localhost:11434"),
  cl::cat(BlockTypeCategory)
);

// 创建 AI 编排器
std::unique_ptr<AIOrchestrator> createAIOrchestrator() {
  AIConfig Config;
  Config.EnableCache = AICache;
  Config.MaxCostPerDay = AICostLimit;
  
  auto Orchestrator = std::make_unique<AIOrchestrator>(Config);
  
  // 根据选择注册提供者
  if (AIProviderName == "openai" || AIProviderName == "all") {
    const char* Key = std::getenv("OPENAI_API_KEY");
    if (Key) {
      std::string Model = AIModel.empty() ? "gpt-4" : AIModel.getValue();
      Orchestrator->registerProvider(std::make_unique<OpenAIProvider>(Key, Model));
      outs() << "Registered OpenAI provider (model: " << Model << ")\n";
    } else {
      errs() << "Warning: OPENAI_API_KEY not set, OpenAI provider disabled\n";
    }
  }
  
  if (AIProviderName == "claude" || AIProviderName == "all") {
    const char* Key = std::getenv("ANTHROPIC_API_KEY");
    if (Key) {
      std::string Model = AIModel.empty() ? "claude-3-5-sonnet-20241022" : AIModel.getValue();
      Orchestrator->registerProvider(std::make_unique<ClaudeProvider>(Key, Model));
      outs() << "Registered Claude provider (model: " << Model << ")\n";
    } else {
      errs() << "Warning: ANTHROPIC_API_KEY not set, Claude provider disabled\n";
    }
  }
  
  if (AIProviderName == "qwen" || AIProviderName == "all") {
    const char* Key = std::getenv("QWEN_API_KEY");
    if (Key) {
      std::string Model = AIModel.empty() ? "qwen-plus" : AIModel.getValue();
      Orchestrator->registerProvider(std::make_unique<QwenProvider>(Key, Model));
      outs() << "Registered Qwen provider (model: " << Model << ")\n";
    } else {
      errs() << "Warning: QWEN_API_KEY not set, Qwen provider disabled\n";
    }
  }
  
  // 始终注册本地提供者作为回退
  {
    std::string Model = AIModel.empty() ? "codellama" : AIModel.getValue();
    Orchestrator->registerProvider(std::make_unique<LocalProvider>(OllamaEndpoint, Model));
    outs() << "Registered Local provider (endpoint: " << OllamaEndpoint << ", model: " << Model << ")\n";
  }
  
  return Orchestrator;
}

int main(int argc, char *argv[]) {
  // 解析命令行参数
  cl::HideUnrelatedOptions(BlockTypeCategory);
  cl::ParseCommandLineOptions(argc, argv, "BlockType - A C++26 compiler with bilingual support\n");
  
  // 初始化 AI 功能
  std::unique_ptr<AIOrchestrator> Orchestrator;
  if (AIAssist) {
    outs() << "AI-assisted compilation enabled\n";
    Orchestrator = createAIOrchestrator();
    
    if (Orchestrator->getProviderCount() == 0) {
      errs() << "Error: No AI providers available\n";
      return 1;
    }
    
    outs() << "AI providers registered: " << Orchestrator->getProviderCount() << "\n";
  }
  
  // 检查输入文件
  if (InputFiles.empty()) {
    outs() << "BlockType - A C++26 compiler with bilingual support\n";
    outs() << "Usage: blocktype [options] <source files>\n";
    outs() << "\nAI Options:\n";
    outs() << "  --ai-assist          Enable AI-assisted compilation\n";
    outs() << "  --ai-provider=<provider>  AI provider (openai, claude, local, qwen, all)\n";
    outs() << "  --ai-cache           Enable AI response caching (default: true)\n";
    outs() << "  --ai-cost-limit=<$>  Maximum daily cost in USD (default: 10.0)\n";
    outs() << "  --ai-model=<model>   AI model to use\n";
    outs() << "  --ollama-endpoint=<url>  Ollama API endpoint (default: http://localhost:11434)\n";
    return 0;
  }
  
  // 编译输入文件
  for (const auto& File : InputFiles) {
    outs() << "Compiling: " << File << "\n";
    
    // TODO: 实际的编译逻辑
    // 这里可以集成 AI 功能来辅助编译
    if (AIAssist && Orchestrator) {
      // 示例：使用 AI 分析代码
      AIRequest Request;
      Request.TaskType = AITaskType::SecurityCheck;
      Request.Lang = Language::Auto;
      Request.SourceFile = File;
      Request.Query = "Analyze this code for potential issues";
      
      outs() << "AI analysis enabled for " << File << "\n";
      // 实际调用会在编译器实现后添加
    }
  }
  
  return 0;
}