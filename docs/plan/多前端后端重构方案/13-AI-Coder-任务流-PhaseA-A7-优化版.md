# Task A.7（优化版）：IRSerializer（文本格式 + 二进制格式）

> 本文档是 Task A.7 的优化版本，替代 `11-AI-Coder-任务流-PhaseA.md` 中第 1565~1651 行的 A.7 定义。
> 本文档自包含，dev-runner 无需查阅其他文档即可编码。

---

## 依赖

- A.1（IRType 体系、IRTypeContext）
- A.2（IRValue、User、Use、Opcode 枚举、ValueKind 枚举）
- A.3（IRInstruction、IRConstant 全系列、IRFormatVersion、IRFileHeader）
- A.4（IRModule、IRFunction、IRBasicBlock、IRGlobalVariable、IRGlobalAlias、IRMetadata）
- A.5（IRBuilder，用于构造验收测试用例）
- A.6（Pass 基类、VerifierPass，用于验收时验证往返结果）

---

## 产出文件

| 操作 | 文件路径 | 说明 |
|------|----------|------|
| 新增 | `include/blocktype/IR/IRSerializer.h` | SerializationDiagnostic + IRWriter + IRReader |
| 新增 | `src/IR/IRSerializer.cpp` | 完整实现 |
| 修改 | `src/IR/CMakeLists.txt` | 添加 IRSerializer.cpp |
| 新增 | `tests/unit/IR/IRSerializerTest.cpp` | 序列化/反序列化单元测试 |
| 修改 | `tests/unit/IR/CMakeLists.txt` | 添加 IRSerializerTest.cpp 测试目标 |

---

## 设计决策说明（4 个问题的解决方案）

### 问题 1：parseText 返回的 IRModule 与 IRTypeContext 生命周期关系不清

**决策：明确 IRModule 仅持有 `IRTypeContext&` 引用，不拥有 Ctx。调用者必须保证 Ctx 的生命周期长于返回的 Module。**

理由：
- `IRModule` 的构造函数签名为 `IRModule(StringRef N, IRTypeContext& Ctx, ...)`，内部存储 `IRTypeContext& TypeCtx`，是引用而非拥有
- 这是 A.4 已确立的设计，序列化器必须遵守
- `parseText` 和 `parseBitcode` 传入 `IRTypeContext& Ctx`，返回的 `unique_ptr<IRModule>` 内部持有该 Ctx 引用
- `readFile` 也传入 `IRTypeContext& Ctx`，语义相同
- 调用者通常这样使用：
  ```cpp
  IRContext IRCtx;                     // IRCtx 拥有 TypeCtx
  IRTypeContext& TCtx = IRCtx.getTypeContext();
  auto M = IRReader::parseText(Text, TCtx);  // M 持有 TCtx 引用
  // IRCtx 和 M 在同一作用域，生命周期匹配
  ```

### 问题 2：文本解析器缺少错误处理接口

**决策：定义 `SerializationDiagnostic` 结构体，解析失败时通过输出参数收集错误信息。**

- 与 A.6 的 `VerificationDiagnostic` 设计模式一致
- 包含：错误类别、行号、列号（仅文本格式有行列信息）、错误描述
- 所有 `parse*` 方法增加可选的 `SerializationDiagnostic*` 输出参数
- 解析失败时返回 `nullptr`，调用者通过 Diagnostic 获取详细错误信息

### 问题 3：二进制格式规范过于粗略

**决策：定义完整的二进制编码规范。**

二进制格式分层结构：
```
[IRFileHeader: 26 字节]
[GlobalSection]
[FunctionDeclSection]
[FunctionSection]
[StringTable]
```

每个 Section 内部有详细的编码规范（见下文"二进制格式编码规范"章节）。

### 问题 4：readFile 需要自动检测格式

**决策：`readFile` 通过文件头魔数自动检测格式。**

检测逻辑：
1. 以二进制模式打开文件，读取前 4 字节
2. 如果前 4 字节 == `"BTIR"` → 调用 `parseBitcode`
3. 否则 → 调用 `parseText`（将整个文件内容作为文本解析）
4. 文件打开失败或为空 → 返回 `nullptr`，设置 Diagnostic 错误

---

## 必须实现的类型定义

### IRSerializer.h — 完整定义

