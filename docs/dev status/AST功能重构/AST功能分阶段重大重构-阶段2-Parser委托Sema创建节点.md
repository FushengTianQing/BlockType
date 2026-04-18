# AST 完整重构 - 阶段 2：Parser 委托 Sema 创建节点

> **状态**: 已完成（部分） — 172→70 处 Context.create 迁移完成，剩余 70 处见阶段 3  
> **前置依赖**: 阶段 1 已完成（CodeGen 已不再修改 AST）  
> **测试基线**: 662/662 全部通过  
> **已完成子阶段**: 2P, 2A, 2B, 2C, 2D, 2E  
> **后续**: 阶段 3 — 完成 70 处剩余迁移（见 `AST功能分阶段重大重构-阶段3-完成剩余迁移.md`）

---

## 一、目标

将 AST 节点创建和类型设置的职责从 Parser 全面移交到 Sema，建立标准编译器分层架构：

```
当前（阶段1后）:  Parser 直接创建节点 → Sema::ProcessAST 后处理补类型 → CodeGen 纯消费
目标（阶段2后）:  Parser 仅解析语法 → Sema::ActOn* 创建节点+设类型 → CodeGen 纯消费
```

### 核心收益
1. **类型安全**：表达式在创建时就拥有正确的 ExprTy，不再有空类型节点
2. **语义前置**：类型检查、名称查找在节点创建时同步完成，错误更早暴露
3. **消除 ProcessAST hack**：不再需要后置遍历补类型
4. **架构清晰**：Parser 只做语法解析，Sema 负责语义

---

## 二、现状分析

### 2.1 Parser 中 Context.create<> 调用分布（共 172 处）

| 文件 | 调用数 | 主要节点类型 |
|------|--------|------------|
| `ParseExpr.cpp` | 25 | BinaryOperator, UnaryOperator, CallExpr, MemberExpr, ArraySubscriptExpr, DeclRefExpr, IntegerLiteral, FloatingLiteral, StringLiteral, CharacterLiteral, CXXBoolLiteral, CXXNullPtrLiteral, ConditionalOperator, InitListExpr, DesignatedInitExpr, UnaryExprOrTypeTraitExpr, PackIndexingExpr, TemplateSpecializationExpr |
| `ParseExprCXX.cpp` | 16 | CXXNewExpr, CXXDeleteExpr, CXXThisExpr, CXXThrowExpr, CXXConstructExpr, CStyleCastExpr, CXXStaticCastExpr, CXXDynamicCastExpr, CXXConstCastExpr, CXXReinterpretCastExpr, LambdaExpr, CXXFoldExpr, RequiresExpr, PackIndexingExpr, ReflexprExpr, CoawaitExpr |
| `ParseStmt.cpp` | 44 | CompoundStmt, ReturnStmt, NullStmt, ExprStmt, LabelStmt, CaseStmt, DefaultStmt, BreakStmt, ContinueStmt, GotoStmt, IfStmt, SwitchStmt, WhileStmt, DoStmt, ForStmt, CXXForRangeStmt, VarDecl, LabelDecl, ExprStmt |
| `ParseStmtCXX.cpp` | 11 | NullStmt, CXXTryStmt, VarDecl, CXXCatchStmt, CoreturnStmt, CoyieldStmt, CoawaitExpr |
| `ParseClass.cpp` | 15 | CXXRecordDecl, FieldDecl, AccessSpecDecl, CXXMethodDecl, VarDecl, CXXConstructorDecl, CXXDestructorDecl, RecordDecl, FriendDecl, UsingDecl, FunctionDecl |
| `ParseDecl.cpp` | 31 | DeclStmt, TypeAliasDecl, UsingDecl, ParmVarDecl, NamespaceDecl, UsingEnumDecl, UsingDirectiveDecl, NamespaceAliasDecl, ModuleDecl, ImportDecl, ExportDecl, EnumDecl, EnumConstantDecl, TypedefDecl, StaticAssertDecl, LinkageSpecDecl, AsmDecl, CXXDeductionGuideDecl, AttributeListDecl, AttributeDecl, StringLiteral, CXXConstructExpr, VarDecl, FunctionDecl |
| `ParseTemplate.cpp` | 28 | TemplateDecl, ClassTemplateSpecializationDecl, VarTemplateSpecializationDecl, FunctionTemplateDecl, ClassTemplateDecl, VarTemplateDecl, TypeAliasTemplateDecl, TemplateTypeParmDecl, NonTypeTemplateParmDecl, TemplateTemplateParmDecl, ClassTemplatePartialSpecializationDecl, VarTemplatePartialSpecializationDecl, ConceptDecl, DeclRefExpr |
| `Parser.cpp` | 2 | TranslationUnitDecl, IntegerLiteral（错误恢复） |

### 2.2 已存在的 Sema::ActOn* 方法

Sema 已有 36 个 ActOn* 方法，覆盖了部分表达式、语句和模板处理：

| 类别 | 已有方法 |
|------|---------|
| 声明 | `ActOnDeclarator`, `ActOnFinishDecl`, `ActOnVarDecl`, `ActOnFunctionDecl`, `ActOnEnumConstant`, `ActOnTranslationUnit`, `ActOnStartOfFunctionDef`, `ActOnFinishOfFunctionDef` |
| 表达式 | `ActOnExpr`, `ActOnCallExpr`, `ActOnMemberExpr`, `ActOnBinaryOperator`, `ActOnUnaryOperator`, `ActOnCastExpr`, `ActOnArraySubscriptExpr`, `ActOnConditionalExpr`, `ActOnCXXNewExpr`, `ActOnCXXDeleteExpr` |
| 语句 | `ActOnReturnStmt`, `ActOnIfStmt`, `ActOnWhileStmt`, `ActOnForStmt`, `ActOnDoStmt`, `ActOnSwitchStmt`, `ActOnCaseStmt`, `ActOnDefaultStmt`, `ActOnBreakStmt`, `ActOnContinueStmt`, `ActOnGotoStmt`, `ActOnCompoundStmt`, `ActOnDeclStmt`, `ActOnNullStmt` |
| 模板 | `ActOnClassTemplateDecl`, `ActOnFunctionTemplateDecl`, `ActOnVarTemplateDecl`, `ActOnTypeAliasTemplateDecl`, `ActOnConceptDecl`, `ActOnTemplateId`, `ActOnExplicitSpecialization`, `ActOnExplicitInstantiation`, `ActOnClassTemplatePartialSpecialization`, `ActOnVarTemplatePartialSpecialization` |
| 其他 | `ProcessAST` |

