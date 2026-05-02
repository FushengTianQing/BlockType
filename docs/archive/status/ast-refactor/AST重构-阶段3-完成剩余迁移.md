# AST 完整重构 - 阶段 3：完成剩余 Context.create 迁移

> **状态**: 方案设计  
> **前置依赖**: 阶段 2 已完成（172→70 处，减少 59%，662/662 测试通过）  
> **当前测试基线**: 662/662 全部通过  
> **目标**: Context.create 调用数从 70 降至 0，实现完美架构  
> **风险等级**: 中（剩余调用多数为错误恢复和模板节点，逻辑相对独立）

---

## 一、目标

**完美架构目标**：Parser 中零 `Context.create<>` 调用，所有 AST 节点创建统一通过 `Sema::ActOn*()` 方法完成。

```
阶段2后（当前）:  Parser 直接创建 70 处节点 → Sema::ProcessAST 后处理补类型 → CodeGen 纯消费
阶段3后（目标）:  Parser 仅解析语法 → Sema::ActOn* 创建所有节点+设类型 → CodeGen 纯消费
```

### 核心收益

1. **架构一致性**：Parser 不再直接操作 ASTContext，完全通过 Sema 间接创建节点
2. **类型安全完整性**：所有表达式在创建时就拥有正确类型，ProcessAST 可最终移除
3. **消除灰色地带**：不再存在"Parser 创建+Sema 后处理"的半完成模式
4. **语义前置全覆盖**：类型检查、名称查找在节点创建时同步完成

---

## 二、现状分析

### 2.1 阶段 2 成果回顾

| 指标 | 阶段 2 前 | 阶段 2 后 | 变化 |
|------|----------|----------|------|
| Context.create 调用数 | 172 | 70 | -59% |
| ActOn* 方法数 | 36 | 61 | +25 新增 |
| 测试通过率 | 662/662 | 662/662 | 100% |
| 新增 Sema include | 0 | 7 文件 | 全覆盖 |

### 2.2 剩余 70 处 Context.create 调用详细分布

| 文件 | 调用数 | 主要类别 |
|------|--------|---------|
| `ParseStmt.cpp` | 24 | NullStmt 错误恢复(20), DoStmt 恢复(2), VarDecl for-range(2) |
| `ParseStmtCXX.cpp` | 6 | NullStmt 恢复(5), VarDecl catch(1) |
| `ParseTemplate.cpp` | 27 | TemplateDecl(10), 模板包装(7), 模板参数(5), 特化(4), ConceptDecl(1) |
| `ParseClass.cpp` | 10 | CXXRecordDecl(3), 方法(3), FriendDecl(2), RecordDecl(1), FunctionDecl(1) |
| `ParseExprCXX.cpp` | 1 | NullStmt lambda 恢复(1) |
| `ParseDecl.cpp` | 1 | EnumConstantDecl(1) |
| `Parser.cpp` | 1 | TranslationUnitDecl(1) |
| **总计** | **70** | |

### 2.3 剩余调用按类别分析

#### 类别 A：NullStmt 错误恢复（26 处）

所有均为 `Context.create<NullStmt>(Loc)` 作为语法错误后的恢复占位符。

| 文件 | 行号 | 场景 |
|------|------|------|
| `ParseStmt.cpp` | 282 | label 后语句解析失败 |
| `ParseStmt.cpp` | 311 | case 后语句解析失败 |
| `ParseStmt.cpp` | 332 | default 后语句解析失败 |
| `ParseStmt.cpp` | 381 | goto 语句错误恢复 |
| `ParseStmt.cpp` | 422 | if 条件解析失败（缺 `(`） |
| `ParseStmt.cpp` | 439 | if then 分支解析失败 |
| `ParseStmt.cpp` | 448 | if else 分支解析失败 |
| `ParseStmt.cpp` | 467 | switch 条件解析失败（缺 `(`） |
| `ParseStmt.cpp` | 483 | switch body 解析失败 |
| `ParseStmt.cpp` | 500 | while 条件解析失败（缺 `(`） |
| `ParseStmt.cpp` | 516 | while body 解析失败 |
| `ParseStmt.cpp` | 533 | do body 解析失败 |
| `ParseStmt.cpp` | 574 | for 左括号缺失 |
| `ParseStmt.cpp` | 639 | for-range 冒号缺失 |
| `ParseStmt.cpp` | 655 | for-range body 解析失败 |
| `ParseStmt.cpp` | 707 | for body 解析失败（传统） |
| `ParseStmt.cpp` | 723 | CXX for-range 左括号缺失 |
| `ParseStmt.cpp` | 746 | CXX for-range 标识符缺失 |
| `ParseStmt.cpp` | 758 | CXX for-range 冒号缺失 |
| `ParseStmt.cpp` | 774 | CXX for-range body 解析失败 |
| `ParseStmtCXX.cpp` | 31 | try 块解析失败（缺 `{`） |
| `ParseStmtCXX.cpp` | 36 | try 块解析恢复 |
| `ParseStmtCXX.cpp` | 62 | catch 块解析失败（缺 `(`） |
| `ParseStmtCXX.cpp` | 97 | catch 块解析失败（缺 `{`） |
| `ParseStmtCXX.cpp` | 102 | catch 块解析恢复 |
| `ParseExprCXX.cpp` | 252 | lambda body 解析失败 |

**迁移策略**：直接替换为 `Actions.ActOnNullStmt(Loc).get()`（方法已存在）

#### 类别 B：DoStmt 错误恢复（2 处）

| 文件 | 行号 | 代码 |
|------|------|------|
| `ParseStmt.cpp` | 539 | `Context.create<DoStmt>(DoLoc, Body, nullptr)` — while 缺失 |
| `ParseStmt.cpp` | 545 | `Context.create<DoStmt>(DoLoc, Body, nullptr)` — 左括号缺失 |

**迁移策略**：替换为 `Actions.ActOnDoStmt(nullptr, Body, DoLoc).get()`（方法已存在，已支持 Cond=nullptr）