```cpp
#ifndef BLOCKTYPE_IR_IRSERIALIZER_H
#define BLOCKTYPE_IR_IRSERIALIZER_H

#include <cstdint>
#include <memory>
#include <string>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class IRModule;
class IRTypeContext;

// ============================================================================
// 序列化诊断
// ============================================================================

/// 序列化错误的类别
enum class SerializationErrorKind : uint8_t {
  Unknown          = 0,
  InvalidFormat    = 1,  // 格式不合法（语法错误、魔数不匹配等）
  InvalidType      = 2,  // 类型编码无法识别
  InvalidOpcode    = 3,  // 操作码无法识别
  InvalidValue     = 4,  // 值引用无法解析
  IOError          = 5,  // 文件 I/O 错误
  VersionMismatch  = 6,  // 版本不兼容
  TruncatedData    = 7,  // 数据截断（二进制格式不够长）
};

/// 单条序列化诊断信息
struct SerializationDiagnostic {
  SerializationErrorKind Kind = SerializationErrorKind::Unknown;
  unsigned Line = 0;     // 文本格式的行号（二进制格式为 0）
  unsigned Column = 0;   // 文本格式的列号（二进制格式为 0）
  std::string Message;

  SerializationDiagnostic() = default;
  SerializationDiagnostic(SerializationErrorKind K, const std::string& Msg,
                          unsigned L = 0, unsigned C = 0)
    : Kind(K), Line(L), Column(C), Message(Msg) {}
};

// ============================================================================
// IRWriter — 序列化器
// ============================================================================

/// IR 文本/二进制序列化写入器。
///
/// 使用方式：
///   std::string Text;
///   raw_string_ostream OS(Text);
///   bool OK = IRWriter::writeText(Module, OS);
class IRWriter {
public:
  /// 将 IRModule 序列化为人类可读的文本格式（BTIR text format）。
  /// @param M  要序列化的模块
  /// @param OS 输出流
  /// @return true=成功, false=失败
  static bool writeText(const IRModule& M, raw_ostream& OS);

  /// 将 IRModule 序列化为紧凑的二进制格式（BTIR binary format）。
  /// @param M  要序列化的模块
  /// @param OS 输出流
  /// @return true=成功, false=失败
  static bool writeBitcode(const IRModule& M, raw_ostream& OS);
};

// ============================================================================
// IRReader — 反序列化器
// ============================================================================

/// IR 文本/二进制反序列化读取器。
///
/// 使用方式：
///   SerializationDiagnostic Diag;
///   auto M = IRReader::parseText(Text, TCtx, &Diag);
///   if (!M) { /* 检查 Diag */ }
class IRReader {
public:
  /// 从文本格式字符串解析 IRModule。
  /// @param Text  BTIR 文本格式的字符串
  /// @param Ctx   类型上下文（用于创建类型实例，调用者保证生命周期）
  /// @param Diag  可选的错误收集器
  /// @return 解析成功返回 IRModule，失败返回 nullptr
  static std::unique_ptr<IRModule> parseText(
      StringRef Text, IRTypeContext& Ctx,
      SerializationDiagnostic* Diag = nullptr);

  /// 从二进制格式数据解析 IRModule。
  /// @param Data  BTIR 二进制格式的数据
  /// @param Ctx   类型上下文（调用者保证生命周期）
  /// @param Diag  可选的错误收集器
  /// @return 解析成功返回 IRModule，失败返回 nullptr
  static std::unique_ptr<IRModule> parseBitcode(
      StringRef Data, IRTypeContext& Ctx,
      SerializationDiagnostic* Diag = nullptr);

  /// 从文件读取并自动检测格式（文本或二进制）。
  /// 检测逻辑：读取文件前 4 字节，若为 "BTIR" 则按二进制格式解析，否则按文本格式。
  /// @param Path 文件路径
  /// @param Ctx  类型上下文（调用者保证生命周期）
  /// @param Diag 可选的错误收集器
  /// @return 解析成功返回 IRModule，失败返回 nullptr
  static std::unique_ptr<IRModule> readFile(
      StringRef Path, IRTypeContext& Ctx,
      SerializationDiagnostic* Diag = nullptr);
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRSERIALIZER_H
```

---

## 文本格式规范（BTIR Text Format）

### 完整语法