### 2.3 需要新增的 ActOn* 方法（约 45 个）

#### 表达式（22 个）
```
ActOnIntegerLiteral(SourceLocation Loc, llvm::APInt Value)
ActOnFloatingLiteral(SourceLocation Loc, llvm::APFloat Value)
ActOnStringLiteral(SourceLocation Loc, llvm::StringRef Text)
ActOnCharacterLiteral(SourceLocation Loc, uint32_t Value)
ActOnCXXBoolLiteral(SourceLocation Loc, bool Value)
ActOnCXXNullPtrLiteral(SourceLocation Loc)
ActOnDeclRefExpr(SourceLocation Loc, ValueDecl *VD)
ActOnQualifiedDeclRefExpr(SourceLocation StartLoc, ValueDecl *VD)
ActOnInitListExpr(SourceLocation LBraceLoc, ArrayRef<Expr*> Inits, SourceLocation RBraceLoc)
ActOnDesignatedInitExpr(SourceLocation DotLoc, ArrayRef<Designator> Designators, Expr *Init)
ActOnUnaryExprOrTypeTraitExpr(SourceLocation OpLoc, UnaryExprOrTypeTraitKind Kind, QualType T)
ActOnUnaryExprOrTypeTraitExprExpr(SourceLocation OpLoc, UnaryExprOrTypeTraitKind Kind, Expr *Arg)
ActOnCXXConstructExpr(SourceLocation Loc, ArrayRef<Expr*> Args)
ActOnCXXThisExpr(SourceLocation ThisLoc)
ActOnCXXThrowExpr(SourceLocation ThrowLoc, Expr *Operand)
ActOnLambdaExpr(SourceLocation LambdaLoc, ArrayRef<LambdaCapture> Captures, ...)
ActOnCXXFoldExpr(SourceLocation FoldLoc, Expr *LHS, Expr *RHS, ...)
ActOnRequiresExpr(SourceLocation RequiresLoc, ...)
ActOnCStyleCastExpr(SourceLocation LParenLoc, Expr *SubExpr)
ActOnCXXNamedCastExpr(SourceLocation CastLoc, CastKind CK, Expr *SubExpr, QualType CastType)
ActOnPackIndexingExpr(SourceLocation EllipsisLoc, Expr *Pack, Expr *Index)
ActOnReflexprExpr(SourceLocation ReflexprLoc, Expr *Arg)
```

#### 声明（17 个）
```
ActOnCXXRecordDecl(SourceLocation NameLoc, llvm::StringRef Name, TagTypeKind TagKind)
ActOnFieldDecl(SourceLocation NameLoc, llvm::StringRef Name, QualType T, ...)
ActOnAccessSpecDecl(SourceLocation Loc, AccessSpecifier Access, SourceLocation ColonLoc)
ActOnCXXMethodDecl(SourceLocation NameLoc, llvm::StringRef Name, QualType Type, ...)
ActOnCXXConstructorDecl(SourceLocation Loc, CXXRecordDecl *Class, ...)
ActOnCXXDestructorDecl(SourceLocation Loc, CXXRecordDecl *Class, Stmt *Body)
ActOnFriendDecl(SourceLocation FriendLoc, FunctionDecl *FriendFunc, QualType FriendType, bool IsClass)
ActOnParmVarDecl(SourceLocation NameLoc, llvm::StringRef Name, QualType Type, unsigned Index, Expr *DefaultArg)
ActOnNamespaceDecl(SourceLocation NameLoc, llvm::StringRef Name, bool IsInline)
ActOnUsingDecl(SourceLocation NameLoc, llvm::StringRef Name, ...)
ActOnUsingDirectiveDecl(SourceLocation NameLoc, llvm::StringRef Name, ...)
ActOnEnumDecl(SourceLocation NameLoc, llvm::StringRef Name)
ActOnLabelDecl(SourceLocation Loc, llvm::StringRef Name)
ActOnTemplateTypeParmDecl(...)
ActOnNonTypeTemplateParmDecl(...)
ActOnTemplateTemplateParmDecl(...)
ActOnLinkageSpecDecl(SourceLocation Loc, llvm::StringRef Lang, bool HasBraces)
```

#### 语句（6 个）
```
ActOnLabelStmt(SourceLocation LabelLoc, LabelDecl *Label, Stmt *SubStmt)
ActOnExprStmt(SourceLocation Loc, Expr *E)
ActOnCXXTryStmt(SourceLocation TryLoc, Stmt *TryBlock, ArrayRef<Stmt*> CatchStmts)
ActOnCXXCatchStmt(SourceLocation CatchLoc, VarDecl *ExceptionDecl, Stmt *CatchBlock)
ActOnCXXForRangeStmt(SourceLocation ForLoc, VarDecl *RangeVar, Expr *Range, Stmt *Body)
ActOnCoreturnStmt(SourceLocation CoreturnLoc, Expr *RetVal)
ActOnCoyieldStmt(SourceLocation CoyieldLoc, Expr *Value)
```

---

## 三、架构设计

### 3.1 Parser → Sema 接口模式

采用 Clang 的 **Actions 模式**：Parser 持有一个 Sema 引用，所有节点创建都通过 `Sema.ActOn*()` 完成。