#### 类别 C：VarDecl for-range/catch（3 处）

| 文件 | 行号 | 代码 | 场景 |
|------|------|------|------|
| `ParseStmt.cpp` | 659 | `Context.create<VarDecl>(VarLoc, VarName, VarType, nullptr)` | for-range 循环变量 |
| `ParseStmt.cpp` | 778 | `Context.create<VarDecl>(VarLoc, VarName, VarType, nullptr)` | for-range 循环变量 |
| `ParseStmtCXX.cpp` | 85 | `Context.create<VarDecl>(VarLoc, VarName, ExceptionType, nullptr)` | catch 异常声明 |

**迁移策略**：替换为 `llvm::cast<VarDecl>(Actions.ActOnVarDecl(Loc, Name, Type, nullptr).get())`（方法已存在）

#### 类别 D：TranslationUnitDecl（1 处）

| 文件 | 行号 | 当前代码 |
|------|------|---------|
| `Parser.cpp` | 39 | `Context.create<TranslationUnitDecl>(StartLoc)` + `Actions.ActOnTranslationUnit(TU)` |

**迁移策略**：新增 `ActOnTranslationUnitDecl` 工厂方法，合并创建+注册

#### 类别 E：CXXRecordDecl class/struct/union（3 处）

| 文件 | 行号 | TagKind | 当前模式 |
|------|------|---------|---------|
| `ParseClass.cpp` | 75 | TK_class | `Context.create<CXXRecordDecl>` + `Actions.ActOnCXXRecordDecl(Class)` |
| `ParseClass.cpp` | 153 | TK_struct | 同上 |
| `ParseClass.cpp` | 204 | TK_union | 同上 |

**迁移策略**：新增 `ActOnCXXRecordDeclFactory` 工厂方法，合并创建+注册

#### 类别 F：CXXMethodDecl（1 处）

| 文件 | 行号 | 当前模式 |
|------|------|---------|
| `ParseClass.cpp` | 613 | `Context.create<CXXMethodDecl>(17 个参数)` + `Actions.ActOnCXXMethodDecl(Method)` |

**迁移策略**：新增 `ActOnCXXMethodDeclFactory` 工厂方法，参数传递给 Context.create

#### 类别 G：CXXConstructorDecl / CXXDestructorDecl（2 处）

| 文件 | 行号 | 节点类型 |
|------|------|---------|
| `ParseClass.cpp` | 809 | CXXConstructorDecl + ActOnCXXConstructorDecl |
| `ParseClass.cpp` | 855 | CXXDestructorDecl + ActOnCXXDestructorDecl |

**迁移策略**：新增 `ActOnCXXConstructorDeclFactory` 和 `ActOnCXXDestructorDeclFactory`

#### 类别 H：FriendDecl 相关（4 处）

| 文件 | 行号 | 节点类型 | 场景 |
|------|------|---------|------|
| `ParseClass.cpp` | 1029 | RecordDecl | friend 类型前向声明 |
| `ParseClass.cpp` | 1034 | FriendDecl | friend 类型声明 |
| `ParseClass.cpp` | 1106 | FunctionDecl | friend 函数 |
| `ParseClass.cpp` | 1109 | FriendDecl | friend 函数声明 |

**迁移策略**：新增 `ActOnFriendTypeDecl` 和 `ActOnFriendFunctionDecl` 组合方法

#### 类别 I：EnumConstantDecl（1 处）

| 文件 | 行号 | 当前模式 |
|------|------|---------|
| `ParseDecl.cpp` | 1089 | `Context.create<EnumConstantDecl>` + `Actions.ActOnEnumConstant(Constant)` |

**迁移策略**：新增 `ActOnEnumConstantDeclFactory` 工厂方法，合并创建+求值+注册

#### 类别 J：模板相关节点（27 处）

**J1: TemplateDecl 通用包装（10 处）**

| 文件 | 行号 | 场景 |
|------|------|------|
| `ParseTemplate.cpp` | 58 | 显式实例化 |
| `ParseTemplate.cpp` | 100 | class 特化包装 |
| `ParseTemplate.cpp` | 102 | class 非特化包装 |
| `ParseTemplate.cpp` | 124 | var 特化包装 |
| `ParseTemplate.cpp` | 126 | var 非特化包装 |
| `ParseTemplate.cpp` | 141 | func 非特化包装 |
| `ParseTemplate.cpp` | 145 | fallback 包装 |
| `ParseTemplate.cpp` | 276 | 其他类型 fallback |
| `ParseTemplate.cpp` | 646 | 模板模板参数默认值 |
| `ParseTemplate.cpp` | 907 | concept 模板包装 |

**J2: ClassTemplateDecl（2 处）**

| 文件 | 行号 | 场景 |
|------|------|------|
| `ParseTemplate.cpp` | 232 | class 偏特化包装 |
| `ParseTemplate.cpp` | 238 | 常规 class 模板 |

**J3: FunctionTemplateDecl（2 处）**

| 文件 | 行号 | 场景 |
|------|------|------|
| `ParseTemplate.cpp` | 138 | func 特化包装 |
| `ParseTemplate.cpp` | 201 | 常规 func 模板 |

**J4: VarTemplateDecl（2 处）**

| 文件 | 行号 | 场景 |
|------|------|------|
| `ParseTemplate.cpp` | 264 | var 偏特化包装 |
| `ParseTemplate.cpp` | 270 | 常规 var 模板 |

**J5: TypeAliasTemplateDecl（1 处）**

| 文件 | 行号 |
|------|------|
| `ParseTemplate.cpp` | 273 |

**J6: 模板特化节点（4 处）**

| 文件 | 行号 | 节点类型 |
|------|------|---------|
| `ParseTemplate.cpp` | 95 | ClassTemplateSpecializationDecl |
| `ParseTemplate.cpp` | 119 | VarTemplateSpecializationDecl |
| `ParseTemplate.cpp` | 221 | ClassTemplatePartialSpecializationDecl |
| `ParseTemplate.cpp` | 254 | VarTemplatePartialSpecializationDecl |