```
<btir>         ::= <module-header> <module-body>
<module-header>::= "module" STRING ("target" STRING)? ("datalayout" STRING)?
<module-body>  ::= (<global-decl> | <function-decl> | <function-def>)*

<global-decl>  ::= "global" "@" IDENT ":" <type> ("=" <constant>)?
<function-decl>::= "declare" <type> "@" IDENT "(" <param-list> ")"

<function-def> ::= "function" <type> "@" IDENT "(" <param-list> ")" "{" 
                    <basic-block>* "}"
<basic-block>  ::= IDENT ":" <instruction>*
<param-list>   ::= (<type> ("," <type>)*)?

<instruction>  ::= "%" IDENT "=" <opcode> <operand-list>
                 | <opcode> <operand-list>
<operand-list> ::= (<operand> ("," <operand>)*)?
<operand>      ::= VALUE_REF | CONSTANT | "@" IDENT | LABEL

<type>         ::= "void" | "i1" | "i8" | "i16" | "i32" | "i64" | "i128"
                 | "f16" | "f32" | "f64" | "f80" | "f128"
                 | <type> "*"                    ; 指针类型
                 | "[" DECIMAL "x" <type> "]"    ; 数组类型
                 | "<" DECIMAL "x" <type> ">"    ; 向量类型
                 | "struct" "{" <type-list> "}"  ; 匿名结构体
                 | IDENT                         ; 命名结构体/不透明类型
                 | <type> "(" <type-list> ")" ("..."?)?  ; 函数类型

<constant>     ::= DECIMAL | HEX_FLOAT | "null" | "undef" | "zeroinitializer"
                 | "true" | "false"

VALUE_REF      ::= "%" IDENT | "%" DECIMAL
LABEL          ::= IDENT
IDENT          ::= [a-zA-Z_][a-zA-Z0-9_.]*
STRING         ::= '"' [^"]* '"'
DECIMAL        ::= [0-9]+
HEX_FLOAT      ::= "0x" [0-9a-fA-F]+
```

### 示例

```
; BTIR text format — 注释以分号开头，到行尾结束
module "test" target "x86_64-unknown-linux-gnu"

global @g1 : i32 = 42

declare i32 @external_func(i32, i32)

function i32 @add(i32 %a, i32 %b) {
entry:
  %1 = add i32 %a, %b
  ret i32 %1
}

function i32 @main() {
entry:
  %1 = call @add(i32 1, i32 2)
  ret i32 %1
}
```

### 类型编码规则

| IR 类型 | 文本表示 | 示例 |
|---------|----------|------|
| `IRVoidType` | `void` | `void` |
| `IRIntegerType(1)` | `i1` | `i1` |
| `IRIntegerType(8)` | `i8` | `i8` |
| `IRIntegerType(16)` | `i16` | `i16` |
| `IRIntegerType(32)` | `i32` | `i32` |
| `IRIntegerType(64)` | `i64` | `i64` |
| `IRIntegerType(128)` | `i128` | `i128` |
| `IRFloatType(16)` | `f16` | `f16` |
| `IRFloatType(32)` | `f32` | `f32` |
| `IRFloatType(64)` | `f64` | `f64` |
| `IRFloatType(80)` | `f80` | `f80` |
| `IRFloatType(128)` | `f128` | `f128` |
| `IRPointerType(T)` | `<T>*` | `i32*`, `void*` |
| `IRArrayType(N, T)` | `[N x <T>]` | `[10 x i32]` |
| `IRVectorType(N, T)` | `<N x <T>>` | `<4 x float>` |
| `IRStructType` | `struct { <T1>, <T2> }` | `struct { i32, f64 }` |
| `IRFunctionType` | `<RetType> (<T1>, <T2>)` | `i32 (i32, i32)` |
| `IROpaqueType` | 类型名直接输出 | `MyOpaqueType` |

### 指令编码规则