```cpp
// Parser.h — 修改构造函数
class Parser {
  Preprocessor &PP;
  ASTContext &Context;
  Sema &Actions;       // 新增：Sema 引用
  DiagnosticsEngine &Diags;
  // ...
public:
  Parser(Preprocessor &PP, ASTContext &Ctx, Sema &S);
};
```

```cpp
// driver.cpp — 修改编译流程
Sema S(Context, Diags);
Parser P(PP, Context, S);  // 传入 Sema 引用
TranslationUnitDecl *TU = P.parseTranslationUnit();
// S.ProcessAST(TU); ← 不再需要，类型在创建时就已设置
```

### 3.2 Sema::ActOn* 方法设计原则

1. **创建+类型化合一**：ActOn* 方法内部调用 `Context.create<T>()` 并立即设置 `ExprTy`
2. **语义检查**：类型检查、名称查找在 ActOn* 中完成，错误立即诊断
3. **返回 Result 类型**：使用 `ExprResult`/`StmtResult`/`DeclResult` 包装返回值，支持错误传播
4. **Scope 集成**：Sema 维护独立的 Scope 栈，Parser 通过 `Actions.PushScope()`/`Actions.PopScope()` 同步

### 3.3 不需要委托 Sema 的场景

以下场景 Parser 可以直接创建节点（无语义分析需求）：

| 节点类型 | 原因 |
|---------|------|
| `NullStmt`（错误恢复） | 纯占位节点，无类型信息 |
| `TranslationUnitDecl` | 顶层容器，在编译开始时创建 |
| `LabelDecl`（goto/label） | 无类型，仅做名称注册 |
| 模板参数声明（`TemplateTypeParmDecl` 等） | 参数性质声明，无类型检查 |
| 属性声明（`AttributeDecl` 等） | 元数据性质，无类型 |
| 模块/导入声明 | 模块系统特有，无类型 |

---

## 四、分步执行计划

### 总体策略：6 个子阶段，从低风险到高风险

```
子阶段 2A: 字面量表达式（6 个方法，6 处调用）        ← 风险极低
子阶段 2B: 简单语句（无需类型推断的 Stmt）           ← 风险低
子阶段 2C: 运算符和复合表达式（需要类型推断）        ← 风险中等
子阶段 2D: 声明节点（VarDecl, FunctionDecl 等）      ← 风险中等
子阶段 2E: 类和模板声明                              ← 风险较高
子阶段 2F: 清理和最终验证                            ← 收尾
```

每个子阶段的流程：
1. 在 Sema 中实现/完善 ActOn* 方法
2. 修改 Parser 调用点从 `Context.create<T>()` 改为 `Actions.ActOn*()`
3. 编译 + 运行全量测试
4. Git commit 检查点

---

### 子阶段 2A：字面量表达式（风险极低）

**新增方法（6 个）**：

```cpp
// Sema.h
ExprResult ActOnIntegerLiteral(SourceLocation Loc, llvm::APInt Value);
ExprResult ActOnFloatingLiteral(SourceLocation Loc, llvm::APFloat Value);
ExprResult ActOnStringLiteral(SourceLocation Loc, llvm::StringRef Text);
ExprResult ActOnCharacterLiteral(SourceLocation Loc, uint32_t Value);
ExprResult ActOnCXXBoolLiteral(SourceLocation Loc, bool Value);
ExprResult ActOnCXXNullPtrLiteral(SourceLocation Loc);
```

**修改文件**：

| 文件 | 行号 | 当前代码 | 改为 |
|------|------|---------|------|
| `ParseExpr.cpp` | 754 | `Context.create<IntegerLiteral>(Loc, Value, Context.getIntType())` | `Actions.ActOnIntegerLiteral(Loc, Value).get()` |
| `ParseExpr.cpp` | 797 | `Context.create<FloatingLiteral>(...)` | `Actions.ActOnFloatingLiteral(...).get()` |
| `ParseExpr.cpp` | 812 | `Context.create<StringLiteral>(...)` | `Actions.ActOnStringLiteral(...).get()` |
| `ParseExpr.cpp` | 883 | `Context.create<CharacterLiteral>(...)` | `Actions.ActOnCharacterLiteral(...).get()` |
| `ParseExpr.cpp` | 891 | `Context.create<CXXBoolLiteral>(...)` | `Actions.ActOnCXXBoolLiteral(...).get()` |
| `ParseExpr.cpp` | 898 | `Context.create<CXXNullPtrLiteral>(...)` | `Actions.ActOnCXXNullPtrLiteral(...).get()` |
| `Parser.cpp` | 374 | `Context.create<IntegerLiteral>(Loc, ...)` （错误恢复） | 保留原样（错误恢复场景） |

**Sema 实现（示例）**：

```cpp
ExprResult Sema::ActOnIntegerLiteral(SourceLocation Loc, llvm::APInt Value) {
  QualType Ty = Context.getIntType();
  return Context.create<IntegerLiteral>(Loc, Value, Ty);
}
```

**影响范围**：6 处调用修改 + 6 个 Sema 方法实现  
**风险**：极低（字面量类型固定，无外部依赖）  
**预计工作量**：0.5 天

---

### 子阶段 2B：简单语句（风险低）

**涉及节点**：NullStmt, BreakStmt, ContinueStmt, ReturnStmt, CompoundStmt, ExprStmt, LabelStmt, CaseStmt, DefaultStmt, GotoStmt, DeclStmt, CXXTryStmt, CXXCatchStmt, CoreturnStmt, CoyieldStmt

**设计决策**：

- **NullStmt/BreakStmt/ContinueStmt**：这些语句无需类型推断，大部分已存在 ActOn* 方法
- **错误恢复中的 NullStmt**：保持 Parser 直接创建（无需 Sema 介入）
- **CompoundStmt**：已有 `ActOnCompoundStmt`，直接使用
- **ReturnStmt/IfStmt/WhileStmt/ForStmt/DoStmt/SwitchStmt 等**：已有对应 ActOn* 方法