**J7: 模板参数节点（5 处）**

| 文件 | 行号 | 节点类型 |
|------|------|---------|
| `ParseTemplate.cpp` | 356 | TemplateTypeParmDecl（约束参数） |
| `ParseTemplate.cpp` | 405 | TemplateTypeParmDecl（约束参数带参数） |
| `ParseTemplate.cpp` | 456 | TemplateTypeParmDecl（常规） |
| `ParseTemplate.cpp` | 521 | NonTypeTemplateParmDecl |
| `ParseTemplate.cpp` | 609 | TemplateTemplateParmDecl |

**J8: ConceptDecl（1 处）**

| 文件 | 行号 | 当前模式 |
|------|------|---------|
| `ParseTemplate.cpp` | 913 | `Context.create<ConceptDecl>` + `Actions.ActOnConceptDecl(Concept)` |

**迁移策略**：新增模板专用 ActOn* 方法组，将模板包装逻辑集中到 Sema

---

## 三、分阶段执行计划

按风险从低到高排序为 5 个子阶段，每个子阶段完成后编译+全量测试验证。

### 子阶段 3A：NullStmt 错误恢复批量替换（26 处）

**风险等级**：极低  
**改动范围**：3 个文件，26 处简单文本替换  
**新增 ActOn* 方法**：0（`ActOnNullStmt` 已存在）

**修改文件清单**：

#### 3A-1: `src/Parse/ParseStmt.cpp`（22 处）

所有 `Context.create<NullStmt>(XXXLoc)` 替换为 `Actions.ActOnNullStmt(XXXLoc).get()`

具体替换列表（按行号）：

| 行号 | 原代码 | 新代码 |
|------|--------|--------|
| 282 | `Context.create<NullStmt>(LabelLoc)` | `Actions.ActOnNullStmt(LabelLoc).get()` |
| 311 | `Context.create<NullStmt>(CaseLoc)` | `Actions.ActOnNullStmt(CaseLoc).get()` |
| 332 | `Context.create<NullStmt>(DefaultLoc)` | `Actions.ActOnNullStmt(DefaultLoc).get()` |
| 381 | `Context.create<NullStmt>(GotoLoc)` | `Actions.ActOnNullStmt(GotoLoc).get()` |
| 422 | `Context.create<NullStmt>(IfLoc)` | `Actions.ActOnNullStmt(IfLoc).get()` |
| 439 | `Context.create<NullStmt>(IfLoc)` | `Actions.ActOnNullStmt(IfLoc).get()` |
| 448 | `Context.create<NullStmt>(IfLoc)` | `Actions.ActOnNullStmt(IfLoc).get()` |
| 467 | `Context.create<NullStmt>(SwitchLoc)` | `Actions.ActOnNullStmt(SwitchLoc).get()` |
| 483 | `Context.create<NullStmt>(SwitchLoc)` | `Actions.ActOnNullStmt(SwitchLoc).get()` |
| 500 | `Context.create<NullStmt>(WhileLoc)` | `Actions.ActOnNullStmt(WhileLoc).get()` |
| 516 | `Context.create<NullStmt>(WhileLoc)` | `Actions.ActOnNullStmt(WhileLoc).get()` |
| 533 | `Context.create<NullStmt>(DoLoc)` | `Actions.ActOnNullStmt(DoLoc).get()` |
| 574 | `Context.create<NullStmt>(ForLoc)` | `Actions.ActOnNullStmt(ForLoc).get()` |
| 639 | `Context.create<NullStmt>(ForLoc)` | `Actions.ActOnNullStmt(ForLoc).get()` |
| 655 | `Context.create<NullStmt>(ForLoc)` | `Actions.ActOnNullStmt(ForLoc).get()` |
| 707 | `Context.create<NullStmt>(ForLoc)` | `Actions.ActOnNullStmt(ForLoc).get()` |
| 723 | `Context.create<NullStmt>(ForLoc)` | `Actions.ActOnNullStmt(ForLoc).get()` |
| 746 | `Context.create<NullStmt>(ForLoc)` | `Actions.ActOnNullStmt(ForLoc).get()` |
| 758 | `Context.create<NullStmt>(ForLoc)` | `Actions.ActOnNullStmt(ForLoc).get()` |
| 774 | `Context.create<NullStmt>(ForLoc)` | `Actions.ActOnNullStmt(ForLoc).get()` |

> 注：以上为初步行号，实际执行时以当前代码为准。共约 20 处 NullStmt + 2 处 DoStmt 错误恢复。

#### 3A-2: `src/Parse/ParseStmtCXX.cpp`（5 处）

| 行号 | 原代码 | 新代码 |
|------|--------|--------|
| 31 | `Context.create<NullStmt>(TryLoc)` | `Actions.ActOnNullStmt(TryLoc).get()` |
| 36 | `Context.create<NullStmt>(TryLoc)` | `Actions.ActOnNullStmt(TryLoc).get()` |
| 62 | `Context.create<NullStmt>(CatchLoc)` | `Actions.ActOnNullStmt(CatchLoc).get()` |
| 97 | `Context.create<NullStmt>(CatchLoc)` | `Actions.ActOnNullStmt(CatchLoc).get()` |
| 102 | `Context.create<NullStmt>(CatchLoc)` | `Actions.ActOnNullStmt(CatchLoc).get()` |

#### 3A-3: `src/Parse/ParseExprCXX.cpp`（1 处）

| 行号 | 原代码 | 新代码 |
|------|--------|--------|
| 252 | `Context.create<NullStmt>(Tok.getLocation())` | `Actions.ActOnNullStmt(Tok.getLocation()).get()` |

**验证点**：编译通过 + 662 测试全部通过

---

### 子阶段 3B：DoStmt/VarDecl/EnumConstantDecl/TranslationUnitDecl 简单改造（7 处）

**风险等级**：低  
**改动范围**：5 个文件，7 处修改  
**新增 ActOn* 方法**：4 个