| Opcode | 文本格式 | 示例 |
|--------|----------|------|
| Ret (void) | `ret void` | `ret void` |
| Ret (value) | `ret <type> <val>` | `ret i32 %1` |
| Br | `br label <dest>` | `br label %loop` |
| CondBr | `br i1 <cond>, label <true>, label <false>` | `br i1 %c, label %yes, label %no` |
| Add | `add <type> <lhs>, <rhs>` | `%1 = add i32 %a, %b` |
| FAdd | `fadd <type> <lhs>, <rhs>` | `%1 = fadd f32 %a, %b` |
| ICmp | `icmp <pred> <type> <lhs>, <rhs>` | `%1 = icmp eq i32 %a, %b` |
| Alloca | `alloca <type>` | `%p = alloca i32` |
| Load | `load <type>, <ptrtype> <ptr>` | `%v = load i32, i32* %p` |
| Store | `store <type> <val>, <ptrtype> <ptr>` | `store i32 %v, i32* %p` |
| Call | `call <type> @<func>(<args>)` | `%1 = call i32 @add(i32 %a, i32 %b)` |
| Phi | `phi <type> [<val>, %<bb>] ...` | `%x = phi i32 [42, %entry], [%y, %loop]` |
| Select | `select i1 <cond>, <type> <tval>, <type> <fval>` | `%r = select i1 %c, i32 %a, i32 %b` |

### ICmp 谓词编码

| 枚举值 | 文本表示 |
|--------|----------|
| `ICmpPred::EQ` | `eq` |
| `ICmpPred::NE` | `ne` |
| `ICmpPred::UGT` | `ugt` |
| `ICmpPred::UGE` | `uge` |
| `ICmpPred::ULT` | `ult` |
| `ICmpPred::ULE` | `ule` |
| `ICmpPred::SGT` | `sgt` |
| `ICmpPred::SGE` | `sge` |
| `ICmpPred::SLT` | `slt` |
| `ICmpPred::SLE` | `sle` |

### FCmp 谓词编码

| 枚举值 | 文本表示 |
|--------|----------|
| `FCmpPred::False` | `false` |
| `FCmpPred::OEQ` | `oeq` |
| `FCmpPred::OGT` | `ogt` |
| `FCmpPred::OGE` | `oge` |
| `FCmpPred::OLT` | `olt` |
| `FCmpPred::OLE` | `ole` |
| `FCmpPred::ONE` | `one` |
| `FCmpPred::ORD` | `ord` |
| `FCmpPred::UNO` | `uno` |
| `FCmpPred::UEQ` | `ueq` |
| `FCmpPred::UGT` | `ugt` |
| `FCmpPred::UGE` | `uge` |
| `FCmpPred::ULT` | `ult` |
| `FCmpPred::ULE` | `ule` |
| `FCmpPred::UNE` | `une` |
| `FCmpPred::True` | `true` |

---

## 二进制格式编码规范（BTIR Binary Format）

### 整体布局

```
Offset  Size    Content
0       4       Magic: "BTIR" (0x42 0x54 0x49 0x52)
4       2       Version.Major (uint16_t, little-endian)
6       2       Version.Minor (uint16_t, little-endian)
8       2       Version.Patch (uint16_t, little-endian)
10      4       Flags (uint32_t, reserved, must be 0)
14      4       ModuleOffset (uint32_t, offset to module data from file start)
18      4       StringTableOffset (uint32_t, offset to string table)
22      4       StringTableSize (uint32_t, size of string table in bytes)
--- 以下为 Module Data（从 ModuleOffset 开始）---
26     ...      GlobalSection
...    ...      FunctionDeclSection
...    ...      FunctionSection
--- 以下为 String Table（从 StringTableOffset 开始）---
...    ...      StringTable
```

> 注：IRFileHeader 已在 `IRFormatVersion.h` 中定义为 26 字节（含 static_assert）。Module 数据紧跟文件头之后。

### 通用编码原语

| 类型 | 编码方式 | 字节数 |
|------|----------|--------|
| `uint8_t` | 直接写入 | 1 |
| `uint16_t` | Little-endian | 2 |
| `uint32_t` | Little-endian | 4 |
| `uint64_t` | Little-endian | 8 |
| `StringRef` | uint32_t Offset + uint32_t Length（指向 StringTable） | 8 |
| `IRType*` | TypeRecord（见下文） | 变长 |
| Opcode | uint16_t | 2 |
| ValueKind | uint8_t | 1 |
| Value引用 | uint32_t ValueID | 4 |

### 字符串表格式

```
[StringTable]
uint32_t  NumStrings          ; 字符串数量
对于每个字符串:
  uint32_t  Length            ; 字符串长度（不含 \0）
  char[Length] Data           ; 字符串数据（不含 \0）
```

