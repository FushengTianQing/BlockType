#include "blocktype/IR/IRFFI.h"

#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRTypeContext.h"

namespace blocktype {
namespace ir {
namespace ffi {

// ============================================================================
// FFITypeMapper — 实现
// ============================================================================

static IRType* mapCType(StringRef TypeName, IRTypeContext& Ctx) {
  // 基本整数类型
  if (TypeName == "void")
    return Ctx.getVoidType();
  if (TypeName == "_Bool" || TypeName == "bool")
    return Ctx.getInt8Ty();
  if (TypeName == "char")
    return Ctx.getInt8Ty();
  if (TypeName == "signed char")
    return Ctx.getInt8Ty();
  if (TypeName == "unsigned char")
    return Ctx.getInt8Ty();
  if (TypeName == "short" || TypeName == "signed short")
    return Ctx.getInt16Ty();
  if (TypeName == "unsigned short")
    return Ctx.getInt16Ty();
  if (TypeName == "int" || TypeName == "signed int")
    return Ctx.getInt32Ty();
  if (TypeName == "unsigned int")
    return Ctx.getInt32Ty();
  if (TypeName == "long" || TypeName == "signed long")
    return Ctx.getInt64Ty(); // LP64
  if (TypeName == "unsigned long")
    return Ctx.getInt64Ty();
  if (TypeName == "long long" || TypeName == "signed long long")
    return Ctx.getInt64Ty();
  if (TypeName == "unsigned long long")
    return Ctx.getInt64Ty();

  // 浮点类型
  if (TypeName == "float")
    return Ctx.getFloatTy();
  if (TypeName == "double")
    return Ctx.getDoubleTy();
  if (TypeName == "long double")
    return Ctx.getFloat80Ty(); // x86

  // 常见指针类型
  if (TypeName == "void*" || TypeName == "char*" ||
      TypeName == "const void*" || TypeName == "const char*")
    return Ctx.getPointerType(Ctx.getInt8Ty());

  // va_list
  if (TypeName == "va_list")
    return Ctx.getPointerType(Ctx.getInt8Ty());

  return nullptr;
}

static std::string mapCTypeFromIR(const IRType* T) {
  if (!T) return "void";

  switch (T->getKind()) {
  case IRType::Void:
    return "void";
  case IRType::Bool:
    return "_Bool";
  case IRType::Integer: {
    auto* IT = static_cast<const IRIntegerType*>(T);
    switch (IT->getBitWidth()) {
    case 8:  return "char";
    case 16: return "short";
    case 32: return "int";
    case 64: return "long long";
    default: return "int" + std::to_string(IT->getBitWidth()) + "_t";
    }
  }
  case IRType::Float: {
    auto* FT = static_cast<const IRFloatType*>(T);
    switch (FT->getBitWidth()) {
    case 32: return "float";
    case 64: return "double";
    case 80: return "long double";
    case 128: return "long double";
    default: return "float" + std::to_string(FT->getBitWidth()) + "_t";
    }
  }
  case IRType::Pointer:
    return "void*";
  case IRType::Array:
    return "void*"; // C 数组退化为指针
  case IRType::Struct:
    return "struct " + static_cast<const IRStructType*>(T)->getName().str();
  case IRType::Function:
    return "void*"; // 函数指针
  default:
    return "void*"; // 未知类型映射为 void*
  }
}

IRType* FFITypeMapper::mapExternalType(StringRef Language, StringRef TypeName,
                                        IRTypeContext& Ctx) {
  if (Language == "C") {
    return mapCType(TypeName, Ctx);
  }

  // Rust/Python/WASM/Swift/Zig 通过 C ABI 映射
  if (Language == "Rust" || Language == "Python" ||
      Language == "WASM" || Language == "Swift" || Language == "Zig") {
    return mapCType(TypeName, Ctx);
  }

  return nullptr;
}

std::string FFITypeMapper::mapToExternalType(const IRType* T,
                                              StringRef TargetLanguage) {
  if (TargetLanguage == "C") {
    return mapCTypeFromIR(T);
  }

  // 其他语言通过 C ABI 间接映射
  if (TargetLanguage == "Rust" || TargetLanguage == "Python" ||
      TargetLanguage == "WASM" || TargetLanguage == "Swift" ||
      TargetLanguage == "Zig") {
    return mapCTypeFromIR(T);
  }

  return mapCTypeFromIR(T);
}

SmallVector<std::string, 8> FFITypeMapper::getSupportedLanguages() {
  return {"C", "Rust", "Python", "WASM", "Swift", "Zig"};
}

// ============================================================================
// FFIFunctionDecl — 实现
// ============================================================================

FFIFunctionDecl::FFIFunctionDecl(StringRef N, StringRef MN, CallingConvention C,
                                 IRFunctionType* S, StringRef Lang,
                                 StringRef HF, bool VA)
  : Name_(N.str()), MangledName_(MN.str()), Conv_(C), Signature_(S),
    SourceLanguage_(Lang.str()), HeaderFile_(HF.str()), IsVariadic_(VA) {}

IRFunction* FFIFunctionDecl::createIRDeclaration(IRModule& M) {
  if (!Signature_)
    return nullptr;

  // 使用修饰名创建声明
  return M.getOrInsertFunction(MangledName_, Signature_);
}

IRFunction* FFIFunctionDecl::createCallWrapper(IRModule& M,
                                                const IRFunction& Caller) {
  if (!Signature_)
    return nullptr;

  // 创建包装函数：名字为 "_ffi_wrap_" + Name_
  std::string WrapperName = "_ffi_wrap_" + Name_;
  auto* Wrapper = M.getOrInsertFunction(WrapperName, Signature_);
  auto* Entry = Wrapper->addBasicBlock("entry");

  // 查找目标 FFI 函数声明
  auto* TargetF = M.getFunction(MangledName_);
  if (!TargetF) {
    TargetF = createIRDeclaration(M);
  }

  // 基础版：创建对目标函数的调用（不带参数转发，
  // 因为 IRArgument 不继承 IRValue，参数转发需后续阶段完善）
  IRContext Ctx;
  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);