**新增方法（约 6 个）**：

```cpp
StmtResult ActOnLabelStmt(SourceLocation LabelLoc, LabelDecl *Label, Stmt *SubStmt);
StmtResult ActOnExprStmt(SourceLocation Loc, Expr *E);
StmtResult ActOnCXXTryStmt(SourceLocation TryLoc, Stmt *TryBlock, ArrayRef<Stmt*> CatchStmts);
StmtResult ActOnCXXCatchStmt(SourceLocation CatchLoc, VarDecl *ExceptionDecl, Stmt *CatchBlock);
StmtResult ActOnCoreturnStmt(SourceLocation CoreturnLoc, Expr *RetVal);
StmtResult ActOnCoyieldStmt(SourceLocation CoyieldLoc, Expr *Value);
```

**修改统计**：

| 文件 | 修改处数 | 说明 |
|------|---------|------|
| `ParseStmt.cpp` | ~30 | 控制流语句替换为 ActOn* 调用 |
| `ParseStmtCXX.cpp` | ~6 | try/catch/co_* 替换 |
| 错误恢复 NullStmt | ~20 | **保留原样**，不委托 Sema |

**影响范围**：~36 处调用修改  
**风险**：低（语句节点无需 ExprTy）  
**预计工作量**：1 天

---

### 子阶段 2C：运算符和复合表达式（风险中等）

**涉及节点**：BinaryOperator, UnaryOperator, CallExpr, MemberExpr, ArraySubscriptExpr, DeclRefExpr, ConditionalOperator, InitListExpr, DesignatedInitExpr, UnaryExprOrTypeTraitExpr, CXXConstructExpr, CXXNewExpr, CXXDeleteExpr, CXXThisExpr, CXXThrowExpr, CStyleCastExpr, CXX*CastExpr, LambdaExpr, PackIndexingExpr, TemplateSpecializationExpr

**这是最复杂的子阶段**，因为大部分表达式需要类型推断。

**已有方法**（可直接使用，需确认签名匹配）：
- `ActOnBinaryOperator`, `ActOnUnaryOperator`, `ActOnCallExpr`, `ActOnMemberExpr`
- `ActOnCastExpr`, `ActOnArraySubscriptExpr`, `ActOnConditionalExpr`
- `ActOnCXXNewExpr`, `ActOnCXXDeleteExpr`

**新增方法（约 14 个）**：

```cpp
ExprResult ActOnDeclRefExpr(SourceLocation Loc, ValueDecl *VD);
ExprResult ActOnCXXThisExpr(SourceLocation ThisLoc);
ExprResult ActOnCXXThrowExpr(SourceLocation ThrowLoc, Expr *Operand);
ExprResult ActOnCXXConstructExpr(SourceLocation Loc, ArrayRef<Expr*> Args);
ExprResult ActOnInitListExpr(SourceLocation LBraceLoc, ArrayRef<Expr*> Inits, SourceLocation RBraceLoc);
ExprResult ActOnDesignatedInitExpr(SourceLocation DotLoc, ArrayRef<Designator> D, Expr *Init);
ExprResult ActOnUnaryExprOrTypeTraitExpr(SourceLocation OpLoc, UnaryExprOrTypeTraitKind Kind, QualType T);
ExprResult ActOnUnaryExprOrTypeTraitExprExpr(SourceLocation OpLoc, UnaryExprOrTypeTraitKind Kind, Expr *Arg);
ExprResult ActOnCStyleCastExpr(SourceLocation LParenLoc, Expr *SubExpr);
ExprResult ActOnCXXNamedCastExpr(SourceLocation CastLoc, CastKind CK, Expr *SubExpr, QualType CastType);
ExprResult ActOnLambdaExpr(SourceLocation LambdaLoc, ArrayRef<LambdaCapture> Captures, ...);
ExprResult ActOnPackIndexingExpr(SourceLocation EllipsisLoc, Expr *Pack, Expr *Index);
ExprResult ActOnTemplateSpecializationExpr(SourceLocation StartLoc, llvm::StringRef Name, ArrayRef<TemplateArgument> Args);
ExprResult ActOnCoawaitExpr(SourceLocation CoawaitLoc, Expr *Operand);
```

**类型推断规则**：

| 表达式 | ExprTy | 推断来源 |
|--------|--------|---------|
| BinaryOperator | 依赖 Op | LHS/RHS 的公共类型；比较→bool；移位→LHS |
| UnaryOperator | 依赖 Op | 操作数类型；`!`/`~`→bool/int；`*`→元素类型；`&`→指针 |
| CallExpr | 返回类型 | Callee 的 FunctionDecl::getReturnType() |
| MemberExpr | 成员类型 | 成员 ValueDecl::getType() |
| DeclRefExpr | 声明类型 | ValueDecl::getType() |
| CXXThisExpr | Class* | CurFunction 所在类的指针类型 |
| CXXNewExpr | T* | AllocatedType 的指针 |
| CXXDeleteExpr | void | 固定 |
| CXXConstructExpr | T | 构造函数所在类 |
| *CastExpr | 目标类型 | 显式指定的 CastType |
| ConditionalOperator | 公共类型 | Then/Else 的公共类型 |
| InitListExpr | 依赖上下文 | 无上下文时设为 void |
| UnaryExprOrTypeTraitExpr | size_t | 固定 |

**修改统计**：

| 文件 | 修改处数 |
|------|---------|
| `ParseExpr.cpp` | ~19 处 |
| `ParseExprCXX.cpp` | ~14 处 |

**影响范围**：~33 处调用修改 + ~14 个新 Sema 方法  
**风险**：中等（类型推断逻辑需要完整实现）  
**预计工作量**：2-3 天

---

### 子阶段 2D：声明节点（风险中等）

