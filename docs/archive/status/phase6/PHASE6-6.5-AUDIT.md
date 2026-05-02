# Stage 6.5 审计报告 — 调试信息 + 测试

**审计日期：** 2026-04-19  
**审计范围：** `CGDebugInfo.h/cpp`、`CodeGenModule` DebugInfo 集成、单元测试(7文件)、lit 测试(8文件)  
**对比基准：** `docs/plan/06-PHASE6-irgen.md` Stage 6.5 章节 + Clang `CGDebugInfo`

---

## A. 对比开发文档 (06-PHASE6-irgen.md) — Stage 6.5 部分

## -----------------------------------------------------------

## Task 6.5.1 调试信息生成

文档要求 vs 实际实现：

### E6.5.1.1 CGDebugInfo 类

**文档定义的接口：**

| 接口 | 文档要求 | 实际实现 | 状态 |
|------|---------|---------|------|
| `CGDebugInfo(CodeGenModule &M)` | ✅ 定义 | ✅ 已实现 | OK |
| `~CGDebugInfo()` | ✅ 定义 | ✅ `= default` | OK |
| `Initialize(StringRef, StringRef)` | ✅ 创建编译单元 | ✅ 创建 DIBuilder + DIFile + DICompileUnit | OK |
| `Finalize()` | ✅ 完成 | ✅ `DIB->finalize()` | OK |
| `GetDIType(QualType)` | ✅ 类型分发 | ✅ 完整 switch 分发 | OK |
| `GetBuiltinDIType(BuiltinType*)` | ✅ 内建类型 | ✅ 覆盖全部 22 种 BuiltinKind | OK |
| `GetRecordDIType(RecordType*)` | ✅ 记录类型 | ✅ 含前向声明+递归引用处理 | OK |
| `GetEnumDIType(EnumType*)` | ✅ 枚举类型 | ✅ 含枚举常量和底层类型 | OK |
| `GetFunctionDI(FunctionDecl*)` | ✅ 函数调试 | ✅ 含成员函数作用域 | OK |
| `EmitLocalVarDI(VarDecl*, AllocaInst*)` | ✅ 局部变量 | ✅ 接口已实现 | OK |
| `setLocation(SourceLocation)` | ✅ 行号信息 | ✅ 已实现（生成DILocation供EmitStmt使用） | OK |

### 文档未要求但已额外实现的接口：

| 接口 | 功能 | 状态 |
|------|------|------|
| `GetPointerDIType(PointerType*)` | 指针类型 DWARF | ✅ OK |
| `GetReferenceDIType(ReferenceType*)` | 引用类型 DWARF | ✅ OK |
| `GetArrayDIType(ArrayType*)` | 数组类型 DWARF | ✅ OK |
| `GetFunctionDIType(FunctionType*)` | 函数子程序类型 | ✅ OK |
| `EmitGlobalVarDI(VarDecl*, GlobalVariable*)` | 全局变量调试信息 | ✅ OK |
| `EmitParamDI(ParmVarDecl*, AllocaInst*, unsigned)` | 参数调试信息 | ✅ OK |
| `setFunctionLocation(Function*, FunctionDecl*)` | 函数 DISubprogram 设置 | ✅ OK |
| `CreateLexicalBlock(SourceLocation)` | 词法作用域 | ✅ OK |
| `getCurrentScope()` | 当前作用域查询 | ✅ OK |
| `getSourceLocation(SourceLocation)` | SourceLocation → DILocation | ✅ OK |
| `isInitialized()` / `getCompileUnit()` / `getFile()` | 访问器 | ✅ OK |
| `RecordDIcache` + `CurrentFnSP` | 递归引用和作用域跟踪 | ✅ OK |

### 类型调试信息覆盖度：

| TypeClass | GetDIType 分支 | 实际处理 | 状态 |
|-----------|---------------|---------|------|
| Builtin | ✅ | 22 种 BuiltinKind 全部覆盖 | OK |
| Pointer | ✅ | void* 特殊处理 | OK |
| LValueReference / RValueReference | ✅ | DW_TAG_reference_type | OK |
| ConstantArray / IncompleteArray / VariableArray | ✅ | 区分定长/不定长 | OK |
| Record | ✅ | 前向声明 + 基类继承 + vptr + 字段 | OK |
| Enum | ✅ | 底层类型 + 枚举常量 | OK |
| Function | ✅ | 返回类型 + 参数列表 | OK |
| Typedef | ✅ | createTypedef | OK |
| Elaborated | ✅ | 递归到命名类型 | OK |
| **TemplateSpecialization** | ❌ 未处理 | default break → 返回 nullptr | P2 |
| **MemberPointer** | ❌ 未处理 | default break → 返回 nullptr | P2 |
| **Auto / Decltype / Dependent / Unresolved / TemplateTypeParm** | ❌ 未处理 | 不应出现在 CodeGen | OK (设计如此) |

