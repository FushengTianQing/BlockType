# Task 2.2.1: 函数调用处理 - 函数清单

**子任务**: 2.2.1  
**功能域**: 函数调用处理 (Function Call Handling)  
**执行时间**: 2026-04-19 18:05-18:20  
**状态**: ✅ DONE

---

## 📊 函数清单总览

| 模块 | 函数名 | 文件位置 | 行号 | 职责 |
|------|--------|---------|------|------|
| **Parser** | parsePostfixExpression | ParseExpr.cpp | L479 | 后缀表达式解析（含调用） |
| | parseCallExpression | ParseExpr.cpp | L1292 | 解析func(args)语法 |
| | parseCallArguments | ParseExpr.cpp | L1311 | 解析参数列表 |
| **Sema** | ActOnCallExpr | Sema.cpp | L1996 | 语义分析函数调用 |
| | ResolveOverload | Sema.cpp | L2781 | 重载决议 |
| | DeduceAndInstantiateFunctionTemplate | SemaTemplate.cpp | L669 | 模板实参推导+实例化 |
| **TypeCheck** | CheckCall | TypeCheck.cpp | L179 | 参数类型检查 |

**总计**: 7个核心函数

---

## 📝 详细函数说明

### Parser层

#### 1. parsePostfixExpression
**文件**: `src/Parse/ParseExpr.cpp`  
**行号**: L479-570（约90行）

**职责**: 解析后缀表达式，包括：
- 函数调用: `func(args)`
- 数组下标: `arr[index]`
- 成员访问: `obj.member`, `ptr->member`

**关键代码** (L482-487):
```cpp
case TokenKind::l_paren:
  // Function call
  llvm::errs() << "DEBUG [ParseExpr L478]: parseCallExpression, Base = " 
               << (Base ? std::to_string(static_cast<int>(Base->getKind())) : "NULL") << "\n";
  Base = parseCallExpression(Base);
  break;
```

**调用关系**:
```
parsePostfixExpression
  → parseCallExpression (当遇到'('时)
  → Actions.ActOnCallExpr
```

---

#### 2. parseCallExpression
**文件**: `src/Parse/ParseExpr.cpp`  
**行号**: L1292-1309（18行）

**职责**: 解析函数调用表达式的语法

**实现**:
```cpp
Expr *Parser::parseCallExpression(Expr *Fn) {
  consumeToken(); // consume '('

  llvm::SmallVector<Expr *, 8> Args = parseCallArguments();

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  return Actions.ActOnCallExpr(Fn, Args, Fn->getLocation(),
                               Tok.getLocation()).get();
}
```

**流程**:
1. 消耗 '('
2. 调用 `parseCallArguments()` 解析参数
3. 消耗 ')'
4. 调用 `Actions.ActOnCallExpr()` 进行语义分析

---

#### 3. parseCallArguments
**文件**: `src/Parse/ParseExpr.cpp`  
**行号**: L1311-1332（22行）

**职责**: 解析逗号分隔的参数列表

**实现**:
```cpp
llvm::SmallVector<Expr *, 8> Parser::parseCallArguments() {
  llvm::SmallVector<Expr *, 8> Args;

  // Empty argument list
  if (Tok.is(TokenKind::r_paren))
    return Args;

  while (true) {
    Expr *Arg = parseAssignmentExpression();
    if (!Arg) {
      emitError(DiagID::err_expected_expression);
      Arg = createRecoveryExpr(Tok.getLocation());
    }
    Args.push_back(Arg);

    if (!tryConsumeToken(TokenKind::comma))
      break;
  }

  return Args;
}
```

**特点**:
- 支持空参数列表
- 支持逗号分隔的多个参数
- 错误恢复：如果解析失败，创建recovery表达式

---

### Sema层

#### 4. ActOnCallExpr
**文件**: `src/Sema/Sema.cpp`  
**行号**: L1996-2175（约180行）

**职责**: 语义分析函数调用表达式

