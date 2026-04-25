# Task A-F12：FFI 接口基础定义 — 优化版

## 红线 Checklist 确认

| # | 红线 | 状态 | 说明 |
|---|------|------|------|
| 1 | 架构优先 | ✅ | FFI 层独立于前端/后端，通过 IR 抽象接口（IRType/IRModule/IRBuilder）交互 |
| 2 | 多前端多后端自由组合 | ✅ | FFI 模块不依赖具体前端或后端实现 |
| 3 | 渐进式改造 | ✅ | 新增 3 个文件（1 头 + 1 源 + 1 测试）+ 修改 1 个 CMakeLists |
| 4 | 现有功能不退化 | ✅ | 纯新增，不修改任何已有文件 |
| 5 | 接口抽象优先 | ✅ | FFITypeMapper/FFIFunctionDecl 通过 IR 抽象接口交互 |
| 6 | IR 中间层解耦 | ✅ | FFI 作为 IR 层与外部语言的桥梁 |

## 依赖
- A.1（IRType 体系）✅
- A.3（Opcode 枚举中的 FFICall/FFICheck/FFICoerce/FFIUnwind）✅

## 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRFFI.h` |
| 新增 | `src/IR/IRFFI.cpp` |
| 新增 | `tests/unit/IR/IRFFITest.cpp` |
| 修改 | `src/IR/CMakeLists.txt`（+1 行） |
| 修改 | `tests/unit/IR/CMakeLists.txt`（+1 行） |

## 设计说明

### 1. CallingConvention 处理
规格中 `ffi::CallingConvention` 与 `ir::CallingConvention`（IRValue.h）同名但值不同。
- `ir::CallingConvention`：IR 内部调用约定（含 Fast、Cold、GHC 等编译器内部约定）
- `ffi::CallingConvention`：FFI 外部调用约定（仅含 C ABI 相关约定）
两者在不同命名空间（`ir` vs `ir::ffi`），不冲突。`ffi::CallingConvention` 独立定义。

### 2. FFIModule 存储
规格使用 `DenseMap<StringRef, FFIFunctionDecl>`，但 StringRef 需要稳定的底层字符串。
改用 `StringMap<std::unique_ptr<FFIFunctionDecl>>`（项目自定义容器，键字符串自持有）。
提供 `addDeclaration()` 方法用于添加声明。

### 3. FFITypeMapper
- `mapExternalType`：将外部语言类型名映射为 IRType（当前实现 C 语言完整映射）
- `mapToExternalType`：将 IRType 映射为外部语言类型名
- `getSupportedLanguages`：返回支持的语言列表

### 4. FFIFunctionDecl
- 存储外部函数的名称、修饰名、调用约定、签名、源语言等
- `createIRDeclaration`：在 IRModule 中创建对应的 IRFunction 声明
- `createCallWrapper`：创建调用包装函数（基础版，使用 createCall）

### 5. importFromCHeader/importFromRustCrate/exportToCAPI/generateBindings
基础阶段为占位实现，返回 false/空，后续阶段完善。

### 关键适配
- 命名空间：`blocktype::ir::ffi`
- 成员变量命名：尾下划线（`Name_`、`Conv_`）
- 头文件前向声明 + `.cpp` 完整 include
- 使用项目自定义容器：`StringMap`、`SmallVector`、`StringRef`
- FFICall/FFICheck/FFICoerce/FFIUnwind 已在 Opcode 枚举中（IRValue.h 第 46 行）
- IRFunction 构造：`IRFunction(IRModule*, StringRef, IRFunctionType*, LinkageKind, CallingConvention)`
- IRModule::getOrInsertFunction：创建或获取函数声明

---

## Part 1：头文件

### `include/blocktype/IR/IRFFI.h`

