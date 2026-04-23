# BlockType 测试基线报告

**生成时间**: 2026-04-23 (第二轮更新)  
**生成人**: tester（测试人员）  
**项目版本**: 0.1.0  
**项目阶段**: Phase 0-6 并行开发中  
**构建类型**: Release  
**测试运行时间**: 5.03 秒

---

## 1. 测试基础设施概览

### 1.1 测试框架

| 框架 | 用途 | 状态 |
|------|------|------|
| Google Test (gtest) | 单元测试 + 集成测试 | ✅ 已集成 |
| CTest | 测试运行器 | ✅ 已集成 |
| Lit + FileCheck | 端到端测试 | ⚠️ 部分配置 |
| 自定义基准测试 | 性能回归检测 | ✅ 已集成 |
| lcov/genhtml | 代码覆盖率 | ✅ 已集成 |

### 1.2 构建与测试命令

```bash
# 构建
./scripts/build.sh [Release|Debug]

# 运行测试
./scripts/test.sh [build-release|build-debug]

# 性能回归检测
python3 scripts/check_performance.py [--verbose]

# Phase 验证
python3 scripts/phase_validator.py --phase <N> --task <T>

# 覆盖率报告（需 ENABLE_COVERAGE=ON）
cmake -B build -DENABLE_COVERAGE=ON && cmake --build build
cd build && ctest && make coverage
```

---

## 2. 测试用例清单

### 2.1 单元测试 (tests/unit/)

| 模块 | 文件数 | 测试文件 | CMake 集成 |
|------|--------|----------|------------|
| **Basic** | 4 | SourceLocationTest, DiagnosticsTest, FixItHintTest, UTF8ValidatorTest | ✅ |
| **Lex** | 10 | LexerTest, TokenTest, PreprocessorTest, LexerFixTest, LexerExtensionTest, BoundaryCaseTest, HighPriorityFixesTest, MediumPriorityFixesTest | ✅ |
| **AST** | 5 | ASTTest, Stage71Test, Stage72Test, Stage73Test | ✅ |
| **Parse** | 6 | ParserTest, DeclarationTest, StatementTest, AccessControlTest, ErrorRecoveryTest | ✅ |
| **Sema** | 13 | SemaTest, TypeCheckTest, NameLookupTest, OverloadResolutionTest, TemplateDeductionTest, TemplateInstantiationTest, VariadicTemplateTest, ConceptTest, SFINAETest, ConstantExprTest, SymbolTableTest, AccessControlTest | ✅ |
| **CodeGen** | 9 | CodeGenFunctionTest, CodeGenExprTest, CodeGenStmtTest, CodeGenTypesTest, CodeGenClassTest, CodeGenConstantTest, CodeGenDebugInfoTest, ManglerTest | ✅ |
| **Module** | 3 | ModuleManagerTest, ModuleLinkerTest, ModuleVisibilityTest | ✅ |
| **Frontend** | 1 | InfrastructureSharingTest | ✅ |
| **AI** | 3 | ResponseCacheTest, CostTrackerTest, AIOrchestratorTest | ✅ |
| **Performance** | 1 | PerformanceTest | ✅ |

**单元测试总计**: 约 54 个测试文件

### 2.2 集成测试 (tests/integration/)

| 模块 | 文件数 | 测试文件 | CMake 集成 |
|------|--------|----------|------------|
| **AI** | 1 | ProviderIntegrationTest | ✅ |

**集成测试总计**: 1 个测试文件

### 2.3 Lit 端到端测试 (tests/lit/)

| 模块 | 文件数 | 测试文件 |
|------|--------|----------|
| **lex** | 4 | chinese-keywords, keywords, literals, operators |
| **parser** | 9 | basic, class, concept, declarations, enum, errors, namespace, template, using |
| **CodeGen** | 12 | arithmetic, basic-types, class-layout, control-flow, cpp23-features, cpp26-contracts, cpp26-reflection, debug-info, function-call, inheritance, virtual-call, virtual-inheritance |

