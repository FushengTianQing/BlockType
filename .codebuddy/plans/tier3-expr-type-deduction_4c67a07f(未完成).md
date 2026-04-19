---
name: tier3-expr-type-deduction
overview: 完成 Tier 3 表达式类型推导：为 InitListExpr、DesignatedInitExpr、LambdaExpr、CXXFoldExpr、PackIndexingExpr、ReflexprExpr、TemplateSpecializationExpr 补设 ExprTy。
todos:
  - id: batch1-set-types
    content: 第一批：Lambda/Fold/Reflexpr/TemplateSpec/DesignatedInit 补设 setType()
    status: pending
  - id: batch1-test
    content: 第一批编译 + 全量测试验证
    status: pending
    dependencies:
      - batch1-set-types
  - id: batch2-initlist
    content: 第二批：InitListExpr 扩展签名 + Parser 传 ExpectedType
    status: pending
    dependencies:
      - batch1-test
  - id: batch2-test
    content: 第二批编译 + 全量测试验证
    status: pending
    dependencies:
      - batch2-initlist
  - id: batch3-packindex-todo
    content: 第三批：PackIndexingExpr 标记 TODO 注释
    status: pending
    dependencies:
      - batch2-test
  - id: commit
    content: 提交 git commit
    status: pending
    dependencies:
      - batch3-packindex-todo
---

## 用户需求

完成 Tier 3 表达式（7 种）的 ExprTy 类型推导，消除所有 `ActOn*` 工厂方法中遗留的 `ExprTy = QualType()` 问题。

## 核查结论

经核查，7 种 Tier 3 表达式**全部已有完整的 Parser + Sema 委托实现**，只是 ActOn* 方法中未设置 ExprTy：

| 表达式 | Parser | Sema ActOn* | CodeGen 依赖 getType() | 类型信息来源 |
| --- | --- | --- | --- | --- |
| InitListExpr | 完整 | 无类型参数 | **是**（StructType/ArrayType 布局） | 需从上下文传入 ExpectedType |
| DesignatedInitExpr | 完整 | 无类型参数 | 否 | 同 InitListExpr |
| LambdaExpr | 完整，ReturnType 已传入 | ReturnType 已接收但未设 | 否 | 直接用 ReturnType 参数 |
| CXXFoldExpr | 完整 | 无 | 否 | Pattern->getType() |
| PackIndexingExpr | 完整 | 无 | 否 | 模板依赖，暂留空 |
| ReflexprExpr | 完整，类已有 ResultType 字段 | 未设 | 否 | void 占位（meta::info 未定义） |
| TemplateSpecializationExpr | 完整 | VD 已传入 | 否 | VD->getType()（如可用） |


## 核心功能

- 为 7 个 ActOn* 工厂方法补设 `setType()` 调用
- 其中 3 个可直接推导（Lambda/Fold/Reflexpr），2 个需利用已有参数（TemplateSpec/DesignatedInit），1 个需扩展签名（InitList），1 个暂标记 TODO（PackIndex）
- 662/662 测试保持通过

## 技术栈

C++ 编译器项目（BlockType），LLVM 基础设施，CMake 构建系统。

## 实现方案

### 分层策略：按类型推导难度分 3 批执行

**原理**：每种表达式的类型推导难度不同，按依赖关系和风险排序：

- 第一批：已有信息直接利用（Lambda/Fold/Reflexpr/TemplateSpec/DesignatedInit）— 仅修改 Sema.cpp，无需改 Parser
- 第二批：需扩展签名传类型（InitListExpr）— 需改 Parser + Sema，CodeGen 依赖
- 第三批：模板依赖类型（PackIndexingExpr）— 标记 TODO

### 第一批：直接类型推导（5 处，仅改 Sema.cpp）

#### 1. LambdaExpr — ReturnType 已传入，直接设置

```cpp
// ActOnLambdaExpr 已接收 ReturnType 参数，只需：
auto *LE = Context.create<LambdaExpr>(...);
if (!ReturnType.isNull())
  LE->setType(ReturnType);  // 有显式返回类型的 lambda 直接用
```

**注意**：当 ReturnType 为空（无 trailing-return-type），理论上应从 body 推导。但当前不实现 auto 推导，保持 ExprTy 为空作为 TODO。

#### 2. CXXFoldExpr — 从 Pattern 推导

折叠表达式的类型 = Pattern 的类型（折叠运算保持元素类型不变）。

```cpp
auto *FE = Context.create<CXXFoldExpr>(...);
if (Pattern && !Pattern->getType().isNull())
  FE->setType(Pattern->getType());
```

#### 3. ReflexprExpr — void 占位