  SmallVector<IRValue*, 8> Args;
  auto* Call = Builder.createCall(TargetF, Args, "ffi_result");

  // 创建返回
  if (Signature_->getReturnType()->isVoid()) {
    Builder.createRetVoid();
  } else {
    Builder.createRet(Call);
  }

  return Wrapper;
}

// ============================================================================
// FFIModule — 实现
// ============================================================================

void FFIModule::addDeclaration(std::unique_ptr<FFIFunctionDecl> Decl) {
  std::string Name = Decl->getName().str();
  Declarations_[Name] = std::move(Decl);
}

bool FFIModule::importFromCHeader(StringRef HeaderPath) {
  // 占位实现 — 后续阶段通过 libclang 解析头文件
  (void)HeaderPath;
  return false;
}

bool FFIModule::importFromRustCrate(StringRef CratePath) {
  // 占位实现 — 后续阶段实现
  (void)CratePath;
  return false;
}

bool FFIModule::exportToCAPI(const IRFunction& F, StringRef ExportName) {
  // 占位实现 — 后续阶段实现
  (void)F;
  (void)ExportName;
  return false;
}

bool FFIModule::generateBindings(StringRef TargetLanguage, raw_ostream& OS) {
  // 占位实现 — 后续阶段实现
  (void)TargetLanguage;
  (void)OS;
  return false;
}

const FFIFunctionDecl* FFIModule::lookup(StringRef Name) const {
  auto It = Declarations_.find(Name);
  if (It == Declarations_.end())
    return nullptr;
  return It->Value.get();
}

unsigned FFIModule::getNumDeclarations() const {
  return static_cast<unsigned>(Declarations_.size());
}

} // namespace ffi
} // namespace ir
} // namespace blocktype