**Lit 测试总计**: 25 个 .test 文件

### 2.4 基准测试 (tests/benchmark/)

| 测试 | 文件 | CMake 集成 |
|------|------|------------|
| Lexer 基准 | LexerBenchmark.cpp | ✅ |
| Parser 基准 | ParserBenchmark.cpp | ✅ |

### 2.5 未纳入构建的测试

| 目录 | 文件数 | 说明 |
|------|--------|------|
| tests/cpp26/ | 34 | C++26 特性测试，未纳入 CMake |
| tests/ 根目录散落文件 | ~30 | 调试/临时测试文件，未纳入 CMake |

---

## 3. 已知测试失败

### 3.1 当前测试结果 (2026-04-23 第二轮)

**总测试数**: 766  
**通过**: 760 (99.2%)  
**失败**: 6 (0.8%)  
**测试运行时间**: 5.03 秒

#### 失败测试详情

| # | 测试用例 | 状态 | 根因分析 | 严重程度 |
|---|----------|------|----------|----------|
| 1 | DeclarationTest.TemplateTypeAlias | ❌ 失败 | 解析器无法识别模板别名声明 `template<typename T> using Vec = std::vector<T>;`，报错 "no template named 'Vec'"，返回 nullptr | P2-中等 |
| 2 | ConceptTest.EvaluateRequiresExprWithExprRequirement | ❌ 失败 | `EvaluateRequiresExpr` 对 ExprRequirement 返回 false，期望 true。requires 表达式中的表达式需求评估逻辑不完整 | P2-中等 |
| 3 | ManglerTest.VTableName | ❌ 失败 | VTable 名字修饰格式错误：实际 `_ZTVN3FooE`，期望 `_ZTV3Foo`。对非嵌套类型错误使用了嵌套名修饰 | P1-严重 |
| 4 | ManglerTest.VTableNameLonger | ❌ 失败 | 同上：实际 `_ZTVN7MyClassE`，期望 `_ZTV7MyClass` | P1-严重 |
| 5 | ManglerTest.RTTIName | ❌ 失败 | RTTI 名字修饰格式错误：实际 `_ZTIN3FooE`，期望 `_ZTI3Foo`。同 VTable 问题 | P1-严重 |
| 6 | ManglerTest.TypeinfoName | ❌ 失败 | Typeinfo 名字修饰格式错误：实际 `_ZTSN3FooE`，期望 `_ZTS3Foo`。同 VTable 问题 | P1-严重 |

#### 失败根因归类

1. **Mangler 嵌套名修饰 Bug（4个测试）**: `getVTableName`/`getRTTIName`/`getTypeinfoName` 对非嵌套类型（如顶层类 `Foo`、`MyClass`）错误地使用了嵌套名格式（`N...E`）。根据 Itanium C++ ABI，只有真正在命名空间内的类型才应使用嵌套名格式。当前代码无条件添加 `N` 前缀和 `E` 后缀，需要根据类型的嵌套层级决定是否使用嵌套名格式。**这是同一 Bug 的4个表现，修复 Mangler 即可解决。**

2. **模板别名解析缺失（1个测试）**: 解析器尚不支持 `template<typename T> using Vec = ...` 语法，将模板别名声明解析失败。这是功能缺失而非回归。

3. **requires 表达式需求评估不完整（1个测试）**: `ConstraintChecker::EvaluateRequiresExpr` 对 `ExprRequirement` 的评估逻辑返回 false，可能是表达式需求的有效性判断未实现。

### 3.2 历史失败测试修复确认

| 测试用例 | 之前状态 | 当前状态 | 说明 |
|----------|----------|----------|------|
| ParserTest.TemplateSpecializationWithBuiltinType | ❌ 失败 | ✅ 已修复 | 模板特化表达式解析已修复 |
| LexerFixTest.HexLiteralWithDigitSeparator | ❌ 失败 | ✅ 已修复 | 十六进制数字分隔符词法分析已修复 |

### 3.3 回归分析结论