### CodeGenModule 集成：

| 集成点 | 位置 | 状态 |
|--------|------|------|
| 构造函数初始化 `DebugInfo` | `CodeGenModule` 构造函数第 48 行 | ✅ OK |
| `EmitTranslationUnit` 调用 `Initialize` | 第 66 行 | ✅ OK |
| `EmitTranslationUnit` 调用 `Finalize` | 第 107 行 | ✅ OK |
| `EmitGlobalVar` 调用 `EmitGlobalVarDI` | 第 160-162 行 | ✅ OK |
| `EmitFunction` 调用 `setFunctionLocation` | 第 206-208 行 | ✅ OK |

### CodeGenFunction 级别集成 — ⚠️ 存在重大遗漏：

| 集成点 | 期望位置 | 实际状态 | 优先级 |
|--------|---------|---------|--------|
| `EmitLocalVarDI` 在 `EmitDeclStmt` 中调用 | `CodeGenStmt.cpp` EmitDeclStmt | ✅ **已集成** (第627行) | OK |
| `EmitParamDI` 在 `EmitFunctionBody` 中调用 | `CodeGenFunction.cpp` EmitFunctionBody | ✅ **已集成** (第108行) | OK |
| `setLocation` 在每条 IR 指令前调用 | `CodeGenFunction` EmitStmt | ✅ **已集成** (第277-285行) | OK |
| `CreateLexicalBlock` 在 `CompoundStmt` 中调用 | `CodeGenStmt.cpp` EmitCompoundStmt | ❌ **未集成** | P2 |

**影响：** 当前生成的 IR 中，局部变量和函数参数**没有** `llvm.dbg.declare` intrinsic，IR 指令**没有** `!dbg` 元数据附件。GDB/LLDB 无法通过调试器查看局部变量或按源码行步进。

## -----------------------------------------------------------

## Task 6.5.2 IRGen 测试

### E6.5.2.1 单元测试文件

文档要求 8 个文件，实际创建 7 个：

| 文件 | 文档要求 | 实际创建 | 测试用例数 | 状态 |
|------|---------|---------|-----------|------|
| `CodeGenModuleTest.cpp` | ✅ | ❌ **缺失** | 0 | P2 |
| `CodeGenTypesTest.cpp` | ✅ | ✅ | 11 | OK |
| `CodeGenConstantTest.cpp` | ✅ | ✅ | 5 | OK |
| `CodeGenExprTest.cpp` | ✅ | ✅ | 2 | ⚠️ 偏少 |
| `CodeGenStmtTest.cpp` | ✅ | ✅ | 3 | OK |
| `CodeGenFunctionTest.cpp` | ✅ | ✅ | 3 | OK |
| `CodeGenClassTest.cpp` | ✅ | ✅ | 6 | OK |
| `CodeGenDebugInfoTest.cpp` | ✅ | ✅ | 6 | OK |

**总计：36 个测试用例，全部通过 ✅**

### 测试覆盖度分析（对照文档覆盖重点）：

| 模块 | 文档要求的测试用例 | 实际覆盖 | 缺失 |
|------|-------------------|---------|------|
| CodeGenTypes | 22 种内建类型、指针、引用、数组、函数、结构体、枚举 | 11 个：7 内建 + 指针 + 记录 + CXX记录 + 函数 | ⚠️ 缺引用、数组、枚举、typedef |
| CodeGenConstant | 整数/浮点/字符串/布尔、零值、空指针 | 5 个：整数 + 浮点 + 布尔 + 零值 + 空指针 | ⚠️ 缺字符串字面量、字符字面量 |
| CodeGenExpr | 算术、比较、逻辑、赋值、调用、成员访问、类型转换 | 2 个：整数/浮点字面量表达式 | ❌ **严重不足** — 缺算术/比较/逻辑/赋值/调用/成员访问/类型转换 |
| CodeGenStmt | if/else、switch、for/while/do、break/continue/return | 3 个：Compound + If + Return | ⚠️ 缺 switch/for/while/do/break/continue |
| CodeGenFunction | 参数传递、返回值、局部变量、递归 | 3 个：void/返回值/参数 | ⚠️ 缺局部变量、递归 |
| CGCXX | 类布局、构造/析构、VTable、虚调用、继承 | 6 个：布局×2 + 继承 + VTable + 构造 + 析构 | ⚠️ 缺虚调用 |
| CGDebugInfo | 类型DI、函数DI、行号 | 6 个：初始化 + 内建 + 指针 + 记录 + 函数 + 缓存 | ⚠️ 缺引用/数组/枚举DI、局部变量DI、参数DI、行号 |