#### 3B-1: DoStmt 错误恢复（2 处）— `src/Parse/ParseStmt.cpp`

| 行号 | 原代码 | 新代码 |
|------|--------|--------|
| 539 | `Context.create<DoStmt>(DoLoc, Body, nullptr)` | `Actions.ActOnDoStmt(nullptr, Body, DoLoc).get()` |
| 545 | `Context.create<DoStmt>(DoLoc, Body, nullptr)` | `Actions.ActOnDoStmt(nullptr, Body, DoLoc).get()` |

无需新增方法（`ActOnDoStmt` 已存在且已支持 null Cond）。

#### 3B-2: VarDecl for-range（2 处）— `src/Parse/ParseStmt.cpp`

| 行号 | 原代码 | 新代码 |
|------|--------|--------|
| 659 | `VarDecl *RangeVar = Context.create<VarDecl>(VarLoc, VarName, VarType, nullptr)` | `VarDecl *RangeVar = llvm::cast<VarDecl>(Actions.ActOnVarDecl(VarLoc, VarName, VarType, nullptr).get())` |
| 778 | `VarDecl *RangeVar = Context.create<VarDecl>(VarLoc, VarName, VarType, nullptr)` | `VarDecl *RangeVar = llvm::cast<VarDecl>(Actions.ActOnVarDecl(VarLoc, VarName, VarType, nullptr).get())` |

无需新增方法（`ActOnVarDecl` 已存在）。

#### 3B-3: VarDecl catch（1 处）— `src/Parse/ParseStmtCXX.cpp`

| 行号 | 原代码 | 新代码 |
|------|--------|--------|
| 85 | `ExceptionDecl = Context.create<VarDecl>(VarLoc, VarName, ExceptionType, nullptr)` | `ExceptionDecl = llvm::cast<VarDecl>(Actions.ActOnVarDecl(VarLoc, VarName, ExceptionType, nullptr).get())` |

#### 3B-4: EnumConstantDecl（1 处）— `src/Parse/ParseDecl.cpp`

**新增方法**：`DeclResult ActOnEnumConstantDeclFactory(SourceLocation Loc, llvm::StringRef Name, QualType EnumType, Expr *Init)`

将当前的"Context.create + ActOnEnumConstant"两步合并为一步：

```cpp
// 原代码：
EnumConstantDecl *Constant = Context.create<EnumConstantDecl>(Loc, Name, QualType(), InitVal);
Actions.ActOnEnumConstant(Constant);

// 新代码：
EnumConstantDecl *Constant = llvm::cast<EnumConstantDecl>(
    Actions.ActOnEnumConstantDeclFactory(Loc, Name, QualType(), InitVal).get());
```

Sema 内部实现：
```cpp
DeclResult Sema::ActOnEnumConstantDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                               QualType EnumType, Expr *Init) {
  auto *ECD = Context.create<EnumConstantDecl>(Loc, Name, EnumType, Init);
  // 复用已有的求值逻辑
  return ActOnEnumConstant(ECD);
}
```

#### 3B-5: TranslationUnitDecl（1 处）— `src/Parse/Parser.cpp`

**新增方法**：`TranslationUnitDecl *ActOnTranslationUnitDecl(SourceLocation Loc)`

将当前的"Context.create + ActOnTranslationUnit"两步合并为一步：

```cpp
// 原代码：
TranslationUnitDecl *TU = Context.create<TranslationUnitDecl>(StartLoc);
Actions.ActOnTranslationUnit(TU);

// 新代码：
TranslationUnitDecl *TU = Actions.ActOnTranslationUnitDecl(StartLoc);
```

Sema 内部实现：
```cpp
TranslationUnitDecl *Sema::ActOnTranslationUnitDecl(SourceLocation Loc) {
  auto *TU = Context.create<TranslationUnitDecl>(Loc);
  ActOnTranslationUnit(TU);  // 复用已有注册逻辑
  return TU;
}
```

**验证点**：编译通过 + 662 测试全部通过

---

### 子阶段 3C：Class 声明节点工厂化（10 处）

**风险等级**：中  
**改动范围**：1 个文件（ParseClass.cpp），10 处修改  
**新增 ActOn* 方法**：6 个

#### 3C-1: CXXRecordDecl class/struct/union（3 处）

**新增方法**：
```cpp
DeclResult ActOnCXXRecordDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                     TagDecl::TagKind Kind);
```

Sema 内部实现：
```cpp
DeclResult Sema::ActOnCXXRecordDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                            TagDecl::TagKind Kind) {
  auto *RD = Context.create<CXXRecordDecl>(Loc, Name, Kind);
  ActOnCXXRecordDecl(RD);  // 注册到符号表和上下文
  return DeclResult(RD);
}
```

Parser 侧替换：
```cpp
// 原代码：
CXXRecordDecl *Class = Context.create<CXXRecordDecl>(NameLoc, Name, TagDecl::TK_class);
Actions.ActOnCXXRecordDecl(Class);

// 新代码：
CXXRecordDecl *Class = llvm::cast<CXXRecordDecl>(
    Actions.ActOnCXXRecordDeclFactory(NameLoc, Name, TagDecl::TK_class).get());
```

3 处分别对应 TK_class(行75)、TK_struct(行153)、TK_union(行204)。

#### 3C-2: CXXMethodDecl（1 处）

**新增方法**：
```cpp
DeclResult ActOnCXXMethodDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                     QualType Type,
                                     llvm::ArrayRef<ParmVarDecl *> Params,
                                     CXXRecordDecl *Class, Stmt *Body,
                                     bool IsStatic, bool IsConst, bool IsVolatile,
                                     bool IsVirtual, bool IsPureVirtual,
                                     bool IsOverride, bool IsFinal,
                                     bool IsDefaulted, bool IsDeleted,
                                     RefQualifierKind RefQual,
                                     bool HasNoexceptSpec, bool NoexceptValue,
                                     Expr *NoexceptExpr,
                                     AccessSpecifier Access);
```