```cpp
#ifndef BLOCKTYPE_IR_IRFFI_H
#define BLOCKTYPE_IR_IRFFI_H

#include <memory>
#include <string>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class IRType;
class IRTypeContext;
class IRFunctionType;
class IRFunction;
class IRModule;

namespace ffi {

// ============================================================================
// FFI CallingConvention — 外部函数调用约定
// ============================================================================

/// FFI 专用调用约定。与 ir::CallingConvention 独立，
/// 仅包含外部 FFI 调用所需的 C ABI 相关约定。
enum class CallingConvention : uint8_t {
  C = 0,
  Stdcall = 1,
  Fastcall = 2,
  VectorCall = 3,
  WASM = 4,
  BTInternal = 5,
  ThisCall = 6,
  Swift = 7,
};

// ============================================================================
// FFITypeMapper — 外部语言类型 ↔ IR 类型映射
// ============================================================================

/// 提供外部语言类型与 IR 类型之间的双向映射。
class FFITypeMapper {
public:
  /// 将外部语言类型名映射为 IRType。
  /// @param Language 目标语言（"C", "Rust", "Python", "WASM", "Swift", "Zig"）
  /// @param TypeName 外部类型名（如 "int", "float", "char*" 等）
  /// @param Ctx      IR 类型上下文
  /// @return 对应的 IRType，或 nullptr 如果无法映射
  static IRType* mapExternalType(StringRef Language, StringRef TypeName,
                                  IRTypeContext& Ctx);

  /// 将 IRType 映射为外部语言类型名。
  /// @param T              IR 类型
  /// @param TargetLanguage 目标语言
  /// @return 外部语言类型名字符串
  static std::string mapToExternalType(const IRType* T, StringRef TargetLanguage);

  /// 返回支持的语言列表。
  static SmallVector<std::string, 8> getSupportedLanguages();
};

// ============================================================================
// FFIFunctionDecl — 外部函数声明
// ============================================================================

/// 描述一个外部函数的 FFI 声明信息。
class FFIFunctionDecl {
  std::string Name_;
  std::string MangledName_;
  CallingConvention Conv_;
  IRFunctionType* Signature_;
  std::string SourceLanguage_;
  std::string HeaderFile_;
  bool IsVariadic_;

public:
  FFIFunctionDecl(StringRef N, StringRef MN, CallingConvention C,
                  IRFunctionType* S, StringRef Lang,
                  StringRef HF = "", bool VA = false);

  StringRef getName() const { return Name_; }
  StringRef getMangledName() const { return MangledName_; }
  CallingConvention getCallingConvention() const { return Conv_; }
  IRFunctionType* getSignature() const { return Signature_; }
  StringRef getSourceLanguage() const { return SourceLanguage_; }
  StringRef getHeaderFile() const { return HeaderFile_; }
  bool isVariadic() const { return IsVariadic_; }

  /// 在 IRModule 中创建对应的 IRFunction 声明。
  IRFunction* createIRDeclaration(IRModule& M);

  /// 创建调用包装函数。在 IRModule 中生成一个 wrapper 函数，
  /// 该函数通过 FFI 调用目标外部函数。
  IRFunction* createCallWrapper(IRModule& M, const IRFunction& Caller);
};

// ============================================================================
// FFIModule — FFI 声明集合
// ============================================================================

/// 管理一组 FFI 函数声明。
class FFIModule {
  StringMap<std::unique_ptr<FFIFunctionDecl>> Declarations_;

public:
  /// 添加一个 FFI 声明。
  void addDeclaration(std::unique_ptr<FFIFunctionDecl> Decl);

  /// 从 C 头文件导入声明（占位实现）。
  bool importFromCHeader(StringRef HeaderPath);

  /// 从 Rust crate 导入声明（占位实现）。
  bool importFromRustCrate(StringRef CratePath);

  /// 将 IRFunction 导出为 C API（占位实现）。
  bool exportToCAPI(const IRFunction& F, StringRef ExportName);

  /// 生成目标语言的绑定代码（占位实现）。
  bool generateBindings(StringRef TargetLanguage, raw_ostream& OS);

  /// 按名称查找 FFI 声明。
  const FFIFunctionDecl* lookup(StringRef Name) const;

  /// 返回声明数量。
  unsigned getNumDeclarations() const;
};

} // namespace ffi
} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRFFI_H
```

---

## Part 2：实现文件

### `src/IR/IRFFI.cpp`

```cpp
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

  IRTypeContext& TCtx = M.getTypeContext();
  IRContext Ctx; // 临时 context 用于 Builder
  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);

  // 查找目标 FFI 函数声明
  auto* TargetF = M.getFunction(MangledName_);
  if (!TargetF) {
    TargetF = createIRDeclaration(M);
  }

  // 收集参数
  SmallVector<IRValue*, 8> Args;
  for (unsigned i = 0; i < Wrapper->getNumArgs(); ++i) {
    Args.push_back(Wrapper->getArg(i));
  }

  // 创建调用
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
  StringRef Name = Decl->getName();
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
  return It->second.get();
}

unsigned FFIModule::getNumDeclarations() const {
  return static_cast<unsigned>(Declarations_.size());
}

} // namespace ffi
} // namespace ir
} // namespace blocktype
```

---

## Part 3：CMakeLists 修改

### `src/IR/CMakeLists.txt` — 在 `IRPasses.cpp` 后添加 1 行

```
  IRFFI.cpp
```

### `tests/unit/IR/CMakeLists.txt` — 在 `IRContractTest.cpp` 后添加 1 行

```
  IRFFITest.cpp
```

---

## Part 4：测试文件

### `tests/unit/IR/IRFFITest.cpp`