### E6.5.2.2 Lit 回归测试

| 文件 | 文档要求 | 实际创建 | 状态 |
|------|---------|---------|------|
| `basic-types.test` | ✅ | ✅ | OK |
| `arithmetic.test` | ✅ | ✅ | OK |
| `function-call.test` | ✅ | ✅ | OK |
| `control-flow.test` | ✅ | ✅ | OK |
| `class-layout.test` | ✅ | ✅ | OK |
| `virtual-call.test` | ✅ | ✅ | OK |
| `inheritance.test` | ✅ | ✅ | OK |
| `debug-info.test` | ✅ | ✅ | OK |

**注意：** Lit 测试依赖 `blocktype` 命令行工具，需编译器驱动程序完整后才能运行。当前 lit 测试文件存在但无法执行验证。

## -----------------------------------------------------------
## B. 对比 Clang 同模块特性
## -----------------------------------------------------------

## CGDebugInfo 对比 Clang CGDebugInfo

### 1. 编译单元管理

| 特性 | Clang | BlockType | 差距 |
|------|-------|-----------|------|
| DICompileUnit 创建 | 从 SourceManager 获取文件名 | 硬编码 "input.cpp" | P2 |
| 语言标准传播 | 根据 LangOptions 选择 DW_LANG | 固定 DW_LANG_C_plus_plus | P2 |
| 生产者字符串 | "Clang version X.Y.Z" | "BlockType Compiler" | OK |
| isOptimized 传播 | 从 CodeGenOptions 获取 | 固定 false | P2 |
| 命令行参数存储 | 存入 DICompileUnit | 空字符串 | P2 |

### 2. 源位置映射

| 特性 | Clang | BlockType | 差距 |
|------|-------|-----------|------|
| SourceManager 集成 | 完整的行/列/文件映射 | ✅ 已集成 SourceManager.getLineAndColumn() | OK |
| DILocation 附加 | 每条 IR 指令都有 !dbg | ✅ 在 EmitStmt 中自动设置 | OK |
| 自动位置传播 | Builder.SetCurrentDebugLocation | ✅ 已在 EmitStmt 中实现 | OK |
| inlinedAt 链 | 支持内联调用栈 | 不支持 | P2 |

### 3. 类型调试信息

| 特性 | Clang | BlockType | 差距 |
|------|-------|-----------|------|
| BuiltinType DI | 完整 | ✅ 完整 | OK |
| PointerType DI | 含地址空间 | 无地址空间（固定 0） | OK (当前阶段) |
| ReferenceType DI | 区分左值/右值引用 | 都生成 DW_TAG_reference_type | P2 |
| ArrayType DI | 含多维数组 | 仅一维 | P2 |
| RecordType DI | 完整（含动态类、虚继承） | 基本完整（单继承+虚函数） | OK (当前阶段) |
| EnumType DI | 支持固定底层类型和 C++11 enum class | 基本实现 | OK |
| FunctionType DI | 含 this 调整和 calling convention | 基本 | OK |
| Typedef DI | 支持 | ✅ 支持 | OK |
| TemplateSpecialization DI | 完整支持 | ❌ 未处理 | P2 |
| CVR 限定符 DI | const/volatile 限定符生成 | 忽略 | P2 |
| BitField DI | 支持 | ❌ 未处理 | P2 |

### 4. 函数/变量调试信息

| 特性 | Clang | BlockType | 差距 |
|------|-------|-----------|------|
| DISubprogram 创建 | 完整 | ✅ 完整 | OK |
| 成员函数作用域 | 指向类 DIType | ✅ 指向类 DIType | OK |
| 函数参数 DI | `llvm.dbg.declare` for each param | ✅ 已在 EmitFunctionBody 中集成 | OK |
| 局部变量 DI | `llvm.dbg.declare` for each local | ✅ 已在 EmitDeclStmt 中集成 | OK |
| 全局变量 DI | `DIGlobalVariableExpression` | ✅ 已集成到 EmitGlobalVar | OK |
| Static 成员 DI | 特殊处理 | 未处理 | P2 |
| 前向声明处理 | `createReplaceableCompositeType` | ✅ 使用相同模式 | OK |
| 前向声明完成后的缓存更新 | `RecordDIcache` 指向旧 FwdDecl | ✅ 已更新为 CompleteTy | OK |