Sema 内部实现：
```cpp
DeclResult Sema::ActOnCXXMethodDeclFactory(...) {
  auto *MD = Context.create<CXXMethodDecl>(Loc, Name, Type, Params, Class, Body,
      IsStatic, IsConst, IsVolatile, IsVirtual, IsPureVirtual,
      IsOverride, IsFinal, IsDefaulted, IsDeleted,
      RefQual, HasNoexceptSpec, NoexceptValue, NoexceptExpr, Access);
  ActOnCXXMethodDecl(MD);
  return DeclResult(MD);
}
```

#### 3C-3: CXXConstructorDecl（1 处）

**新增方法**：
```cpp
DeclResult ActOnCXXConstructorDeclFactory(SourceLocation Loc,
                                          CXXRecordDecl *Class,
                                          llvm::ArrayRef<ParmVarDecl *> Params,
                                          Stmt *Body, bool IsExplicit);
```

#### 3C-4: CXXDestructorDecl（1 处）

**新增方法**：
```cpp
DeclResult ActOnCXXDestructorDeclFactory(SourceLocation Loc,
                                         CXXRecordDecl *Class, Stmt *Body);
```

#### 3C-5: FriendDecl 相关（4 处）

**新增方法 1**：`DeclResult ActOnFriendTypeDecl(SourceLocation FriendLoc, QualType FriendType)`

合并 ParseClass.cpp 行 1034 的 FriendDecl 创建：
```cpp
// 原代码（注意：ActOnFriendDecl 被重复调用了两次，这是一个 bug）：
FriendDecl *FD = Context.create<FriendDecl>(FriendLoc, nullptr, FriendType, true);
Actions.ActOnFriendDecl(FD);
Actions.ActOnFriendDecl(FD);  // BUG: 重复调用，迁移时删除

// 新代码：
FriendDecl *FD = llvm::cast<FriendDecl>(
    Actions.ActOnFriendTypeDecl(FriendLoc, FriendType).get());
```

**新增方法 2**：`DeclResult ActOnFriendFunctionDecl(SourceLocation FriendLoc, FunctionDecl *FriendFunc)`

合并 ParseClass.cpp 行 1106-1109 的 FunctionDecl + FriendDecl 创建：
```cpp
// 原代码：
FunctionDecl *FriendFunc = Context.create<FunctionDecl>(NameLoc, Name, Type, Params, nullptr);
return Context.create<FriendDecl>(FriendLoc, FriendFunc, QualType(), false);

// 新代码：
return llvm::cast<FriendDecl>(
    Actions.ActOnFriendFunctionDecl(FriendLoc, NameLoc, Name, Type, Params).get());
```

其中 `ActOnFriendFunctionDecl` 内部创建 FunctionDecl 和 FriendDecl：
```cpp
DeclResult Sema::ActOnFriendFunctionDecl(SourceLocation FriendLoc,
                                         SourceLocation NameLoc,
                                         llvm::StringRef Name, QualType Type,
                                         llvm::ArrayRef<ParmVarDecl *> Params) {
  auto *FriendFunc = Context.create<FunctionDecl>(NameLoc, Name, Type, Params, nullptr);
  auto *FD = Context.create<FriendDecl>(FriendLoc, FriendFunc, QualType(), false);
  ActOnFriendDecl(FD);
  return DeclResult(FD);
}
```

**处理 RecordDecl 前向声明**（行 1029）：此 RecordDecl 是为了获取 QualType 而创建的前向声明，属于 Sema 的类型查找职责。将其纳入 `ActOnFriendTypeDecl` 内部处理：Parser 传入类型名而非 QualType，Sema 内部完成查找或创建前向声明。

```cpp
DeclResult ActOnFriendTypeDecl(SourceLocation FriendLoc, llvm::StringRef TypeName,
                               SourceLocation TypeNameLoc);
```

Sema 内部：
```cpp
DeclResult Sema::ActOnFriendTypeDecl(SourceLocation FriendLoc,
                                     llvm::StringRef TypeName,
                                     SourceLocation TypeNameLoc) {
  QualType FriendType;
  // 1. 尝试在当前作用域查找类型
  if (CurrentScope) {
    if (NamedDecl *Found = CurrentScope->lookup(TypeName)) {
      if (auto *TD = llvm::dyn_cast<TypeDecl>(Found)) {
        FriendType = Context.getTypeDeclType(TD);
      }
    }
  }
  // 2. 未找到则创建前向声明
  if (FriendType.isNull()) {
    auto *ForwardDecl = Context.create<RecordDecl>(TypeNameLoc, TypeName, TagDecl::TK_struct);
    FriendType = Context.getTypeDeclType(ForwardDecl);
  }
  auto *FD = Context.create<FriendDecl>(FriendLoc, nullptr, FriendType, true);
  ActOnFriendDecl(FD);
  return DeclResult(FD);
}
```

**验证点**：编译通过 + 662 测试全部通过

---

### 子阶段 3D：模板参数节点（5 处）

**风险等级**：中  
**改动范围**：1 个文件（ParseTemplate.cpp），5 处修改  
**新增 ActOn* 方法**：3 个

#### 3D-1: TemplateTypeParmDecl（3 处）

**新增方法**：
```cpp
DeclResult ActOnTemplateTypeParmDecl(SourceLocation Loc, llvm::StringRef Name,
                                     unsigned Depth, unsigned Index,
                                     bool IsParameterPack, bool IsTypename);
```

3 处替换（行 356、405、456）全部使用此方法。注意行 356 和 405 的 `IsTypename` 传入 `false`，行 456 传入实际值。

```cpp
// 原代码：
TemplateTypeParmDecl *Param = Context.create<TemplateTypeParmDecl>(
    NameLoc, Name, 0, 0, IsParameterPack, false);

// 新代码：
TemplateTypeParmDecl *Param = llvm::cast<TemplateTypeParmDecl>(
    Actions.ActOnTemplateTypeParmDecl(NameLoc, Name, 0, 0, IsParameterPack, false).get());
```