字符串表中的索引按写入顺序分配（0, 1, 2, ...）。所有需要字符串的地方（模块名、函数名、BB 名、Value 名等）通过 `(Offset, Length)` 引用字符串表。

### 类型编码（TypeRecord）

```
uint8_t   Kind                ; IRType::Kind 枚举值
依据 Kind 不同:
  Void(0):   (无额外数据)
  Bool(1):   (无额外数据)
  Integer(2): uint32_t BitWidth
  Float(3):  uint32_t BitWidth
  Pointer(4): TypeRecord(Pointee) + uint32_t AddressSpace
  Array(5):  uint64_t NumElements + TypeRecord(Element)
  Struct(6): StringRef(Name) + uint32_t NumFields + TypeRecord[NumFields] + uint8_t(IsPacked)
  Function(7): TypeRecord(Ret) + uint8_t(IsVarArg) + uint32_t NumParams + TypeRecord[NumParams]
  Vector(8): uint32_t NumElements + TypeRecord(Element)
  Opaque(9): StringRef(Name)
```

### 全局变量节（GlobalSection）

```
uint32_t  NumGlobals
对于每个全局变量:
  StringRef  Name
  TypeRecord Type
  uint8_t    IsConstant
  uint8_t    HasInitializer
  若 HasInitializer:
    ConstantRecord(Initializer)    ; 见下文
  uint32_t   Alignment
  uint32_t   AddressSpace
  uint8_t    LinkageKind
```

### 函数声明节（FunctionDeclSection）

```
uint32_t  NumFunctionDecls
对于每个声明:
  StringRef      Name
  TypeRecord     FunctionType
  uint8_t        LinkageKind
  uint8_t        CallingConvention
```

### 函数定义节（FunctionSection）

```
uint32_t  NumFunctions
对于每个函数:
  StringRef      Name
  TypeRecord     FunctionType
  uint8_t        LinkageKind
  uint8_t        CallingConvention
  uint32_t       FunctionAttrs
  uint32_t       NumBasicBlocks
  对于每个基本块:
    StringRef    Name
    uint32_t     NumInstructions
    对于每条指令:
      uint16_t   Opcode
      uint8_t    DialectID
      TypeRecord ResultType
      StringRef  Name (可能为空)
      uint32_t   NumOperands
      对于每个操作数:
        ValueRecord             ; 见下文
```

### 常量编码（ConstantRecord）

```
uint8_t   ValueKind
依据 ValueKind 不同:
  ConstantInt(0):  uint32_t BitWidth + uint64_t Value (注意：仅支持 <= 64 bit)
  ConstantFloat(1): uint32_t BitWidth + uint64_t Bits (IEEE 754 位模式)
  ConstantNull(2):  TypeRecord(Type)
  ConstantUndef(3): TypeRecord(Type)
  ConstantAggregateZero(6): TypeRecord(Type)
  ConstantStruct(4): TypeRecord(StructType) + uint32_t NumElements + ConstantRecord[NumElements]
  ConstantArray(5): TypeRecord(ArrayType) + uint32_t NumElements + ConstantRecord[NumElements]
  ConstantFunctionRef(7): uint32_t FunctionIndex (在函数列表中的索引)
  ConstantGlobalRef(8): uint32_t GlobalIndex (在全局变量列表中的索引)
```

### 值引用编码（ValueRecord）

指令操作数是对其他 Value 的引用，编码方式取决于 ValueKind：

```
uint8_t   ValueKind
依据 ValueKind:
  ConstantInt/Float/Null/Undef/AggregateZero/Struct/Array:
    ConstantRecord (内联编码)
  ConstantFunctionRef:
    uint32_t FunctionIndex
  ConstantGlobalRef:
    uint32_t GlobalIndex
  InstructionResult:
    uint32_t BasicBlockIndex + uint32_t InstructionIndex (在同一函数内)
  Argument:
    uint32_t ArgIndex (在同一函数内)
  BasicBlockRef:
    uint32_t BasicBlockIndex (在同一函数内)
```

### 版本兼容性

- 读取时检查 `IRFileHeader.Version` 是否与当前版本兼容（`IRFormatVersion::isCompatibleWith()`）
- Major 版本不匹配 → 拒绝解析，报告 `VersionMismatch` 错误
- Minor 版本等于或高于 Reader → 兼容

---

## CMakeLists.txt 修改

