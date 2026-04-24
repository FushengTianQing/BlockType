#include "blocktype/Config/Version.h"
#include "blocktype/Frontend/CompilerInstance.h"
#include "blocktype/Frontend/CompilerInvocation.h"
#include "blocktype/AI/AIOrchestrator.h"
#include "blocktype/AI/Providers/OpenAIProvider.h"
#include "blocktype/AI/Providers/ClaudeProvider.h"
#include "blocktype/AI/Providers/LocalProvider.h"
#include "blocktype/AI/Providers/QwenProvider.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Support/TargetSelect.h"
#include <memory>

using namespace llvm;
using namespace blocktype;

// Command-line options (will be migrated to CompilerInvocation)
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

static cl::opt<bool> EmitLLVM(
  "emit-llvm",
  cl::desc("Emit LLVM IR"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<bool> EmitObject(
  "c",
  cl::desc("Compile and assemble, but do not link"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<bool> PreprocessOnly(
  "E",
  cl::desc("Preprocess only"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<bool> SyntaxOnly(
  "fsyntax-only",
  cl::desc("Check syntax only, do not generate code"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<unsigned> OptimizationLevel(
  "O",
  cl::desc("Optimization level (0-3)"),
  cl::value_desc("level"),
  cl::init(0),
  cl::cat(BlockTypeCategory)
);

static cl::list<std::string> IncludePaths(
  "I",
  cl::desc("Add directory to include search path"),
  cl::value_desc("directory"),
  cl::cat(BlockTypeCategory)
);

static cl::list<std::string> LibraryPaths(
  "L",
  cl::desc("Add directory to library search path"),
  cl::value_desc("directory"),
  cl::cat(BlockTypeCategory)
);

static cl::list<std::string> Libraries(
  "l",
  cl::desc("Link library"),
  cl::value_desc("library"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<bool> Verbose(
  "v",
  cl::desc("Enable verbose output"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<std::string> OutputFile(
  "o",
  cl::desc("Output file"),
  cl::value_desc("file"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<unsigned> CXXStandard(
  "std",
  cl::desc("C++ standard version (11, 14, 17, 20, 23, 26)"),
  cl::value_desc("version"),
  cl::init(26),
  cl::cat(BlockTypeCategory)
);

static cl::opt<std::string> TargetTriple(
  "target",
  cl::desc("Target triple for code generation"),
  cl::value_desc("triple"),
  cl::init(""),
  cl::cat(BlockTypeCategory)
);

// P7.3.2.3: Contract mode options
static cl::opt<std::string> ContractModeOption(
  "fcontract-mode",
  cl::desc("Contract checking mode (off|default|enforce|observe|quick_enforce)"),
  cl::value_desc("mode"),
  cl::init("enforce"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<bool> ContractsEnabled(
  "fcontracts",
  cl::desc("Enable contract checking"),
  cl::cat(BlockTypeCategory)
);

static cl::opt<bool> ContractsDisabled(
  "fno-contracts",
  cl::desc("Disable contract checking"),
  cl::cat(BlockTypeCategory)
);

// 创建 AI 编排器
std::unique_ptr<AIOrchestrator> createAIOrchestrator(const CompilerInvocation &CI) {
  AIConfig Config;
  Config.EnableCache = CI.AIOpts.EnableCache;
  Config.MaxCostPerDay = CI.AIOpts.MaxCostPerDay;
  
  auto Orchestrator = std::make_unique<AIOrchestrator>(Config);
  
  const std::string &ProviderName = CI.AIOpts.Provider;
  const std::string &Model = CI.AIOpts.Model;
  
  // 根据选择注册提供者
  if (ProviderName == "openai" || ProviderName == "all") {
    const char* Key = std::getenv("OPENAI_API_KEY");
    if (Key) {
      std::string UseModel = Model.empty() ? "gpt-4" : Model;
      Orchestrator->registerProvider(std::make_unique<OpenAIProvider>(Key, UseModel));
      outs() << "Registered OpenAI provider (model: " << UseModel << ")\n";
    } else {
      errs() << "Warning: OPENAI_API_KEY not set, OpenAI provider disabled\n";
    }
  }
  
  if (ProviderName == "claude" || ProviderName == "all") {
    const char* Key = std::getenv("ANTHROPIC_API_KEY");
    if (Key) {
      std::string UseModel = Model.empty() ? "claude-3-5-sonnet-20241022" : Model;
      Orchestrator->registerProvider(std::make_unique<ClaudeProvider>(Key, UseModel));
      outs() << "Registered Claude provider (model: " << UseModel << ")\n";
    } else {
      errs() << "Warning: ANTHROPIC_API_KEY not set, Claude provider disabled\n";
    }
  }
  
  if (ProviderName == "qwen" || ProviderName == "all") {
    const char* Key = std::getenv("QWEN_API_KEY");
    if (Key) {
      std::string UseModel = Model.empty() ? "qwen-plus" : Model;
      Orchestrator->registerProvider(std::make_unique<QwenProvider>(Key, UseModel));
      outs() << "Registered Qwen provider (model: " << UseModel << ")\n";
    } else {
      errs() << "Warning: QWEN_API_KEY not set, Qwen provider disabled\n";
    }
  }
  
  // 始终注册本地提供者作为回退
  {
    std::string UseModel = Model.empty() ? "codellama" : Model;
    Orchestrator->registerProvider(std::make_unique<LocalProvider>(CI.AIOpts.OllamaEndpoint, UseModel));
    outs() << "Registered Local provider (endpoint: " << CI.AIOpts.OllamaEndpoint << ", model: " << UseModel << ")\n";
  }
  
  return Orchestrator;
}

/// Create CompilerInvocation from command-line options.
std::shared_ptr<CompilerInvocation> createCompilerInvocation() {
  auto CI = std::make_shared<CompilerInvocation>();
  
  // Language options
  CI->LangOpts.CXXStandard = CXXStandard;
  
  // AI options
  CI->AIOpts.Enable = AIAssist;
  CI->AIOpts.Provider = AIProviderName;
  CI->AIOpts.EnableCache = AICache;
  CI->AIOpts.MaxCostPerDay = AICostLimit;
  CI->AIOpts.Model = AIModel;
  CI->AIOpts.OllamaEndpoint = OllamaEndpoint;
  
  // CodeGen options
  CI->CodeGenOpts.EmitLLVM = EmitLLVM;
  CI->CodeGenOpts.EmitObject = EmitObject;
  CI->CodeGenOpts.PreprocessOnly = PreprocessOnly;
  CI->CodeGenOpts.SyntaxOnly = SyntaxOnly;
  CI->CodeGenOpts.OptimizationLevel = OptimizationLevel;
  CI->CodeGenOpts.OutputFile = OutputFile;
  CI->CodeGenOpts.LinkExecutable = !EmitObject && !PreprocessOnly && !SyntaxOnly && !EmitLLVM;
  
  if (!TargetTriple.empty()) {
    CI->CodeGenOpts.TargetTriple = TargetTriple;
  }
  
  // Frontend options
  for (const auto &File : InputFiles) {
    CI->FrontendOpts.InputFiles.push_back(File);
  }
  CI->FrontendOpts.OutputFile = OutputFile;
  CI->FrontendOpts.DumpAST = ASTDump;
  CI->FrontendOpts.Verbose = Verbose;
  
  // Include paths
  for (const auto &Path : IncludePaths) {
    CI->FrontendOpts.IncludePaths.push_back(Path);
  }
  
  // Library paths
  for (const auto &Path : LibraryPaths) {
    CI->FrontendOpts.LibraryPaths.push_back(Path);
  }
  
  // Libraries
  for (const auto &Lib : Libraries) {
    CI->FrontendOpts.Libraries.push_back(Lib);
  }

  // P7.3.2.3: Contract mode
  if (ContractsDisabled) {
    CI->FrontendOpts.ContractsEnabled = false;
    CI->FrontendOpts.DefaultContractMode = ContractMode::Off;
  } else {
    CI->FrontendOpts.ContractsEnabled = ContractsEnabled || ContractModeOption != "off";
    // Parse contract mode string
    if (ContractModeOption == "off") {
      CI->FrontendOpts.DefaultContractMode = ContractMode::Off;
    } else if (ContractModeOption == "default") {
      CI->FrontendOpts.DefaultContractMode = ContractMode::Default;
    } else if (ContractModeOption == "enforce") {
      CI->FrontendOpts.DefaultContractMode = ContractMode::Enforce;
    } else if (ContractModeOption == "observe") {
      CI->FrontendOpts.DefaultContractMode = ContractMode::Observe;
    } else if (ContractModeOption == "quick_enforce") {
      CI->FrontendOpts.DefaultContractMode = ContractMode::Quick_Enforce;
    } else {
      errs() << "Warning: Unknown contract mode '" << ContractModeOption
             << "', using 'enforce'\n";
      CI->FrontendOpts.DefaultContractMode = ContractMode::Enforce;
    }
  }

  // Set default target triple
  CI->setDefaultTargetTriple();
  
  return CI;
}

int main(int argc, char *argv[]) {
  // Initialize LLVM Target components for supported platforms only.
  // We only link AArch64 and X86 backends, so we must not call
  // InitializeAll*() which would reference symbols from unlinked backends.
  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
  LLVMInitializeX86TargetMC();
  LLVMInitializeX86AsmPrinter();
  LLVMInitializeX86AsmParser();

  LLVMInitializeAArch64TargetInfo();
  LLVMInitializeAArch64Target();
  LLVMInitializeAArch64TargetMC();
  LLVMInitializeAArch64AsmPrinter();
  LLVMInitializeAArch64AsmParser();

  // 解析命令行参数
  cl::HideUnrelatedOptions(BlockTypeCategory);
  cl::ParseCommandLineOptions(argc, argv, "BlockType - A C++26 compiler with bilingual support\n");
  
  // 创建 CompilerInvocation
  auto CI = createCompilerInvocation();
  
  // 检查输入文件
  if (CI->FrontendOpts.InputFiles.empty()) {
    outs() << "BlockType - A C++26 compiler with bilingual support\n";
    outs() << "Usage: blocktype [options] <source files>\n";
    outs() << "\nAI Options:\n";
    outs() << "  --ai-assist          Enable AI-assisted compilation\n";
    outs() << "  --ai-provider=<provider>  AI provider (openai, claude, local, qwen, all)\n";
    outs() << "  --ai-cache           Enable AI response caching (default: true)\n";
    outs() << "  --ai-cost-limit=<$>  Maximum daily cost in USD (default: 10.0)\n";
    outs() << "  --ai-model=<model>   AI model to use\n";
    outs() << "  --ollama-endpoint=<url>  Ollama API endpoint (default: http://localhost:11434)\n";
    outs() << "\nCompilation Options:\n";
    outs() << "  --std=<version>      C++ standard version (11, 14, 17, 20, 23, 26, default: 26)\n";
    outs() << "  --target=<triple>    Target triple for code generation\n";
    outs() << "  -o <file>            Output file\n";
    outs() << "  --ast-dump           Dump AST after parsing\n";
    outs() << "  --emit-llvm          Emit LLVM IR\n";
    outs() << "  -v                   Enable verbose output\n";
    return 0;
  }
  
  // 验证选项
  if (!CI->validate()) {
    return 1;
  }
  
  // 初始化 AI 功能（可选）
  std::unique_ptr<AIOrchestrator> Orchestrator;
  if (CI->AIOpts.Enable) {
    outs() << "AI-assisted compilation enabled\n";
    Orchestrator = createAIOrchestrator(*CI);
    
    if (Orchestrator->getProviderCount() == 0) {
      errs() << "Error: No AI providers available\n";
      return 1;
    }
    
    outs() << "AI providers registered: " << Orchestrator->getProviderCount() << "\n";
  }
  
  // 创建 CompilerInstance
  CompilerInstance Instance;
  
  // 初始化
  if (!Instance.initialize(CI)) {
    errs() << "Error: Failed to initialize compiler\n";
    return 1;
  }
  
  // 编译所有输入文件
  bool Success = Instance.compileAllFiles();
  
  // AI 辅助分析（可选）
  if (CI->AIOpts.Enable && Orchestrator) {
    for (const auto &File : CI->FrontendOpts.InputFiles) {
      AIRequest Request;
      Request.TaskType = AITaskType::SecurityCheck;
      Request.Lang = AILanguage::Auto;
      Request.SourceFile = File;
      Request.Query = "Analyze this code for potential issues";
      
      if (CI->FrontendOpts.Verbose) {
        outs() << "  AI analysis requested for " << File << "\n";
      }
      
      // TODO: 实际调用 Orchestrator->sendRequest(Request)
    }
  }
  
  return Success ? 0 : 1;
}