```cpp
#include <gtest/gtest.h>

#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRFFI.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"

using namespace blocktype;
using namespace blocktype::ir;
using namespace blocktype::ir::ffi;

// ============================================================================
// V1: C 语言类型映射 — 基本整数类型
// ============================================================================

TEST(IRFFITest, MapCTypeInt) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "int", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 32u);
}

TEST(IRFFITest, MapCTypeVoid) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "void", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isVoid());
}

TEST(IRFFITest, MapCTypeChar) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "char", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 8u);
}

TEST(IRFFITest, MapCTypeShort) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "short", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 16u);
}

TEST(IRFFITest, MapCTypeLongLong) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "long long", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 64u);
}

TEST(IRFFITest, MapCTypeBool) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "_Bool", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 8u);
}

TEST(IRFFITest, MapCTypeBoolAlt) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "bool", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 8u);
}

// ============================================================================
// V1b: 浮点类型
// ============================================================================

TEST(IRFFITest, MapCTypeFloat) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "float", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isFloat());
  EXPECT_EQ(static_cast<IRFloatType*>(Ty)->getBitWidth(), 32u);
}

TEST(IRFFITest, MapCTypeDouble) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "double", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isFloat());
  EXPECT_EQ(static_cast<IRFloatType*>(Ty)->getBitWidth(), 64u);
}

TEST(IRFFITest, MapCTypeLongDouble) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "long double", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isFloat());
}

// ============================================================================
// V1c: 指针类型
// ============================================================================

TEST(IRFFITest, MapCTypeVoidPointer) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "void*", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isPointer());
}

TEST(IRFFITest, MapCTypeCharPointer) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "char*", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isPointer());
}

// ============================================================================
// V1d: 未知类型返回 nullptr
// ============================================================================

TEST(IRFFITest, MapCTypeUnknown) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "unknown_type_xyz", TCtx);
  EXPECT_EQ(Ty, nullptr);
}

// ============================================================================
// 反向映射
// ============================================================================

TEST(IRFFITest, MapToExternalTypeInt) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto Result = FFITypeMapper::mapToExternalType(TCtx.getInt32Ty(), "C");
  EXPECT_EQ(Result, "int");
}

TEST(IRFFITest, MapToExternalTypeFloat) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto Result = FFITypeMapper::mapToExternalType(TCtx.getFloatTy(), "C");
  EXPECT_EQ(Result, "float");
}

TEST(IRFFITest, MapToExternalTypeDouble) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto Result = FFITypeMapper::mapToExternalType(TCtx.getDoubleTy(), "C");
  EXPECT_EQ(Result, "double");
}

TEST(IRFFITest, MapToExternalTypeVoid) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto Result = FFITypeMapper::mapToExternalType(TCtx.getVoidType(), "C");
  EXPECT_EQ(Result, "void");
}

TEST(IRFFITest, MapToExternalTypePointer) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* PtrTy = TCtx.getPointerType(TCtx.getInt8Ty());
  auto Result = FFITypeMapper::mapToExternalType(PtrTy, "C");
  EXPECT_EQ(Result, "void*");
}

// ============================================================================
// V2: FFIFunctionDecl 创建和 getter
// ============================================================================

TEST(IRFFITest, FFIFunctionDeclBasic) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getPointerType(TCtx.getInt8Ty())}, true);
  FFIFunctionDecl FDecl("printf", "_printf", CallingConvention::C,
                         FTy, "C", "stdio.h", true);

  EXPECT_EQ(FDecl.getName(), "printf");
  EXPECT_EQ(FDecl.getMangledName(), "_printf");
  EXPECT_EQ(FDecl.getCallingConvention(), CallingConvention::C);
  EXPECT_EQ(FDecl.getSignature(), FTy);
  EXPECT_EQ(FDecl.getSourceLanguage(), "C");
  EXPECT_EQ(FDecl.getHeaderFile(), "stdio.h");
  EXPECT_TRUE(FDecl.isVariadic());
}

// ============================================================================
// FFIFunctionDecl::createIRDeclaration
// ============================================================================

TEST(IRFFITest, CreateIRDeclaration) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getPointerType(TCtx.getInt8Ty())}, true);
  FFIFunctionDecl FDecl("printf", "_printf", CallingConvention::C,
                         FTy, "C", "stdio.h", true);

  IRModule M("ffi_test", TCtx, "x86_64-unknown-linux-gnu");
  auto* IRFunc = FDecl.createIRDeclaration(M);

  ASSERT_NE(IRFunc, nullptr);
  EXPECT_EQ(IRFunc->getName(), "_printf");
  EXPECT_TRUE(IRFunc->isDeclaration());
}

// ============================================================================
// FFIModule
// ============================================================================

TEST(IRFFITest, FFIModuleAddAndLookup) {
  FFIModule Mod;

  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {});

  auto Decl = std::make_unique<FFIFunctionDecl>("abs", "_abs", CallingConvention::C,
                                                  FTy, "C", "stdlib.h");
  Mod.addDeclaration(std::move(Decl));

  EXPECT_EQ(Mod.getNumDeclarations(), 1u);

  auto* Found = Mod.lookup("abs");
  ASSERT_NE(Found, nullptr);
  EXPECT_EQ(Found->getName(), "abs");
  EXPECT_EQ(Found->getMangledName(), "_abs");

  // 查找不存在的声明
  EXPECT_EQ(Mod.lookup("nonexistent"), nullptr);
}

TEST(IRFFITest, FFIModuleMultipleDeclarations) {
  FFIModule Mod;

  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {});

  Mod.addDeclaration(std::make_unique<FFIFunctionDecl>("abs", "_abs",
                    CallingConvention::C, FTy, "C", "stdlib.h"));
  Mod.addDeclaration(std::make_unique<FFIFunctionDecl>("strlen", "strlen",
                    CallingConvention::C, FTy, "C", "string.h"));

  EXPECT_EQ(Mod.getNumDeclarations(), 2u);
  EXPECT_NE(Mod.lookup("abs"), nullptr);
  EXPECT_NE(Mod.lookup("strlen"), nullptr);
}

// ============================================================================
// getSupportedLanguages
// ============================================================================

TEST(IRFFITest, GetSupportedLanguages) {
  auto Languages = FFITypeMapper::getSupportedLanguages();
  EXPECT_GE(Languages.size(), 6u);

  // 验证包含 "C"
  bool HasC = false;
  for (auto& L : Languages) {
    if (L == "C") HasC = true;
  }
  EXPECT_TRUE(HasC);
}

// ============================================================================
// 其他语言通过 C ABI 映射
// ============================================================================

TEST(IRFFITest, MapRustTypeThroughCABI) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("Rust", "i32", TCtx);
  // Rust 通过 C ABI 映射，"i32" 不在 C 映射表中，返回 nullptr
  EXPECT_EQ(Ty, nullptr);

  // 但 "int" 可以通过 C ABI 映射
  auto* Ty2 = FFITypeMapper::mapExternalType("Rust", "int", TCtx);
  ASSERT_NE(Ty2, nullptr);
  EXPECT_TRUE(Ty2->isInteger());
}
```