**关键流程**:
```cpp
ExprResult Sema::ActOnCallExpr(Expr *Fn, llvm::ArrayRef<Expr *> Args, ...) {
  // Step 1: 从DeclRefExpr获取Decl
  FunctionDecl *FD = nullptr;
  
  if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(Fn)) {
    Decl *D = DRE->getDecl();
    
    // ⚠️ BUG: Early return when D=nullptr
    if (!D) {
      auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
      return ExprResult(CE);  // ← 跳过后续模板处理
    }
    
    if (auto *FunD = llvm::dyn_cast<FunctionDecl>(D)) {
      FD = FunD;
    }
    
    // Handle function template
    if (!FD) {
      if (auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(D)) {
        FD = DeduceAndInstantiateFunctionTemplate(FTD, Args, LParenLoc);
      }
    }
  }
  
  // Step 2: Overload resolution
  if (!FD) {
    FD = ResolveOverload(Name, Args, LR);
  }
  
  // Step 3: Type check
  if (!TC.CheckCall(FD, Args, LParenLoc))
    return ExprResult::getInvalid();

  // Step 4: Create CallExpr
  auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
  // Set return type...
  return ExprResult(CE);
}
```

**已知问题**:
- ⚠️ L2094-2098: Early return导致模板推导分支无法到达
- 这是P0级别的bug，阻塞函数模板调用

---

#### 5. ResolveOverload
**文件**: `src/Sema/Sema.cpp`  
**行号**: L2781-2832（52行）

**职责**: 重载决议，从候选函数集中选择最佳匹配

**实现**:
```cpp
FunctionDecl *Sema::ResolveOverload(llvm::StringRef Name,
                                     llvm::ArrayRef<Expr *> Args,
                                     const LookupResult &Candidates) {
  OverloadCandidateSet OCS(CallLoc);
  OCS.addCandidates(Candidates);

  if (OCS.empty()) {
    Diags.report(SourceLocation(), DiagID::err_ovl_no_viable_function, Name);
    return nullptr;
  }

  auto [Result, Best] = OCS.resolve(Args);

  if (Result == OverloadResult::Success)
    return Best;

  if (Result == OverloadResult::Deleted) {
    // 处理deleted函数
    ...
  }

  if (Result == OverloadResult::NoViable) {
    // 没有可行候选
    ...
  }

  // Ambiguous
  ...
  return nullptr;
}
```

**返回结果**:
- `Success`: 找到唯一最佳匹配
- `Deleted`: 最佳匹配是deleted函数
- `NoViable`: 没有可行的候选
- `Ambiguous`: 多个同样好的候选（歧义）

---

#### 6. DeduceAndInstantiateFunctionTemplate
**文件**: `src/Sema/SemaTemplate.cpp`  
**行号**: L669-742（74行）

**职责**: 模板实参推导 + 约束检查 + 实例化

**完整流程**:
```cpp
FunctionDecl *Sema::DeduceAndInstantiateFunctionTemplate(
    FunctionTemplateDecl *FTD, llvm::ArrayRef<Expr *> Args,
    SourceLocation CallLoc) {
  
  // Step 1: 模板实参推导（SFINAE保护）
  TemplateDeductionInfo Info;
  unsigned SavedErrors = Diags.getNumErrors();
  {
    SFINAEGuard Guard(...);
    Result = Deduction->DeduceFunctionTemplateArguments(FTD, Args, Info);
  }

  if (Result != TemplateDeductionResult::Success) {
    // 报告诊断
    return nullptr;
  }

  // Step 2: 收集推导出的实参
  llvm::SmallVector<TemplateArgument, 4> DeducedArgs =
      Info.getDeducedArgs(NumParams);

  // Step 3: 检查requires-clause约束
  if (FTD->hasRequiresClause()) {
    bool Satisfied = ConstraintChecker->CheckConstraintSatisfaction(...);
    if (!Satisfied) {
      Diags.report(CallLoc, DiagID::err_concept_not_satisfied, ...);
      return nullptr;
    }
  }

  // Step 4: 实例化函数模板
  FunctionDecl *InstFD = Instantiator->InstantiateFunctionTemplate(FTD, DeducedArgs);
  
  return InstFD;
}
```

**关键特性**:
- SFINAE保护：推导失败不是硬错误
- 约束检查：C++20 concepts支持
- 完整的实例化流程

**问题**: 由于ActOnCallExpr的early return，此函数在函数调用时无法到达

---

### TypeCheck层

#### 7. CheckCall
**文件**: `src/Sema/TypeCheck.cpp`  
**行号**: L179-227（49行）

**职责**: 检查函数调用的参数类型匹配

