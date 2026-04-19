#include "blocktype/Config/Version.h"
#include "blocktype/AI/AIOrchestrator.h"
#include "blocktype/AI/Providers/OpenAIProvider.h"
#include "blocktype/AI/Providers/ClaudeProvider.h"
#include "blocktype/AI/Providers/LocalProvider.h"
#include "blocktype/AI/Providers/QwenProvider.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Parse/Parser.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/ASTDumper.h"
#include "blocktype/AST/Decl.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/FileSystem.h"
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

// 编译选项
static cl::opt<bool> ASTDump(
  "ast-dump",
  cl::desc("Dump AST after parsing"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<bool> Verbose(
  "v",
  cl::desc("Enable verbose output"),
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
    if (Verbose) {
      outs() << "Compiling: " << File << "\n";
    }
    
    // 1. 读取源文件
    auto BufferOrErr = llvm::MemoryBuffer::getFile(File);
    if (!BufferOrErr) {
      errs() << "Error: Cannot read file '" << File << "'\n";
      continue;
    }
    
    std::unique_ptr<llvm::MemoryBuffer> Buffer = std::move(*BufferOrErr);
    StringRef SourceCode = Buffer->getBuffer();
    
    if (Verbose) {
      outs() << "  Source size: " << SourceCode.size() << " bytes\n";
    }
    
    // 2. 创建编译基础设施
    SourceManager SM;
    DiagnosticsEngine Diags;
    ASTContext Context;
    
    // 3. 创建文件 ID
    SM.createMainFileID(File, SourceCode);
    
    // 4. 创建预处理器并进入源文件
    Preprocessor PP(SM, Diags);
    PP.enterSourceFile(File, SourceCode);
    
    // 5. 创建 Sema 实例（在 Parser 之前，以便 Parser 可以委托 Sema 创建节点）
    Sema S(Context, Diags);

    // 6. 创建解析器并解析翻译单元
    Parser P(PP, Context, S);

    if (Verbose) {
      outs() << "  Parsing...\n";
    }

    TranslationUnitDecl *TU = P.parseTranslationUnit();

    // 7. Post-parse diagnostics: unused declarations, unreachable code
    S.DiagnoseUnusedDecls(TU);

    // 8. 报告错误
    if (P.hasErrors()) {
      errs() << "Error: Parsing failed for '" << File << "'\n";
      continue;
    }

    // 8. 可选：输出 AST
    if (ASTDump && TU) {
      outs() << "\n=== AST Dump for " << File << " ===\n";
      TU->dump(outs());
      outs() << "=== End AST Dump ===\n\n";
    }
    
    // 10. AI 辅助分析（可选）
    if (AIAssist && Orchestrator) {
      AIRequest Request;
      Request.TaskType = AITaskType::SecurityCheck;
      Request.Lang = AILanguage::Auto;
      Request.SourceFile = File;
      Request.Query = "Analyze this code for potential issues";
      
      if (Verbose) {
        outs() << "  AI analysis requested for " << File << "\n";
      }
      // 实际调用会在语义分析完成后添加
    }
    
    if (Verbose) {
      outs() << "  Compilation successful\n";
    }
  }
  
  return 0;
}