### 5. 作用域管理

| 特性 | Clang | BlockType | 差距 |
|------|-------|-----------|------|
| 词法块 DI | 完整支持 | 接口存在但未在 CompoundStmt 中调用 | P2 |
| 命名空间 DI | 完整支持 | ❌ 未实现 | P2 |
| 作用域栈 | LexicalBlockStack 管理 | 仅 CurrentFnSP | P2 |
| 函数退出时清理 | 重置 CurrentFnSP | ✅ 已调用 clearCurrentFnSP | OK |

## -----------------------------------------------------------
## C. 关联关系错误
## -----------------------------------------------------------

### 1. ⚠️ GetEnumDIType 大小重复乘 8

```cpp
// CGDebugInfo.cpp:339-343
uint64_t EnumSize = Underlying.isNull() ? 32
                    : CGM.getTarget().getTypeSize(Underlying) * 8;  // ← 已经 ×8
return DIB->createEnumerationType(CU, ..., EnumSize * 8, ...);  // ← 又 ×8 = ×64
```

**问题：** `EnumSize` 已经是比特单位，但 `createEnumerationType` 又乘以 8，导致枚举大小为正确值的 8 倍。

**优先级：** P1 — 生成的 DWARF 枚举类型大小错误，调试器显示异常。

### 2. ⚠️ GetRecordDIType 前向声明缓存不更新

```cpp
// CGDebugInfo.cpp:244-245
RecordDIcache[RD] = FwdDecl;  // 缓存前向声明
// ...
// CGDebugInfo.cpp:316
DIB->replaceTemporary(llvm::TempMDNode(FwdDecl), CompleteTy);
// 但 RecordDIcache[RD] 仍指向前向声明，后续查询返回过时指针
```

**问题：** `replaceTemporary` 将前向声明替换为完整类型，但 `RecordDIcache` 仍保存着旧的前向声明指针。后续对同一 `RecordDecl` 的查询会返回已过时的 `FwdDecl`。

**优先级：** P1 — LLVM 的 `replaceTemporary` 会更新 RAUW，但语义上应更新缓存。

### 3. ⚠️ CurrentFnSP 在函数退出时从不重置

```cpp
// GetFunctionDI 设置:
CurrentFnSP = SP;
// 但没有任何地方清除 CurrentFnSP
```

**问题：** 当函数 A 生成完毕后开始生成函数 B，如果 B 的 `GetFunctionDI` 未被调用（例如 B 是构造/析构函数通过 CGCXX::EmitConstructor/EmitDestructor 生成），则 B 中的局部变量调试信息会错误地使用 A 的 DISubprogram 作为作用域。

**优先级：** P1 — 作用域泄漏。

### 4. ⚠️ createBasicBlock 修改影响范围

```cpp
// CodeGenFunction.cpp:274-276 (已修复)
llvm::BasicBlock *CodeGenFunction::createBasicBlock(llvm::StringRef Name) {
  return llvm::BasicBlock::Create(CGM.getLLVMContext(), Name);  // 不再插入 CurFn
}
```

**注意：** 此修复是 Stage 6.5 开发中做出的。`EmitFunctionBody` 中直接创建的 entry 块仍使用 `llvm::BasicBlock::Create(Ctx, "entry", Function)` 插入函数，这是正确的。但需要确保 `createBasicBlock` 的修改不影响其他路径。

**优先级：** OK — 已修复。

### 5. ⚠️ GetBuiltinDIType 的 Size 变量先赋值后覆盖

```cpp
// CGDebugInfo.cpp:126-179
switch (BT->getKind()) {
case BuiltinKind::Bool: Size = 8; break;     // ← Size 先赋值
// ...
case BuiltinKind::NullPtr: Size = PtrSize * 8; return ...; // ← NullPtr 直接返回
default: Size = 32; break;
}
QualType QT(...);
Size = CGM.getTarget().getTypeSize(QT) * 8;   // ← Size 被覆盖
```

**问题：** switch 中的 Size 赋值无意义，因为最后总被 `getTypeSize(QT) * 8` 覆盖。虽然结果正确（`getTypeSize` 返回字节数，×8 转比特），但代码逻辑混乱，容易误导。

**优先级：** P2 — 代码质量问题，结果正确。

### 6. ⚠️ setLocation 方法体为空

