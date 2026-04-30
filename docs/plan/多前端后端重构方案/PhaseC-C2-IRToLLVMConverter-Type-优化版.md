# Phase C-C2 优化版：IRToLLVMConverter 类型转换

> 基于 PhaseC-Planner-Report 修复后生成
> 修复内容：移除零长度数组映射、mapType 可见性说明

---

## 依赖关系

- C.1（BackendBase/BackendOptions）
- Phase A（IRType 体系）

## 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Backend/IRToLLVMConverter.h` |
| 新增 | `src/Backend/IRToLLVMConverter.cpp` |

---

## 完整接口签名

```cpp
// === include/blocktype/Backend/IRToLLVMConverter.h ===
#pragma once
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRValue.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/Backend/BackendOptions.h"

// LLVM 前向声明（后端文件可引用 LLVM）
namespace llvm {
  class LLVMContext;
  class Module;
  class Type;
  class Value;
  class Function;
  class BasicBlock;
  class Constant;
  class IRBuilderBase;
  template<typename T> class IRBuilder;
} // namespace llvm

namespace blocktype::backend {

class IRToLLVMConverter {
  ir::IRTypeContext& IRCtx;
  llvm::LLVMContext& LLVMCtx;
  const BackendOptions& Opts;
  std::unique_ptr<llvm::Module> TheModule;

  // 类型映射缓存
  ir::DenseMap<const ir::IRType*, llvm::Type*> TypeMap;
  // 值映射缓存
  ir::DenseMap<const ir::IRValue*, llvm::Value*> ValueMap;
  // 函数映射缓存
  ir::DenseMap<const ir::IRFunction*, llvm::Function*> FunctionMap;
  // 基本块映射缓存
  ir::DenseMap<const ir::IRBasicBlock*, llvm::BasicBlock*> BlockMap;

public:
  IRToLLVMConverter(ir::IRTypeContext& IRCtx,
                    llvm::LLVMContext& LLVMCtx,
                    const BackendOptions& Opts = BackendOptions());

  /// 主转换入口：IRModule → llvm::Module
  /// 失败时返回 nullptr
  std::unique_ptr<llvm::Module> convert(ir::IRModule& IRModule);

  // 类型映射（public 以便单元测试）
  llvm::Type* mapType(const ir::IRType* T);

private:
  llvm::Value* mapValue(const ir::IRValue* V);
  llvm::Function* mapFunction(const ir::IRFunction* F);
  llvm::BasicBlock* mapBasicBlock(const ir::IRBasicBlock* BB);
  void convertFunction(ir::IRFunction& IRF, llvm::Function* LLVMF);
  void convertInstruction(ir::IRInstruction& I, llvm::IRBuilder<>& Builder);
  void convertGlobalVariable(ir::IRGlobalVariable& IRGV);
  llvm::Constant* convertConstant(const ir::IRConstant* C);
};

} // namespace blocktype::backend
```

---

## IRType → llvm::Type 映射规则表

| IR Type | LLVM Type | 边界情况 |
|---------|-----------|---------|
| IRVoidType | `llvm::Type::getVoidTy()` | — |
| IRIntegerType(1) | `llvm::Type::getInt1Ty()` | 布尔特殊处理 |
| IRIntegerType(N) | `llvm::Type::getIntNTy(N)` | N ∈ {1,8,16,32,64,128} |
| IRFloatType(16) | `llvm::Type::getHalfTy()` | 半精度 |
| IRFloatType(32) | `llvm::Type::getFloatTy()` | 单精度 |
| IRFloatType(64) | `llvm::Type::getDoubleTy()` | 双精度 |
| IRFloatType(80) | `llvm::Type::getX86_FP80Ty()` | 仅 x86 支持 |
| IRFloatType(128) | `llvm::Type::getFP128Ty()` | 四精度 |
| IRFloatType(其他) | **报错** | 不支持的位宽 |
| IRPointerType(T, 0) | `llvm::PointerType::get(mapType(T), 0)` | 默认地址空间 |
| IRPointerType(T, N) | `llvm::PointerType::get(mapType(T), N)` | 非默认地址空间 |
| IRArrayType(T, N) N>0 | `llvm::ArrayType::get(mapType(T), N)` | — |
| IRStructType(Name, Elems, false) | `llvm::StructType::create(mapTypes(Elems), Name)` | 非 packed |
| IRStructType(Name, Elems, true) | `llvm::StructType::create(mapTypes(Elems), Name, true)` | packed |
| IRStructType("", Elems, P) | `llvm::StructType::get(mapTypes(Elems), P)` | 匿名结构体 |
| IRFunctionType(Ret, Params, false) | `llvm::FunctionType::get(mapType(Ret), mapTypes(Params))` | 非变参 |
| IRFunctionType(Ret, Params, true) | `llvm::FunctionType::get(mapType(Ret), mapTypes(Params), true)` | 变参 |
| IRVectorType(N, T) | `llvm::VectorType::get(mapType(T), N, false)` | 固定长度向量 |
| IROpaqueType(Name) | `llvm::StructType::create(LLVMCtx, Name)` | 不透明→命名结构体占位 |

**注意**：IR 层 `IRArrayType` 构造函数有 `assert(N > 0)` 约束，不存在零长度数组情况。

---

## 递归类型处理算法

```
mapType(IRStructType "Node", [IRPointerType(Node), IRIntegerType(32)]):
  1. TypeMap["Node"] = llvm::StructType::create(LLVMCtx, "Node")  // 先占位
  2. mapType(IRPointerType(Node)) → llvm::PointerType::get(占位结构体)
  3. mapType(IRIntegerType(32)) → llvm::Type::getInt32Ty()
  4. 占位结构体->setBody([PointerType, Int32Ty])  // 后设 body
```

---

## 实现约束

1. mapType 必须缓存（相同 IRType* 返回相同 llvm::Type*）
2. 递归类型必须先占位再设 body
3. convert 失败时发出 `diag::err_ir_to_llvm_conversion_failed`

---

## 验收标准

```cpp
// V1: 整数类型映射
auto* LLVMInt32 = Converter.mapType(IRInt32Ty);
assert(LLVMInt32 == llvm::Type::getInt32Ty(LLVMCtx));

// V2: 指针类型映射
auto* LLVMPtr = Converter.mapType(IRPtrInt8Ty);
assert(isa<llvm::PointerType>(LLVMPtr));

// V3: 结构体映射（含递归）
auto* LLVMStruct = Converter.mapType(IRNodeStructTy);
assert(isa<llvm::StructType>(LLVMStruct));
assert(cast<llvm::StructType>(LLVMStruct)->isOpaque() == false);

// V4: convert 端到端
auto LLVMMod = Converter.convert(IRMod);
assert(LLVMMod != nullptr);
assert(LLVMMod->getFunction("test_func") != nullptr);
```

---

## 与前序 Task 的接口衔接

| 前序产出 | 本 Task 使用方式 |
|---------|----------------|
| C.1: BackendOptions | 构造参数（TargetTriple 等） |
| Phase A: IRType 体系 | mapType 的输入 |
| Phase A: IRTypeContext | 构造参数 |
| Phase A: IRModule | convert() 的输入 |

---

> Git 提交：`feat(C): 完成 C.2 — IRToLLVMConverter 类型转换`