**涉及节点**：VarDecl, FunctionDecl, ParmVarDecl, CXXRecordDecl, FieldDecl, CXXMethodDecl, CXXConstructorDecl, CXXDestructorDecl, EnumDecl, EnumConstantDecl, NamespaceDecl, UsingDecl, FriendDecl, AccessSpecDecl, TypedefDecl, TypeAliasDecl, StaticAssertDecl, LinkageSpecDecl 等

**已有方法**：
- `ActOnVarDecl`, `ActOnFunctionDecl`, `ActOnDeclarator`, `ActOnEnumConstant`

**设计决策**：

声明节点的改造比表达式简单，因为：
1. 声明的类型在 Parser 中已经通过 `parseType()` 确定
2. 声明不需要 ExprTy
3. 语义分析主要是名称注册（SymbolTable）和作用域检查

**新增方法（约 15 个）**：

```cpp
DeclResult ActOnCXXRecordDecl(SourceLocation NameLoc, llvm::StringRef Name, TagTypeKind TagKind);
DeclResult ActOnFieldDecl(SourceLocation NameLoc, llvm::StringRef Name, QualType T, Expr *BitWidth, bool IsMutable, Expr *InClassInit, AccessSpecifier Access);
DeclResult ActOnAccessSpecDecl(SourceLocation Loc, AccessSpecifier Access, SourceLocation ColonLoc);
DeclResult ActOnCXXMethodDecl(SourceLocation NameLoc, llvm::StringRef Name, QualType Type, ArrayRef<ParmVarDecl*> Params, CXXRecordDecl *Class, Stmt *Body, ...);
DeclResult ActOnCXXConstructorDecl(SourceLocation Loc, CXXRecordDecl *Class, ArrayRef<ParmVarDecl*> Params, Stmt *Body, bool IsExplicit);
DeclResult ActOnCXXDestructorDecl(SourceLocation Loc, CXXRecordDecl *Class, Stmt *Body);
DeclResult ActOnFriendDecl(SourceLocation FriendLoc, FunctionDecl *FriendFunc, QualType FriendType, bool IsClass);
DeclResult ActOnParmVarDecl(SourceLocation NameLoc, llvm::StringRef Name, QualType Type, unsigned Index, Expr *DefaultArg);
DeclResult ActOnNamespaceDecl(SourceLocation NameLoc, llvm::StringRef Name, bool IsInline);
DeclResult ActOnEnumDecl(SourceLocation NameLoc, llvm::StringRef Name);
DeclResult ActOnLabelDecl(SourceLocation Loc, llvm::StringRef Name);
DeclResult ActOnTypedefDecl(SourceLocation Loc, llvm::StringRef Name, QualType T);
DeclResult ActOnTypeAliasDecl(SourceLocation Loc, llvm::StringRef Name, QualType T);
DeclResult ActOnLinkageSpecDecl(SourceLocation Loc, llvm::StringRef Lang, bool HasBraces);
DeclResult ActOnCXXDeductionGuideDecl(SourceLocation NameLoc, llvm::StringRef Name, QualType ReturnType, ...);
```

**修改统计**：

| 文件 | 修改处数 |
|------|---------|
| `ParseDecl.cpp` | ~25 处 |
| `ParseClass.cpp` | ~12 处 |
| `ParseStmt.cpp` (VarDecl) | ~4 处 |
| `ParseStmtCXX.cpp` (VarDecl) | ~1 处 |

**影响范围**：~42 处调用修改 + ~15 个新 Sema 方法  
**风险**：中等  
**预计工作量**：2 天

---

### 子阶段 2E：类和模板声明（风险较高）

**涉及节点**：TemplateDecl, ClassTemplateDecl, FunctionTemplateDecl, VarTemplateDecl, TypeAliasTemplateDecl, ConceptDecl, TemplateTypeParmDecl, NonTypeTemplateParmDecl, TemplateTemplateParmDecl, 各种 SpecializationDecl

**已有方法**：
- `ActOnClassTemplateDecl`, `ActOnFunctionTemplateDecl`, `ActOnVarTemplateDecl`
- `ActOnTypeAliasTemplateDecl`, `ActOnConceptDecl`

**设计决策**：

模板声明是最复杂的部分，但 Sema 已有模板相关的基础设施（TemplateInstantiator, TemplateDeduction, ConstraintSatisfaction）。改造重点：

1. 模板参数声明（TemplateTypeParmDecl 等）：保持 Parser 直接创建，仅做名称注册
2. 模板声明体（ClassTemplateDecl 等）：通过 ActOn* 创建，Sema 负责注册到模板特化表

**修改统计**：

| 文件 | 修改处数 |
|------|---------|
| `ParseTemplate.cpp` | ~20 处 |
| `ParseClass.cpp` | ~3 处 |

**影响范围**：~23 处调用修改  
**风险**：较高（模板特化/实例化链路复杂）  
**预计工作量**：2 天

---

### 子阶段 2F：清理和最终验证

1. **删除 `Sema::ProcessAST()`**：当所有节点都在创建时就拥有正确类型后，ProcessAST 不再需要
2. **从 driver.cpp 移除 ProcessAST 调用**
3. **全面测试**：运行全量测试 + 手动验证复杂用例
4. **更新注释和文档**
5. **代码审查**：确认无遗留的 Parser 直接创建节点（错误恢复除外）

---

## 五、关键风险与应对方案

### 5.1 循环依赖风险

**问题**：Parser 包含 `Sema.h`，Sema 包含 AST 头文件。如果 Sema.h 包含 `Parser.h`，就会循环依赖。

**方案**：不存在此风险。Parser → Sema → AST 是单向依赖链。Sema 不需要知道 Parser 的存在。

**验证**：
- `Parser.h` 包含：AST、Lex、Parse 子头文件 + `blocktype/Sema/Scope.h`
- `Sema.h` 包含：AST、Sema 子头文件
- `Sema.h` 不包含任何 Parse/ 头文件 → 无循环依赖 ✓

### 5.2 Scope 统一问题