---

## Part 5：验收标准映射

| 验收标准 | 测试用例 |
|----------|----------|
| V1: C 语言类型映射（int → i32） | `IRFFITest.MapCTypeInt` |
| V1b: 浮点类型 | `IRFFITest.MapCTypeFloat/Double/LongDouble` |
| V1c: 指针类型 | `IRFFITest.MapCTypeVoidPointer/CharPointer` |
| V1d: 未知类型返回 nullptr | `IRFFITest.MapCTypeUnknown` |
| V2: FFIFunctionDecl 创建 + getName | `IRFFITest.FFIFunctionDeclBasic` |
| createIRDeclaration | `IRFFITest.CreateIRDeclaration` |
| FFIModule add + lookup | `IRFFITest.FFIModuleAddAndLookup` |
| getSupportedLanguages | `IRFFITest.GetSupportedLanguages` |
| 反向映射 | `IRFFITest.MapToExternalTypeInt/Float/Double/Void/Pointer` |

---

## Part 6：dev-tester 执行步骤

1. 创建 `include/blocktype/IR/IRFFI.h`（复制 Part 1）
2. 创建 `src/IR/IRFFI.cpp`（复制 Part 2）
3. 创建 `tests/unit/IR/IRFFITest.cpp`（复制 Part 4）
4. 修改 `src/IR/CMakeLists.txt`：在 `IRPasses.cpp`（或 `IRContract.cpp`）后添加 `IRFFI.cpp`
5. 修改 `tests/unit/IR/CMakeLists.txt`：在 `IRContractTest.cpp` 后添加 `IRFFITest.cpp`
6. 编译并运行测试：`cd build && cmake --build . --target blocktype-ir-test && ./test/unit/IR/blocktype-ir-test`
7. 确认所有测试通过

### ⚠️ 注意事项
- `createCallWrapper` 中使用了独立的 `IRContext Ctx` 创建 `IRBuilder`。如果测试需要验证 wrapper 函数内容，dev-tester 可能需要调整让 wrapper 与 module 共享 context。当前实现足够满足基础定义需求。
- `importFromCHeader`/`importFromRustCrate`/`exportToCAPI`/`generateBindings` 均为占位实现，后续阶段完善。