Sema 内部实现：
```cpp
DeclResult Sema::ActOnTemplateTypeParmDecl(SourceLocation Loc, llvm::StringRef Name,
                                           unsigned Depth, unsigned Index,
                                           bool IsParameterPack, bool IsTypename) {
  auto *Param = Context.create<TemplateTypeParmDecl>(Loc, Name, Depth, Index,
                                                      IsParameterPack, IsTypename);
  if (CurrentScope) Symbols.addDecl(Param);
  return DeclResult(Param);
}
```

#### 3D-2: NonTypeTemplateParmDecl（1 处）

**新增方法**：
```cpp
DeclResult ActOnNonTypeTemplateParmDecl(SourceLocation Loc, llvm::StringRef Name,
                                        QualType Type, unsigned Depth,
                                        unsigned Index, bool IsParameterPack);
```

```cpp
// 原代码（行 521）：
NonTypeTemplateParmDecl *Param = Context.create<NonTypeTemplateParmDecl>(
    NameLoc, Name, Type, 0, 0, IsParameterPack);

// 新代码：
NonTypeTemplateParmDecl *Param = llvm::cast<NonTypeTemplateParmDecl>(
    Actions.ActOnNonTypeTemplateParmDecl(NameLoc, Name, Type, 0, 0, IsParameterPack).get());
```

#### 3D-3: TemplateTemplateParmDecl（1 处）

**新增方法**：
```cpp
DeclResult ActOnTemplateTemplateParmDecl(SourceLocation Loc, llvm::StringRef Name,
                                         unsigned Depth, unsigned Index,
                                         bool IsParameterPack);
```

```cpp
// 原代码（行 609）：
TemplateTemplateParmDecl *Param = Context.create<TemplateTemplateParmDecl>(
    NameLoc, Name, 0, 0, IsParameterPack);

// 新代码：
TemplateTemplateParmDecl *Param = llvm::cast<TemplateTemplateParmDecl>(
    Actions.ActOnTemplateTemplateParmDecl(NameLoc, Name, 0, 0, IsParameterPack).get());
```

**验证点**：编译通过 + 662 测试全部通过

---

### 子阶段 3E：模板包装节点和特化节点（22 处）

**风险等级**：中高（涉及模板特化逻辑，代码较复杂）  
**改动范围**：1 个文件（ParseTemplate.cpp），22 处修改  
**新增 ActOn* 方法**：10 个

#### 3E-1: TemplateDecl 通用包装（10 处）

**新增方法**：
```cpp
DeclResult ActOnTemplateDecl(SourceLocation Loc, llvm::StringRef Name,
                             Decl *TemplatedDecl);
```

所有 10 处 `Context.create<TemplateDecl>(...)` 替换为：
```cpp
TemplateDecl *Template = llvm::cast<TemplateDecl>(
    Actions.ActOnTemplateDecl(TemplateLoc, Name, TemplatedDecl).get());
```

Sema 内部：
```cpp
DeclResult Sema::ActOnTemplateDecl(SourceLocation Loc, llvm::StringRef Name,
                                   Decl *TemplatedDecl) {
  auto *TD = Context.create<TemplateDecl>(Loc, Name, TemplatedDecl);
  if (CurrentScope && !Name.empty()) Symbols.addDecl(TD);
  return DeclResult(TD);
}
```

#### 3E-2: ClassTemplateDecl（2 处）

**新增方法**：
```cpp
DeclResult ActOnClassTemplateDecl(SourceLocation Loc, llvm::StringRef Name,
                                  Decl *TemplatedDecl);
```

#### 3E-3: FunctionTemplateDecl（2 处）

**新增方法**：
```cpp
DeclResult ActOnFunctionTemplateDecl(SourceLocation Loc, llvm::StringRef Name,
                                     Decl *TemplatedDecl);
```

#### 3E-4: VarTemplateDecl（2 处）

**新增方法**：
```cpp
DeclResult ActOnVarTemplateDecl(SourceLocation Loc, llvm::StringRef Name,
                                Decl *TemplatedDecl);
```

#### 3E-5: TypeAliasTemplateDecl（1 处）

**新增方法**：
```cpp
DeclResult ActOnTypeAliasTemplateDecl(SourceLocation Loc, llvm::StringRef Name,
                                      Decl *TemplatedDecl);
```

#### 3E-6: ClassTemplateSpecializationDecl（1 处）

**新增方法**：
```cpp
DeclResult ActOnClassTemplateSpecializationDecl(SourceLocation Loc,
                                                llvm::StringRef Name,
                                                ClassTemplateDecl *PrimaryTemplate,
                                                llvm::ArrayRef<TemplateArgument> Args,
                                                bool IsExplicit);
```

#### 3E-7: VarTemplateSpecializationDecl（1 处）

**新增方法**：
```cpp
DeclResult ActOnVarTemplateSpecializationDecl(SourceLocation Loc,
                                              llvm::StringRef Name, QualType Type,
                                              VarTemplateDecl *PrimaryTemplate,
                                              llvm::ArrayRef<TemplateArgument> Args,
                                              Expr *Init, bool IsExplicit);
```

#### 3E-8: ClassTemplatePartialSpecializationDecl（1 处）

**新增方法**：
```cpp
DeclResult ActOnClassTemplatePartialSpecDecl(SourceLocation Loc,
                                             llvm::StringRef Name,
                                             ClassTemplateDecl *PrimaryTemplate,
                                             llvm::ArrayRef<TemplateArgument> Args);
```

#### 3E-9: VarTemplatePartialSpecializationDecl（1 处）

**新增方法**：
```cpp
DeclResult ActOnVarTemplatePartialSpecDecl(SourceLocation Loc,
                                           llvm::StringRef Name, QualType Type,
                                           VarTemplateDecl *PrimaryTemplate,
                                           llvm::ArrayRef<TemplateArgument> Args,
                                           Expr *Init);
```

#### 3E-10: ConceptDecl（1 处）