在 `src/IR/CMakeLists.txt` 的 `add_library(blocktype-ir ...)` 列表中追加：

```
IRSerializer.cpp
```

---

## 实现约束

1. **命名空间**：所有代码在 `namespace blocktype::ir` 中
2. **依赖限制**：`IRSerializer` 仅依赖 `blocktype-basic`（ADT）和 IR 层其他头文件，不链接 LLVM
3. **文本格式注释**：以 `;` 开头到行尾为注释，解析器必须跳过
4. **文本格式空白**：空行被忽略，缩进无语义但建议 2 空格
5. **二进制字节序**：所有多字节整数使用 Little-Endian
6. **writeText 语义**：调用 `IRModule::print(raw_ostream&)` 方法输出，或独立实现格式化输出。只要输出符合文本格式规范即可
7. **parseText 返回值**：成功返回 `unique_ptr<IRModule>`，失败返回 `nullptr`
8. **parseBitcode 返回值**：成功返回 `unique_ptr<IRModule>`，失败返回 `nullptr`
9. **raw_ostream 使用**：代码库中有 `raw_string_ostream`（写入 std::string），没有 `raw_svector_ostream`。二进制输出使用 `SmallVector<char, N>` + 直接 append 方式，或使用 `std::string` + `raw_string_ostream`
10. **IRArgument 不是 IRValue 子类**：序列化时函数参数通过 `IRFunction::getArg(i)` 获取，参数名通过 `getArg(i)->getName()`，类型通过 `getArg(i)->getType()`。反序列化时参数由 `IRFunction` 构造函数自动创建
11. **writeText 必须产生合法的 BTIR text**：输出的文本必须能被 `parseText` 正确解析回来（往返一致性）
12. **writeBitcode 必须产生合法的 BTIR binary**：输出的二进制必须能被 `parseBitcode` 正确解析回来（往返一致性）

---

## 验收标准

### V1：文本格式写入

```cpp
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("test", TCtx, "x86_64-unknown-linux-gnu");

auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
auto* F = M.getOrInsertFunction("add", FTy);
auto* Entry = F->addBasicBlock("entry");

IRBuilder Builder(Ctx);
Builder.setInsertPoint(Entry);
auto* Sum = Builder.createAdd(
    /*LHS=*/ Builder.getInt32(1),
    /*RHS=*/ Builder.getInt32(2), "sum");
Builder.createRet(Sum);

std::string Text;
raw_string_ostream OS(Text);
bool WriteOK = IRWriter::writeText(M, OS);
assert(WriteOK && "writeText should succeed");
assert(!Text.empty() && "Output should not be empty");
// 输出应包含 "module" 和 "function" 关键字
assert(Text.find("module") != std::string::npos);
assert(Text.find("function") != std::string::npos);
```

### V2：文本格式往返（round-trip）

```cpp
// 构建同 V1 的模块，写入文本再解析回来
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("roundtrip", TCtx, "x86_64-unknown-linux-gnu");

auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
auto* F = M.getOrInsertFunction("add", FTy);
auto* Entry = F->addBasicBlock("entry");

IRBuilder Builder(Ctx);
Builder.setInsertPoint(Entry);
auto* Sum = Builder.createAdd(Builder.getInt32(1), Builder.getInt32(2), "sum");
Builder.createRet(Sum);

// 写入
std::string Text;
raw_string_ostream OS(Text);
IRWriter::writeText(M, OS);

// 解析回来
SerializationDiagnostic Diag;
auto Parsed = IRReader::parseText(StringRef(Text), TCtx, &Diag);
assert(Parsed != nullptr && "parseText should succeed after writeText");
assert(Parsed->getName() == M.getName());
assert(Parsed->getNumFunctions() == M.getNumFunctions());
assert(Parsed->getFunction("add") != nullptr);

// 验证解析后的模块结构
auto* PF = Parsed->getFunction("add");
assert(PF != nullptr);
assert(PF->isDefinition());
assert(PF->getNumBasicBlocks() == 1);
```

### V3：二进制格式往返