`reflexpr` 结果类型应为 `meta::info`，但该类型尚未定义。使用 void 占位。

```cpp
auto *RE = Context.create<ReflexprExpr>(Loc, Arg);
auto *VoidTy = Context.getBuiltinType(BuiltinKind::Void);
RE->setType(QualType(VoidTy, Qualifier::None));
```

#### 4. TemplateSpecializationExpr — 从 VD 获取类型

Parser 已传入 `ValueDecl *VD`（模板声明），如果有类型信息直接用。

```cpp
auto *TSE = Context.create<TemplateSpecializationExpr>(...);
if (VD && !VD->getType().isNull())
  TSE->setType(VD->getType());
```

#### 5. DesignatedInitExpr — 从 Init 表达式获取类型

指定初始化器的类型可以从初始化表达式推导（近似值，精确推导需上下文）。

```cpp
auto *DIE = Context.create<DesignatedInitExpr>(DotLoc, Designators, Init);
if (Init && !Init->getType().isNull())
  DIE->setType(Init->getType());
```

### 第二批：InitListExpr — 扩展签名 + Parser 传类型

**这是最关键的一处**：CodeGen 的 `EmitInitListExpr` 直接依赖 `InitList->getType()` 来决定 StructType/ArrayType 布局。

#### 方案：扩展签名传入 ExpectedType

1. **Sema.h / Sema.cpp**：`ActOnInitListExpr` 新增 `QualType ExpectedType` 参数
2. **Parser**：`parseInitializerList` 新增 `QualType ExpectedType` 参数
3. **buildVarDecl**：调用 `parseInitializerList(T)` 传入变量类型
4. **parsePrimaryExpression**：调用 `parseInitializerList()` 不传类型（QualType()）

在 ActOnInitListExpr 内部：

- 如果 ExpectedType 非空，直接用
- 否则尝试推导：所有元素类型一致时创建 ConstantArrayType
- 都不行则留空

#### InitListExpr 元素类型推导逻辑

```cpp
ExprResult Sema::ActOnInitListExpr(SourceLocation LBraceLoc,
                                   llvm::ArrayRef<Expr *> Inits,
                                   SourceLocation RBraceLoc,
                                   QualType ExpectedType) {
  auto *ILE = Context.create<InitListExpr>(LBraceLoc, Inits, RBraceLoc);
  
  // 优先使用上下文提供的预期类型
  if (!ExpectedType.isNull()) {
    ILE->setType(ExpectedType);
    return ExprResult(ILE);
  }
  
  // 尝试从元素推导：所有元素类型一致时创建数组类型
  if (!Inits.empty()) {
    QualType ElemType = Inits[0]->getType();
    if (!ElemType.isNull()) {
      bool Uniform = true;
      for (auto *Init : Inits) {
        if (Init->getType().isNull() || 
            !TC.isTypeCompatible(Init->getType(), ElemType)) {
          Uniform = false;
          break;
        }
      }
      if (Uniform) {
        auto *ArrTy = Context.getIncompleteArrayType(ElemType.getTypePtr());
        ILE->setType(QualType(ArrTy, Qualifier::None));
      }
    }
  }
  
  return ExprResult(ILE);
}
```

### 第三批：PackIndexingExpr — 标记 TODO

包索引表达式类型完全依赖模板展开后的具体类型，在非模板上下文下无法推导。标记 TODO 注释。

```cpp
ExprResult Sema::ActOnPackIndexingExpr(SourceLocation Loc, Expr *Pack,
                                       Expr *Index) {
  auto *PIE = Context.create<PackIndexingExpr>(Loc, Pack, Index);
  // TODO: ExprTy depends on pack expansion result type.
  // Will be set by TemplateInstantiator during instantiation.
  return ExprResult(PIE);
}
```

## 修改文件清单

| 文件 | 修改内容 |
| --- | --- |
| `src/Sema/Sema.cpp` | 7 处 ActOn* 方法补设 setType() |
| `include/blocktype/Sema/Sema.h` | ActOnInitListExpr 签名扩展 |
| `src/Parse/ParseExpr.cpp` | parseInitializerList 签名扩展 + 传 ExpectedType |
| `src/Parse/ParseDecl.cpp` | buildVarDecl 中 parseInitializerList(T) 传变量类型 |


## 实现注意事项

- InitListExpr 的 ExpectedType 机制是渐进式的：先覆盖 buildVarDecl 路径（有类型），parsePrimaryExpression 路径留空
- InitListExpr 自动推导使用 `getIncompleteArrayType`（不指定大小），CodeGen 需要能处理此类型
- TypeCheck::isTypeCompatible 已存在，可安全使用
- 每批完成后编译 + 662 测试验证