**新增方法**：
```cpp
DeclResult ActOnConceptDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                   Expr *Constraint, TemplateDecl *Template);
```

```cpp
// 原代码（行 913）：
ConceptDecl *Concept = Context.create<ConceptDecl>(ConceptNameLoc, ConceptName, Constraint, Template);
Actions.ActOnConceptDecl(Concept);

// 新代码：
ConceptDecl *Concept = llvm::cast<ConceptDecl>(
    Actions.ActOnConceptDeclFactory(ConceptNameLoc, ConceptName, Constraint, Template).get());
```

**验证点**：编译通过 + 662 测试全部通过

---

## 四、新增 ActOn* 方法完整清单

### 4.1 子阶段 3B 新增（4 个）

| # | 方法签名 | 返回类型 | 说明 |
|---|---------|---------|------|
| 1 | `ActOnEnumConstantDeclFactory(SourceLocation, StringRef, QualType, Expr*)` | DeclResult | 枚举常量工厂 |
| 2 | `ActOnTranslationUnitDecl(SourceLocation)` | TranslationUnitDecl* | 翻译单元工厂 |
| 3 | — | — | ActOnNullStmt 已存在，无需新增 |
| 4 | — | — | ActOnDoStmt 已存在，无需新增 |

实际新增 **2 个**（ActOnEnumConstantDeclFactory、ActOnTranslationUnitDecl），ActOnVarDecl 已存在复用。

### 4.2 子阶段 3C 新增（6 个）

| # | 方法签名 | 返回类型 | 说明 |
|---|---------|---------|------|
| 1 | `ActOnCXXRecordDeclFactory(SourceLocation, StringRef, TagKind)` | DeclResult | 类/结构体/联合体工厂 |
| 2 | `ActOnCXXMethodDeclFactory(17 个参数)` | DeclResult | 成员方法工厂 |
| 3 | `ActOnCXXConstructorDeclFactory(SourceLocation, CXXRecordDecl*, ArrayRef<ParmVarDecl*>, Stmt*, bool)` | DeclResult | 构造函数工厂 |
| 4 | `ActOnCXXDestructorDeclFactory(SourceLocation, CXXRecordDecl*, Stmt*)` | DeclResult | 析构函数工厂 |
| 5 | `ActOnFriendTypeDecl(SourceLocation, StringRef, SourceLocation)` | DeclResult | 友元类型声明工厂 |
| 6 | `ActOnFriendFunctionDecl(SourceLocation, SourceLocation, StringRef, QualType, ArrayRef<ParmVarDecl*>)` | DeclResult | 友元函数声明工厂 |

### 4.3 子阶段 3D 新增（3 个）

| # | 方法签名 | 返回类型 | 说明 |
|---|---------|---------|------|
| 1 | `ActOnTemplateTypeParmDecl(SourceLocation, StringRef, unsigned, unsigned, bool, bool)` | DeclResult | 类型模板参数 |
| 2 | `ActOnNonTypeTemplateParmDecl(SourceLocation, StringRef, QualType, unsigned, unsigned, bool)` | DeclResult | 非类型模板参数 |
| 3 | `ActOnTemplateTemplateParmDecl(SourceLocation, StringRef, unsigned, unsigned, bool)` | DeclResult | 模板模板参数 |

### 4.4 子阶段 3E 新增（10 个）

| # | 方法签名 | 返回类型 | 说明 |
|---|---------|---------|------|
| 1 | `ActOnTemplateDecl(SourceLocation, StringRef, Decl*)` | DeclResult | 通用模板包装 |
| 2 | `ActOnClassTemplateDecl(SourceLocation, StringRef, Decl*)` | DeclResult | 类模板 |
| 3 | `ActOnFunctionTemplateDecl(SourceLocation, StringRef, Decl*)` | DeclResult | 函数模板 |
| 4 | `ActOnVarTemplateDecl(SourceLocation, StringRef, Decl*)` | DeclResult | 变量模板 |
| 5 | `ActOnTypeAliasTemplateDecl(SourceLocation, StringRef, Decl*)` | DeclResult | 别名模板 |
| 6 | `ActOnClassTemplateSpecializationDecl(SourceLocation, StringRef, ClassTemplateDecl*, ArrayRef<TemplateArgument>, bool)` | DeclResult | 类模板显式特化 |
| 7 | `ActOnVarTemplateSpecializationDecl(SourceLocation, StringRef, QualType, VarTemplateDecl*, ArrayRef<TemplateArgument>, Expr*, bool)` | DeclResult | 变量模板显式特化 |
| 8 | `ActOnClassTemplatePartialSpecDecl(SourceLocation, StringRef, ClassTemplateDecl*, ArrayRef<TemplateArgument>)` | DeclResult | 类模板偏特化 |
| 9 | `ActOnVarTemplatePartialSpecDecl(SourceLocation, StringRef, QualType, VarTemplateDecl*, ArrayRef<TemplateArgument>, Expr*)` | DeclResult | 变量模板偏特化 |
| 10 | `ActOnConceptDeclFactory(SourceLocation, StringRef, Expr*, TemplateDecl*)` | DeclResult | 概念工厂 |

### 4.5 方法总计

| 子阶段 | 新增方法数 |
|--------|----------|
| 3A | 0（复用已有 ActOnNullStmt） |
| 3B | 2 |
| 3C | 6 |
| 3D | 3 |
| 3E | 10 |
| **总计** | **21 个新方法** |

---

## 五、修改文件清单

| 文件 | 修改类型 | 涉及子阶段 | 修改处数 |
|------|---------|----------|---------|
| `src/Parse/ParseStmt.cpp` | 文本替换 | 3A, 3B | ~26 |
| `src/Parse/ParseStmtCXX.cpp` | 文本替换 | 3A, 3B | ~6 |
| `src/Parse/ParseExprCXX.cpp` | 文本替换 | 3A | 1 |
| `src/Parse/Parser.cpp` | 替换+调用改造 | 3B | 1 |
| `src/Parse/ParseDecl.cpp` | 替换 | 3B | 1 |
| `src/Parse/ParseClass.cpp` | 替换+调用改造 | 3C | 10 |
| `src/Parse/ParseTemplate.cpp` | 替换+调用改造 | 3D, 3E | 27 |
| `include/blocktype/Sema/Sema.h` | 新增方法声明 | 3B-3E | 21 个声明 |
| `src/Sema/Sema.cpp` | 新增方法实现 | 3B-3E | 21 个实现 |
| **总计** | | | **~73 处** |