**问题**：Parser 和 Sema 各有独立的 Scope 栈（`Parser::CurrentScope` vs `Sema::CurrentScope`）。

**方案**：统一使用 Sema 的 Scope。

```cpp
// Parser.h — 移除 CurrentScope 成员
class Parser {
  // Scope *CurrentScope = nullptr;  ← 删除
  // 改用 Actions.getCurrentScope()
};

// Parser 中的 pushScope/popScope 委托给 Sema
void Parser::pushScope(ScopeFlags Flags) {
  Actions.PushScope(Flags);
}
void Parser::popScope() {
  Actions.PopScope();
}
```

**迁移时机**：子阶段 2B（语句改造时同步完成）

### 5.3 错误恢复路径

**问题**：Parser 中约 20 处错误恢复创建 NullStmt/IntegerLiteral，这些场景不需要 Sema 介入。

**方案**：错误恢复节点保持 Parser 直接用 `Context.create<>()` 创建。在 ActOn* 方法的文档中明确标注：

```cpp
/// NOTE: Error recovery nodes (NullStmt for empty bodies, IntegerLiteral(0) for
///       invalid expressions) may be created directly by Parser without Sema.
///       These nodes have trivial types and require no semantic analysis.
```

### 5.4 查找失败路径

**问题**：`DeclRefExpr` 和 `MemberExpr` 依赖名称查找结果。当查找失败时，当前 Parser 返回 `createRecoveryExpr()`，不应调用 Sema。

**方案**：Parser 在查找失败时仍使用 `createRecoveryExpr()`。仅当查找成功获得 `ValueDecl*` 时才调用 `Actions.ActOnDeclRefExpr()`。

### 5.5 TemplateInstantiation 路径

**问题**：`TemplateInstantiator` 内部也使用 `Context.create<>()` 创建 AST 节点。

**方案**：不改造。TemplateInstantiator 是 Sema 的内部组件，其创建的节点已经过语义分析，与 Parser 无关。只需确保 Instantiator 创建的节点具有正确的类型。

### 5.6 测试安全网

**当前测试覆盖**：
- 662 个测试用例（全部通过）
- 单元测试覆盖 Parser、Sema、CodeGen 各模块

**新增测试策略**：
- 每个子阶段完成后运行全量测试
- 为新增的 ActOn* 方法编写单元测试
- 特别关注边界情况：空表达式、类型不匹配、名称查找失败

### 5.7 回滚策略

每个子阶段完成后 git commit，如发现问题可直接 `git revert` 回退到上一个检查点。

```
commit: "阶段2A: 字面量表达式委托Sema"
commit: "阶段2B: 简单语句委托Sema"
commit: "阶段2C: 运算符和复合表达式委托Sema"
commit: "阶段2D: 声明节点委托Sema"
commit: "阶段2E: 类和模板声明委托Sema"
commit: "阶段2F: 清理ProcessAST + 最终验证"
```

---

## 六、工作量估算

| 子阶段 | 新增 Sema 方法 | 修改 Parser 处 | 工作量 | 风险 |
|--------|--------------|---------------|--------|------|
| 2A 字面量 | 6 | 6 | 0.5 天 | 极低 |
| 2B 简单语句 | ~6 | ~36 | 1 天 | 低 |
| 2C 运算符表达式 | ~14 | ~33 | 2-3 天 | 中等 |
| 2D 声明节点 | ~15 | ~42 | 2 天 | 中等 |
| 2E 模板声明 | 0（已有） | ~23 | 2 天 | 较高 |
| 2F 清理验证 | 0 | ~5 | 0.5 天 | 低 |
| **合计** | **~41** | **~145** | **8-9 天** | |

---

## 七、预期最终架构

```
┌─────────────────────────────────────────────────────┐
│                    driver.cpp                        │
│                                                      │
│  Preprocessor PP;                                    │
│  ASTContext Context;                                 │
│  DiagnosticsEngine Diags;                            │
│  Sema S(Context, Diags);                             │
│  Parser P(PP, Context, S);                           │
│                                                      │
│  auto *TU = P.parseTranslationUnit();  // Parser 通过 Sema 创建节点
│  // S.ProcessAST(TU);  ← 已删除，不再需要            │
│  CodeGenModule CGM(Context, Diags);                  │
│  CGM.EmitTranslationUnit(TU);         // CodeGen 纯只读消费 AST
└─────────────────────────────────────────────────────┘
```

```
Parser 职责:                    Sema 职责:                   CodeGen 职责:
- 词法/语法分析                 - AST 节点创建               - IR 生成
- Token 消费                    - 类型推断和设置 ExprTy       - 只读访问 AST
- 调用 Actions.ActOn*()         - 名称查找                    - 不修改 AST
- 维护解析上下文                - 类型检查                    - 不创建 AST 节点
- 错误恢复（直接创建节点）       - 作用域管理
                                - 诊断输出
```

---

## 八、核查发现的遗漏与补充（2026-04-19 全面审计）

> 以下问题通过逐行审计全部 172 处 `Context.create<>` 调用、13 处 `new` 创建、
> Sema 58 个已实现方法、Parser.h/driver.cpp/Scope 全链路发现。

### 8.1 🔴 遗漏 1：ParseDecl.cpp 中约 10 个声明类型未列入改造计划

**问题**：子阶段 2D 中声明节点列表遗漏了以下 `Context.create<>` 调用：

| 节点类型 | 文件:行号 | 处数 | 现状 |
|---------|-----------|------|------|
| `ModuleDecl` | ParseDecl.cpp:684, 736, 751 | 3 | **未列入** |
| `ImportDecl` | ParseDecl.cpp:885, 905 | 2 | **未列入** |
| `ExportDecl` | ParseDecl.cpp:927 | 1 | **未列入** |
| `UsingEnumDecl` | ParseDecl.cpp:530 | 1 | **未列入** |
| `NamespaceAliasDecl` | ParseDecl.cpp:648 | 1 | **未列入** |
| `StaticAssertDecl` | ParseDecl.cpp:1235 | 1 | **未列入** |
| `AsmDecl` | ParseDecl.cpp:1350 | 1 | **未列入** |
| `AttributeListDecl` | ParseDecl.cpp:1465 | 1 | **未列入** |
| `AttributeDecl` | ParseDecl.cpp:1531, 1533 | 2 | **未列入** |