```cpp
void CGDebugInfo::setLocation(SourceLocation Loc) {
  if (!Initialized || !Loc.isValid()) return;
  // ← 空方法体，没有实际操作
}
```

**问题：** `setLocation` 应在 IRBuilder 上设置 `CurrentDebugLocation`，使后续生成的每条 IR 指令自动附带 `!dbg` 元数据。当前实现导致所有 IR 指令无调试位置信息。

**优先级：** P1 — GDB/LLDB 无法按源码行步进。

## -----------------------------------------------------------
## D. 汇总
## -----------------------------------------------------------

## P0 问题（必须立即修复）— 1 个

1. ✅ **SourceLocation → 行/列映射不准确**：已修复。
   - 在 CodeGenModule 中添加 SourceManager 引用
   - 修改 getLineNumber/getColumnNumber 使用 SourceManager.getLineAndColumn()
   - 添加 SourceManager 头文件包含
   - **影响：** 现在行号信息准确，调试器可以正确定位源码。

## P1 问题（应尽快修复）— 6 个

1. ✅ **setLocation 方法体为空**：已修复。
   - 在 EmitStmt 中自动为每条语句设置调试位置
   - 调用 getSourceLocation 获取 DILocation
   - 通过 Builder.SetCurrentDebugLocation 设置到 IRBuilder
   - **影响：** IR 指令现在有 !dbg 附件，调试器可以步进。

2. ✅ **EmitLocalVarDI 未在 EmitDeclStmt 中调用**：已修复（之前已存在）。
   - 在 CodeGenStmt.cpp 的 EmitDeclStmt 第627行调用
   - **影响：** 局部变量有 llvm.dbg.declare。

3. ✅ **EmitParamDI 未在 EmitFunctionBody 中调用**：已修复（之前已存在）。
   - 在 CodeGenFunction.cpp 的 EmitFunctionBody 第108行调用
   - **影响：** 函数参数有 llvm.dbg.declare。

4. ✅ **GetEnumDIType 大小重复乘 8**：已修复。
   - EnumSize 已经是比特单位，移除了多余的 * 8
   - **影响：** 枚举类型大小正确。

5. ✅ **CurrentFnSP 在函数退出时从不重置**：已修复（之前已存在）。
   - 在 EmitFunctionBody 结束时调用 clearCurrentFnSP
   - **影响：** 作用域不会泄漏。

6. ✅ **GetRecordDIType 前向声明缓存不更新**：已修复（之前已存在）。
   - 在 replaceTemporary 后更新 RecordDIcache[RD] = CompleteTy
   - **影响：** 缓存指向完整类型。

## P2 问题（后续改进）— 12 个

1. `CodeGenModuleTest.cpp` 缺失（文档要求 8 个测试文件，实际只有 7 个）
2. `CodeGenExprTest.cpp` 测试用例严重不足（仅 2 个字面量测试，缺算术/比较/逻辑/赋值/调用/成员访问/类型转换）
3. `CodeGenStmtTest.cpp` 缺 switch/for/while/do/break/continue 测试
4. `CodeGenTypesTest.cpp` 缺引用/数组/枚举/typedef 测试
5. 左值引用和右值引用的 DWARF 类型未区分（都生成 `DW_TAG_reference_type`）
6. TemplateSpecializationType / MemberPointerType 的 DI 未处理
7. 命名空间 DI 未实现
8. 词法块 DI 未在 CompoundStmt 中集成
9. 编译单元信息硬编码（文件名 "input.cpp"、语言标准固定、isOptimized 固定）
10. CVR 限定符的调试信息未生成
11. `GetBuiltinDIType` 中 Size 变量先赋值后覆盖，代码逻辑混乱
12. BitField 调试信息未处理

## 测试通过率

| 测试套件 | 用例数 | 通过 | 失败 | 通过率 |
|----------|--------|------|------|--------|
| CodeGenTypesTest | 11 | 11 | 0 | 100% |
| CodeGenConstantTest | 5 | 5 | 0 | 100% |
| CodeGenExprTest | 2 | 2 | 0 | 100% |
| CodeGenStmtTest | 3 | 3 | 0 | 100% |
| CodeGenFunctionTest | 3 | 3 | 0 | 100% |
| CodeGenClassTest | 6 | 6 | 0 | 100% |
| CodeGenDebugInfoTest | 6 | 6 | 0 | 100% |
| **总计** | **36** | **36** | **0** | **100%** |

**说明：** 36 个测试全部通过，但覆盖度未达到文档要求的 ≥ 80%（当前估计约 55%）。主要缺失在于表达式和控制流的测试覆盖不足。