**实现**:
```cpp
bool TypeCheck::CheckCall(FunctionDecl *F, llvm::ArrayRef<Expr *> Args,
                          SourceLocation CallLoc) {
  QualType FnType = F->getType();
  const auto *FT = llvm::dyn_cast<FunctionType>(FnType.getTypePtr());
  if (!FT)
    return false;

  unsigned NumParams = F->getNumParams();
  bool IsVariadic = FT->isVariadic();

  // 计算最少参数数量（排除有默认值的参数）
  unsigned MinParams = 0;
  for (unsigned I = 0; I < NumParams; ++I) {
    if (!F->getParamDecl(I)->getDefaultArg()) {
      MinParams = I + 1;
    }
  }

  // 检查参数数量
  if (Args.size() < MinParams) {
    Diags.report(CallLoc, DiagID::err_too_few_args, ...);
    return false;
  }
  if (!IsVariadic && Args.size() > NumParams) {
    Diags.report(CallLoc, DiagID::err_too_many_args, ...);
    return false;
  }

  // 检查每个参数的类型
  for (unsigned I = 0; I < Args.size() && I < NumParams; ++I) {
    QualType ParamType = F->getParamDecl(I)->getType();
    QualType ArgType = Args[I]->getType();

    if (!isTypeCompatible(ArgType, ParamType)) {
      Diags.report(CallLoc, DiagID::err_arg_type_mismatch, ...);
      return false;
    }
  }

  return true;
}
```

**检查项**:
1. 参数数量（考虑默认参数和variadic）
2. 每个参数的类型兼容性
3. 报告详细的诊断信息

---

## 🔗 完整调用链

```mermaid
graph TD
    A[parsePostfixExpression L479] --> B{Token == '('?}
    B -->|是| C[parseCallExpression L1292]
    B -->|否| D[其他后缀操作]
    
    C --> E[parseCallArguments L1311]
    E --> F[解析参数列表]
    F --> G[Actions.ActOnCallExpr L1996]
    
    G --> H{DRE->getDecl == nullptr?}
    H -->|是| I[⚠️ Early Return<br/>跳过模板处理]
    H -->|否| J{是FunctionDecl?}
    
    J -->|是| K[直接使用]
    J -->|否| L{是FunctionTemplateDecl?}
    
    L -->|是| M[DeduceAndInstantiateFunctionTemplate L669]
    L -->|否| N[ResolveOverload L2781]
    
    M --> O[模板实参推导]
    O --> P[约束检查]
    P --> Q[实例化函数]
    
    N --> R[重载决议]
    
    K & Q & R --> S[TC.CheckCall L179]
    S --> T[参数数量检查]
    T --> U[参数类型检查]
    U --> V[创建CallExpr]
```

---

## ⚠️ 发现的问题

### 问题1: ActOnCallExpr Early Return (P0)

**位置**: Sema.cpp L2094-2098

**现象**:
```cpp
if (!D) {
  auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
  return ExprResult(CE);  // ← 直接返回
}
// 后续的模板处理代码永远无法到达
```

**影响**:
- 当DeclRefExpr的Decl为nullptr时，直接创建无效的CallExpr
- 跳过了L2104的FunctionTemplateDecl处理
- 导致函数模板调用无法工作

**根因**:
- parseIdentifier创建了DeclRefExpr(nullptr)
- 因为FunctionTemplateDecl不是ValueDecl，dyn_cast失败

**修复方向**:
- 需要在D=nullptr时尝试其他方式恢复信息
- 或者在Parser层避免创建无效的DeclRefExpr

---

## 📊 统计信息

| 指标 | 数值 |
|------|------|
| Parser函数数 | 3 |
| Sema函数数 | 3 |
| TypeCheck函数数 | 1 |
| **总计** | **7** |
| 代码行数（估算） | ~400行 |
| 已知问题数 | 1 (P0) |

---

## ✅ 验收标准

- [x] 搜索并列出所有Parser层函数
- [x] 搜索并列出所有Sema层函数
- [x] 搜索并列出所有TypeCheck层函数
- [x] 记录每个函数的文件位置和行号
- [x] 阅读关键函数的实现
- [x] 绘制完整调用链图
- [x] 识别已知问题

---

## 🔗 下一步

**Task 2.2.2**: 模板实例化功能域的函数收集

**依赖**: Task 2.2.1已完成 ✅

---

**输出文件**: 
- 本报告: `docs/review/reports/task_2.2.1_report.md`