```cpp
// 构建同 V1 的模块，写入二进制再解析回来
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("bin_roundtrip", TCtx, "x86_64-unknown-linux-gnu");

auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
auto* F = M.getOrInsertFunction("add", FTy);
auto* Entry = F->addBasicBlock("entry");

IRBuilder Builder(Ctx);
Builder.setInsertPoint(Entry);
auto* Sum = Builder.createAdd(Builder.getInt32(1), Builder.getInt32(2), "sum");
Builder.createRet(Sum);

// 写入二进制
std::string Binary;
raw_string_ostream BOS(Binary);
bool WriteOK = IRWriter::writeBitcode(M, BOS);
assert(WriteOK);

// 验证魔数
assert(Binary.size() >= 4);
assert(Binary[0] == 'B' && Binary[1] == 'T' && Binary[2] == 'I' && Binary[3] == 'R');

// 解析回来
SerializationDiagnostic Diag;
auto Parsed = IRReader::parseBitcode(
    StringRef(Binary.data(), Binary.size()), TCtx, &Diag);
assert(Parsed != nullptr && "parseBitcode should succeed");
assert(Parsed->getName() == M.getName());
assert(Parsed->getNumFunctions() == M.getNumFunctions());
```

### V4：错误处理 — 无效文本格式

```cpp
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();

SerializationDiagnostic Diag;
auto M = IRReader::parseText("this is not valid BTIR", TCtx, &Diag);
assert(M == nullptr && "Invalid text should fail to parse");
assert(Diag.Kind == SerializationErrorKind::InvalidFormat);
assert(Diag.Line > 0 && "Should report line number");
assert(!Diag.Message.empty());
```

### V5：错误处理 — 无效二进制魔数

```cpp
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();

const char* BadData = "XXXX";  // 错误的魔数
SerializationDiagnostic Diag;
auto M = IRReader::parseBitcode(StringRef(BadData, 4), TCtx, &Diag);
assert(M == nullptr && "Invalid magic should fail");
assert(Diag.Kind == SerializationErrorKind::InvalidFormat);
```

### V6：readFile 自动格式检测

```cpp
// 先将文本格式写入临时文件
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("file_test", TCtx);

auto* FTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
auto* F = M.getOrInsertFunction("dummy", FTy);
auto* Entry = F->addBasicBlock("entry");
IRBuilder Builder(Ctx);
Builder.setInsertPoint(Entry);
Builder.createRetVoid();

std::string Text;
raw_string_ostream OS(Text);
IRWriter::writeText(M, OS);

// 写入临时文件（文件内容不含 BTIR 魔数 → 自动识别为文本格式）
// 这里验证 readFile 的自动检测逻辑即可
// 实际测试中需要创建临时文件
// SerializationDiagnostic Diag;
// auto Loaded = IRReader::readFile("/tmp/test.btir", TCtx, &Diag);
// assert(Loaded != nullptr);
// assert(Loaded->getName() == "file_test");
```

### V7：错误处理 — 版本不兼容

```cpp
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();

// 构造一个 Major 版本不匹配的二进制数据
// 前 4 字节: "BTIR", 后面版本号为 99.0.0
char BadVersion[26] = {};
BadVersion[0] = 'B'; BadVersion[1] = 'T'; BadVersion[2] = 'I'; BadVersion[3] = 'R';
// Major = 99 (little-endian)
BadVersion[4] = 99; BadVersion[5] = 0;
// Minor = 0
BadVersion[6] = 0; BadVersion[7] = 0;
// Patch = 0
BadVersion[8] = 0; BadVersion[9] = 0;

SerializationDiagnostic Diag;
auto M = IRReader::parseBitcode(StringRef(BadVersion, 26), TCtx, &Diag);
assert(M == nullptr && "Version mismatch should fail");
assert(Diag.Kind == SerializationErrorKind::VersionMismatch);
```

### V8：含全局变量的模块往返

```cpp
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("globals_test", TCtx, "x86_64-unknown-linux-gnu");

auto* GV = M.getOrInsertGlobal("g_counter", TCtx.getInt32Ty());
GV->setInitializer(/* 需要通过 IRContext 创建 */ nullptr);  // 无初始化器

// 写入文本
std::string Text;
raw_string_ostream OS(Text);
IRWriter::writeText(M, OS);

// 解析回来
auto Parsed = IRReader::parseText(StringRef(Text), TCtx);
assert(Parsed != nullptr);
assert(Parsed->getGlobalVariable("g_counter") != nullptr);
```

---

> **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.7 — IRSerializer（文本 + 二进制格式）" && git push origin HEAD
> ```