---

## 六、执行顺序与验证策略

### 6.1 严格顺序

```
3A (NullStmt 批量替换)
  ↓ 编译 + 662 测试
3B (DoStmt/VarDecl/EnumConstant/TranslationUnit)
  ↓ 编译 + 662 测试
3C (Class 声明节点)
  ↓ 编译 + 662 测试
3D (模板参数节点)
  ↓ 编译 + 662 测试
3E (模板包装/特化节点)
  ↓ 编译 + 662 测试
3F: 最终验证 (Context.create 归零检查 + 代码审查)
```

### 6.2 每个子阶段的验证流程

1. **编译检查**：`cmake --build build` 无错误无警告
2. **全量测试**：`ctest --test-dir build --output-on-failure` 662/662 通过
3. **Context.create 计数**：`grep -r "Context.create" src/Parse/ | wc -l` 验证减少数量
4. **代码审查**：检查 ActOn* 调用的参数传递正确性

### 6.3 最终验证（3F）

1. **归零检查**：`grep -r "Context.create" src/Parse/` 应返回零结果
2. **Sema 内部 Context.create 检查**：确认 Sema 内部的 Context.create 正常（Sema 有权创建）
3. **全量回归测试**：662 测试全部通过
4. **代码审查**：
   - Parser 中不再有 `Context.create` 调用
   - 所有 AST 节点创建通过 `Actions.ActOn*()` 完成
   - ProcessAST 的防御性检查可以简化（不再有空类型节点）
5. **ProcessAST 清理评估**：评估是否可以移除 ProcessAST 中的防御性 null-type 检查

---

## 七、风险分析与回滚方案

### 7.1 风险矩阵

| 子阶段 | 风险 | 原因 | 缓解措施 |
|--------|------|------|---------|
| 3A | 极低 | 纯文本替换，逻辑不变 | 全局搜索替换，编译验证 |
| 3B | 低 | 少量新方法，逻辑简单 | 单点验证 |
| 3C | 中 | 方法参数多（17个），需要精确传递 | 逐一对比参数 |
| 3D | 中 | 模板参数有默认值设置，需保持时序 | 保留 setDefaultArgument 调用 |
| 3E | 中高 | 模板特化涉及 PrimaryTemplate 查找，逻辑分支多 | 保持分支逻辑不变，只替换 create 调用 |

### 7.2 回滚策略

- 每个子阶段提交独立 git commit
- 出问题时 `git revert` 单个 commit 即可回滚
- 不使用 `git rebase` 或 `--force` 推送

### 7.3 关键注意事项

1. **模板参数默认值**：模板参数节点的 `setDefaultArgument()` 在 create 之后调用。迁移到 Sema 后，Parser 仍需调用 `set*` 方法——ActOn* 方法只负责创建+注册，默认值设置仍在 Parser 侧完成。
2. **TemplateParameterList**：`new TemplateParameterList(...)` 不使用 Context.create（使用全局 new），无需迁移。
3. **PrimaryTemplate 查找**：模板特化中的 `CurrentScope->lookup()` 应保留在 Parser 中还是移入 Sema？方案：**保留在 Parser**，Parser 负责名称解析，将查找结果传给 ActOn* 方法。这符合 Clang 模式。
4. **行号偏移**：随着修改进行，行号会偏移。实际执行时以代码搜索而非行号为准。
5. **Parser Scope 管理**：Parser 的 `CurrentScope->addDecl()` 调用（如 CXXRecordDecl 后的 `CurrentScope->addDecl(Class)`）**必须保留**。Parser 维护自己的 Scope 用于解析期间名称查找，Sema 的 Symbols 是独立的。ActOn* 方法只负责 Sema 侧注册。
6. **addSpecialization/setTemplateParameterList**：模板特化节点的 `PrimaryTemplate->addSpecialization(Spec)` 和 `PartialSpec->setTemplateParameterList(TPL)` 调用需要在 Sema 的 ActOn* 方法内部完成。这些是语义操作，属于 Sema 职责。
7. **重复 ActOnFriendDecl 调用**：ParseClass.cpp 行 1036 有 `Actions.ActOnFriendDecl(FD)` 被调用了两次，这是阶段 2 引入的 bug，迁移时需修复（仅调用一次）。
8. **ConceptDecl 双重创建**：ConceptDecl 迁移涉及 2 处 Context.create（行 907 的 TemplateDecl + 行 913 的 ConceptDecl），`ActOnConceptDeclFactory` 方法内部需同时处理两者。

---

## 八、预期成果

| 指标 | 阶段 3 前 | 阶段 3 后 | 变化 |
|------|----------|----------|------|
| Context.create 调用数 | 70 | 0 | -100% |
| ActOn* 方法总数 | 61 | 82 | +21 新增 |
| Parser 直接创建节点 | 70 处 | 0 处 | 完全消除 |
| 架构纯净度 | 部分 Sema 委托 | 完全 Sema 委托 | Clang 标准架构 |

---

## 九、后续工作（阶段 4 预告）

阶段 3 完成后，可进入以下后续优化：

1. **ProcessAST 瘦身**：移除防御性 null-type 检查，简化为仅处理 Sema 无法在创建时完成的类型推断
2. **Scope 统一**：将 Parser 的 `CurrentScope` 和 Sema 的 `CurrentScope` 统一为 Sema 管理
3. **错误诊断增强**：ActOn* 方法中增加更丰富的语义错误提示
4. **ConstEval 前置**：将常量表达式求值从 ActOnEnumConstant 等方法前置到更多 ActOn* 方法中