**新增 ActOn* 方法（补充 9 个）**：

```cpp
DeclResult ActOnModuleDecl(SourceLocation ModuleLoc, llvm::StringRef Name,
                            bool IsExported, llvm::StringRef Partition,
                            bool IsModulePartition);
DeclResult ActOnImportDecl(SourceLocation ImportLoc, llvm::StringRef ModuleName,
                            bool IsExported, llvm::StringRef Partition = "",
                            llvm::StringRef HeaderName = "", bool IsHeader = false);
DeclResult ActOnExportDecl(SourceLocation ExportLoc, Decl *ExportedDecl);
DeclResult ActOnUsingEnumDecl(SourceLocation EnumLoc, llvm::StringRef EnumName,
                               llvm::StringRef NestedName, bool HasNested);
DeclResult ActOnNamespaceAliasDecl(SourceLocation AliasLoc, llvm::StringRef AliasName,
                                    llvm::StringRef TargetName, llvm::StringRef NestedName);
DeclResult ActOnStaticAssertDecl(SourceLocation Loc, Expr *Cond, StringLiteral *Msg);
DeclResult ActOnAsmDecl(SourceLocation Loc, StringLiteral *AsmString);
DeclResult ActOnAttributeListDecl(SourceLocation Loc);
DeclResult ActOnAttributeDecl(SourceLocation Loc, llvm::StringRef AttrName, Expr *Arg);
DeclResult ActOnAttributeDecl(SourceLocation Loc, llvm::StringRef Namespace,
                               llvm::StringRef AttrName, Expr *Arg);
```

**影响**：新增 9 个方法签名 + 约 13 处调用点修改  
**子阶段归属**：2D（声明节点改造）

### 8.2 🔴 遗漏 2：Name Lookup 迁移方案缺失

**问题**：Parser 当前有 ~15 处 `CurrentScope->lookup()` 名称查找和 ~10 处 `CurrentScope->addDecl()` 声明注册，全部绕过 Sema。方案文档仅提到 "统一使用 Sema 的 Scope"，但未给出具体迁移路径。

**受影响位置**：

| 文件 | `lookup()` 处数 | `addDecl()` 处数 |
|------|----------------|-----------------|
| ParseExpr.cpp | 7 | 0 |
| ParseType.cpp | 2 | 0 |
| ParseClass.cpp | 1 | 7 |
| ParseTemplate.cpp | 5 | 1 |
| ParseStmt.cpp | 1 | 0 |
| Parser.cpp | 0 | 1 |

**迁移策略**：

```cpp
// 修改前（Parser 直接查找）
ValueDecl *VD = nullptr;
if (CurrentScope) {
  if (NamedDecl *D = CurrentScope->lookup(Name))
    VD = dyn_cast<ValueDecl>(D);
}

// 修改后（委托 Sema 查找）
ValueDecl *VD = Actions.LookupNameAsValue(Name);
// Sema::LookupNameAsValue 内部使用 Sema 自己的 Scope 栈
```

**Sema 新增便捷方法**：

```cpp
/// 在当前 Scope 中查找名称并返回为 ValueDecl（Parser 常用模式）
ValueDecl *LookupNameAsValue(llvm::StringRef Name);

/// 在当前 Scope 中注册声明（Parser 常用模式）
void RegisterDecl(NamedDecl *D);
```

**子阶段归属**：2A（前置基础设施改造时同步完成）

### 8.3 🟡 遗漏 3：`CXXCtorInitializer` 的 `new` 创建

**问题**：`ParseClass.cpp:976` 使用 `new CXXCtorInitializer(...)` 创建构造函数初始化器，绕过了 arena 分配器。

**方案**：

```cpp
// 修改前
return new CXXCtorInitializer(MemberLoc, MemberName, Args, false, IsDelegating);

// 方案 A：使用 Context.create（需让 CXXCtorInitializer 继承 ASTNode）
// 方案 B：保持 new 创建，但在 Sema 中包装
CXXCtorInitializer *Init = Actions.CreateCXXCtorInitializer(MemberLoc, MemberName, Args, IsDelegating);
```

**推荐**：方案 B（不修改继承体系），新增 `Sema::CreateCXXCtorInitializer()`。  
**子阶段归属**：2D（类声明改造时处理）

### 8.4 🔴 遗漏 4：ProcessAST 与 ActOn* 共存策略

**问题**：渐进迁移期间（2A-2E），Parser 部分节点通过 `Actions.ActOn*()` 创建（已类型化），部分节点仍通过 `Context.create<>()` 创建（未类型化）。`ProcessAST` 需继续处理未迁移的节点，但**不能重复处理已迁移的节点**。

**方案**：在 ProcessAST 中增加"已类型化"检查：

```cpp
// Sema::ProcessAST 中的 ASTVisitor 修改
void VisitCXXNewExpr(CXXNewExpr *E) override {
  // 已通过 ActOn* 设置过类型的节点，跳过
  if (!E->getType().isNull()) return;
  // ... 原有处理逻辑
}
```

或者更通用的方案：在 ASTNode 基类增加标记位 `bool SemaProcessed = false`。

**子阶段归属**：2A 前置（基础设施改造阶段添加防御性检查）

### 8.5 🟡 遗漏 5：ParseType.cpp 的 Scope 交互

**问题**：`ParseType.cpp` 在 2 处使用 `CurrentScope->lookup()` 做类型名查找（行 108, 166），不涉及 `Context.create<>()` 但涉及 Scope 交互。

**方案**：Name Lookup 迁移时（遗漏 2 的修复）一并处理。  
**子阶段归属**：2A