**未检测到回归问题**。6个失败测试均为已知功能缺失/缺陷，非近期修改引入的回归：
- Mangler 4个失败是 Itanium ABI 嵌套名修饰的既有 Bug
- TemplateTypeAlias 是模板别名解析功能缺失
- EvaluateRequiresExprWithExprRequirement 是约束检查器表达式需求评估不完整

### 3.4 各模块测试通过率

| 模块 | 测试数 | 通过 | 失败 | 通过率 |
|------|--------|------|------|--------|
| Basic (SourceLocation/Diagnostics/FixItHint/UTF8) | 43 | 43 | 0 | 100% |
| Lex (Lexer/Token/Preprocessor/Fix/Extension/Boundary/HighPriority/MediumPriority) | 160 | 160 | 0 | 100% |
| Parse (Parser/Declaration/Statement/AccessControl/ErrorRecovery) | 193 | 192 | 1 | 99.5% |
| Sema (Sema/TypeCheck/NameLookup/Overload/TemplateDeduction/TemplateInstantiation/VariadicTemplate/Concept/SFINAE/ConstantExpr/SymbolTable/AccessControl/Contract) | 155 | 154 | 1 | 99.4% |
| CodeGen (Function/Expr/Stmt/Types/Class/Constant/DebugInfo/Mangler/Attribute) | 48 | 44 | 4 | 91.7% |
| Module (Manager/Linker/Visibility) | 4 | 4 | 0 | 100% |
| Frontend (InfrastructureSharing) | 1 | 1 | 0 | 100% |
| AI (ResponseCache/CostTracker/AIOrchestrator/ProviderIntegration) | 37 | 37 | 0 | 100% |
| Performance | 14 | 14 | 0 | 100% |
| **合计** | **766** | **760** | **6** | **99.2%** |

### 3.5 关键功能类别验证

| 类别 | 测试数 | 通过 | 失败 | 状态 |
|------|--------|------|------|------|
| Contracts 相关 | 9 | 9 | 0 | ✅ 全通过 |
| 模板实例化 | 12 | 12 | 0 | ✅ 全通过 |
| 约束/概念 | 18 | 17 | 1 | ⚠️ ExprRequirement 评估失败 |
| 类型推导 (DeducingThis) | 3 | 3 | 0 | ✅ 全通过 |
| 赋值表达式 | 12 | 12 | 0 | ✅ 全通过 |
| Sema 拆分后功能 | 19 | 19 | 0 | ✅ 全通过 |
| 属性系统 | 1 | 1 | 0 | ✅ 全通过 |
| 重载解析 | 10 | 10 | 0 | ✅ 全通过 |
| SFINAE | 10 | 10 | 0 | ✅ 全通过 |
| 模板推导 | 9 | 9 | 0 | ✅ 全通过 |

### 3.6 旧版测试结果（存档）

#### test_result.xml (2026-04-16)

| 测试用例 | 状态 | 详情 |
|----------|------|------|
| ParserTest.TemplateSpecializationWithBuiltinType | ❌ 失败 | `llvm::isa<TemplateSpecializationExpr>(E)` 返回 false，期望 true |

#### test_results.xml (2026-04-21)

| 测试用例 | 状态 | 详情 |
|----------|------|------|
| LexerFixTest.HexLiteralWithDigitSeparator | ❌ 失败 | `Diags.hasErrorOccurred()` 返回 true，期望 false |

---

## 4. 测试覆盖分析

### 4.1 模块覆盖矩阵