### 8.6 🟡 遗漏 6：错误恢复 NullStmt 计数修正

**修正**：

| 文件 | NullStmt 错误恢复处数 |
|------|---------------------|
| ParseStmt.cpp | 20 |
| ParseStmtCXX.cpp | 5 |
| ParseExprCXX.cpp | 1 |
| **合计** | **26**（非文档所述的 ~20） |

新增 6 处未计入，影响 2B 的工作量估算：从 "~36 处修改" 修正为 "~42 处修改"。

### 8.7 🟡 遗漏 7：前置基础设施改造步骤

**问题**：方案假设 Parser 已持有 Sema 引用，但没有将此改造作为独立前置步骤。

**补充前置步骤（子阶段 2P）**：

```
子阶段 2P（Pre-requisite）：
1. Parser.h: 添加 Sema &Actions 成员，修改构造函数为 Parser(PP, Context, S)
2. driver.cpp: 将 Sema 创建移到 Parser 之前，传入 Sema 引用
3. CMakeLists.txt: 确认 blocktype-parse 已链接 blocktype-sema（已满足）
4. 编译 + 运行全量测试
5. Git commit: "阶段2P: Parser持有Sema引用"
```

**工作量**：0.5 天

### 8.8 🟡 遗漏 8：Scope 双轨制到单轨制的过渡方案

**问题**：不能一次性将 Parser 的 `CurrentScope` 替换为 Sema 的 Scope。需要渐进过渡。

**过渡方案**：

```
阶段 A（2P-2E 期间）：双 Scope 保持同步
  - Parser::pushScope() 同时维护 CurrentScope 和 Actions.PushScope()
  - Parser::popScope() 同时维护 CurrentScope 和 Actions.PopScope()
  - Name lookup 逐步从 CurrentScope->lookup() 迁移到 Actions.LookupNameAsValue()

阶段 B（2F 清理阶段）：移除 Parser 的 CurrentScope
  - Parser::pushScope/popScope 委托给 Actions
  - 删除 Parser::CurrentScope 成员变量
```

### 8.9 🟢 遗漏 9：`TemplateParameterList` 的内存管理

**问题**：6 处 `new TemplateParameterList(...)` 使用堆分配而非 arena 分配。虽然 `TemplateParameterList` 不是 ASTNode，但存在内存泄漏风险。

**方案**：保持现状（生命周期与 ASTContext 绑定，在 ASTContext 析构时统一释放）。如果后续需要，可将 `TemplateParameterList` 的分配改为使用 `Context.getAllocator()`。

### 8.10 🟢 遗漏 10：`RecoveryExpr` 错误恢复表达式

**问题**：`RecoveryExpr` 类存在于 `Expr.h`（行 1542），但 Parser 中没有通过 `Context.create<RecoveryExpr>()` 创建它的调用。当前的错误恢复使用 `NullStmt` 和零值 `IntegerLiteral`。

**方案**：当前不涉及改造。但建议在后续阶段将错误恢复统一改为 `RecoveryExpr`，以区分真正的空语句和错误恢复节点。

---

## 九、更新后的完整方法清单

### 新增 ActOn* 方法汇总（原 45 个 + 补充 11 个 = **56 个**）

| 子阶段 | 新增方法数 | 补充方法 | 总修改处数 |
|--------|-----------|---------|-----------|
| 2P 前置 | 0 | — | ~5（基础设施） |
| 2A 字面量 | 6 | — | 6 |
| 2B 简单语句 | 6 | — | 42（原 36 + 6 NullStmt 修正） |
| 2C 运算符表达式 | 14 | — | 33 |
| 2D 声明节点 | 15 + 9 = **24** | ModuleDecl, ImportDecl, ExportDecl, UsingEnumDecl, NamespaceAliasDecl, StaticAssertDecl, AsmDecl, AttributeListDecl, AttributeDecl + Name Lookup 迁移 | 55（原 42 + 13 补充） |
| 2E 模板声明 | 0 | — | 23 |
| 2F 清理 | 0 | — | 5 |
| **合计** | **56** | — | **~169** |

### 更新后的工作量估算

| 子阶段 | 工作量（原） | 工作量（更新） |
|--------|------------|--------------|
| 2P 前置基础设施 | — | **0.5 天** |
| 2A 字面量 | 0.5 天 | 0.5 天 |
| 2B 简单语句 | 1 天 | **1.5 天**（+26 NullStmt, +Scope 同步） |
| 2C 运算符表达式 | 2-3 天 | 2-3 天 |
| 2D 声明节点 | 2 天 | **3 天**（+13 补充节点, +Name Lookup） |
| 2E 模板声明 | 2 天 | 2 天 |
| 2F 清理验证 | 0.5 天 | 0.5 天 |
| **合计** | **8-9 天** | **10-11 天** |

---

## 十、前置条件检查清单

- [x] 阶段 1 已完成：CodeGen 不再修改 AST
- [x] `Sema::ProcessAST()` 在 driver.cpp 中被调用
- [x] 662/662 测试全部通过
- [x] 无循环依赖风险
- [x] Parser 构造函数签名可扩展（添加 Sema& 参数）
- [x] 错误恢复路径已识别并标记为"不需改造"（26 处，非 20 处）
- [ ] ~~待实施~~：子阶段 2P 前置基础设施改造
- [ ] ~~待实施~~：ProcessAST 防御性检查（避免与 ActOn* 重复处理）
- [ ] ~~待实施~~：Name Lookup 从 Parser 迁移到 Sema（~25 处）

---

## 十一、参考资料

- Clang 源码中的 Parser/Sema 分工（`clang/lib/Parse/ParseExpr.cpp` 使用 `Actions.ActOn*()`）
- Clang Architecture: https://clang.llvm.org/docs/InternalsManual.html
- 阶段 1 重构文档：`docs/dev status/AST功能重构/AST功能分阶段重大重构-阶段1.md`