| 编译器阶段 | 单元测试 | 集成测试 | Lit 测试 | 覆盖评估 |
|------------|----------|----------|----------|----------|
| Basic (基础设施) | ✅ 4个 | - | - | 🟡 基本 |
| Lex (词法分析) | ✅ 10个 | - | ✅ 4个 | 🟢 较好 |
| Parse (语法分析) | ✅ 6个 | - | ✅ 9个 | 🟢 较好 |
| AST (抽象语法树) | ✅ 5个 | - | - | 🟡 基本 |
| Sema (语义分析) | ✅ 13个 | - | - | 🟢 较好 |
| CodeGen (代码生成) | ✅ 9个 | - | ✅ 12个 | 🟢 较好 |
| Module (模块系统) | ✅ 3个 | - | - | 🟡 基本 |
| Frontend (前端) | ✅ 1个 | - | - | 🔴 不足 |
| AI (AI 集成) | ✅ 3个 | ✅ 1个 | - | 🟡 基本 |
| Driver (驱动器) | - | - | - | 🔴 缺失 |

### 4.2 覆盖缺口

1. **Frontend 模块**: 仅 1 个测试文件（InfrastructureSharingTest），缺少编译流程端到端测试
2. **Driver 模块**: 无专门测试，编译主流程未被测试覆盖
3. **双语支持**: 仅 lit/lex/chinese-keywords.test 覆盖中文关键字，缺少中文标识符、中文诊断消息等测试
4. **错误恢复**: Parse/ErrorRecoveryTest 存在但覆盖面未知
5. **C++26 特性**: 34 个测试文件未纳入构建，无法验证
6. **端到端流程**: 缺少从源代码到 LLVM IR 完整编译流程的集成测试

---

## 5. 测试基础设施问题

| 问题 | 严重程度 | 说明 |
|------|----------|------|
| 根目录散落调试文件 | 中 | ~30 个未纳入构建的测试文件，可能是临时调试产物 |
| cpp26 测试未集成 | 中 | 34 个 C++26 特性测试未纳入 CMake 构建 |
| Lit 测试框架未完全配置 | 中 | ROADMAP 标记为未完成，需 lit 工具安装 |
| 无 CI/CD | 高 | 无自动化持续集成，测试需手动运行 |
| 测试结果文件过时 | 低 | test_result.xml 和 test_results.xml 非最新 |
| 集成测试不足 | 高 | 仅 1 个集成测试文件 |

---

## 6. 测试辅助工具

| 工具 | 路径 | 用途 |
|------|------|------|
| TestHelpers.h | tests/TestHelpers.h | 临时文件创建、ParseHelper、BlockTypeTest 基类、字符串断言 |
| phase_validator.py | scripts/phase_validator.py | Phase 完成验证（支持 Task 2.1, 2.3） |
| check_performance.py | scripts/check_performance.py | 性能回归检测（Lexer 吞吐量、缓存加速比） |
| verify_parser_completeness.py | scripts/verify_parser_completeness.py | Parser 函数完整性验证 |

---

## 7. 测试标准与规范

### 7.1 测试用例编写规范

1. **单元测试**: 使用 gtest 框架，每个模块一个目录，文件名以 `*Test.cpp` 结尾
2. **Lit 测试**: 使用 `// RUN:` 和 `// CHECK:` 指令，文件名以 `.test` 结尾
3. **测试夹具**: 继承 `BlockTypeTest` 基类或自定义 `::testing::Test` 子类
4. **辅助工具**: 使用 `TestHelpers.h` 中的 `createTempFile`、`ParseHelper` 等

### 7.2 Bug 报告模板

```markdown
## Bug 报告

**严重程度**: [P0-阻塞/P1-严重/P2-中等/P3-轻微]

### 复现步骤
1. ...

### 预期结果
...

### 实际结果
...

### 环境信息
- 操作系统: 
- 构建类型: 
- LLVM 版本: 

### 相关代码
- 文件: 
- 行号: 
```

---

## 8. 下一步行动

1. ✅ 运行完整测试套件获取最新基线（已完成 2026-04-23）
2. ⚠️ 修复 Mangler 嵌套名修饰 Bug（4个失败测试，P1 优先级）
3. ⚠️ 实现 requires 表达式 ExprRequirement 评估逻辑（1个失败测试）
4. ⚠️ 实现模板别名声明解析（1个失败测试）
5. 🔲 补充覆盖缺口测试（Frontend/Driver 模块）
6. 🔲 将 cpp26 测试纳入 CMake 构建
