# BlockType 编译流程 - 主流程图

**版本**: v2.0  
**创建时间**: 2026-04-19 23:35  
**说明**: 这是Phase 2 Task 2.3的输出，展示从Driver到CodeGen的完整编译流程  
**注意**: 本图是高层概览，点击各层可查看详细子图

---

## 🗺️ 完整编译流程总览

```mermaid
graph TB
    subgraph Driver["Driver层 (tools/driver.cpp)"]
        A[main] --> B[读取源文件<br/>MemoryBuffer::getFile]
        B --> C[创建基础设施]
        C --> D[SourceManager SM]
        C --> E[DiagnosticsEngine Diags]
        C --> F[ASTContext Context]
        D & E --> G[Preprocessor PP]
        G --> H[PP.enterSourceFile]
        F & E --> I[Sema S]
        G & F & I --> J[Parser P]
        J --> K[P.parseTranslationUnit]
        
        style A fill:#e1f5ff
        style K fill:#e1f5ff
    end
    
    K ==> L
    
    subgraph Parser["Parser层 (src/Parse/)"]
        L{循环直到EOF}
        L --> M{Token类型?}
        M -->|#| N[skipPreprocessingDirective]
        M -->|其他| O[parseDeclaration]
        
        O --> P{声明类型识别}
        P -->|module/import/export| Q[parseModuleDeclaration<br/>C++20模块]
        P -->|template| R[parseTemplateDeclaration]
        P -->|namespace| S[parseNamespaceDeclaration]
        P -->|using| T[parseUsingXXX]
        P -->|class/struct/enum| U[parseClass/Struct/Enum]
        P -->|typedef| V[parseTypedefDeclaration]
        P -->|默认路径| W[parseDeclSpecifierSeq]
        
        W --> X[parseDeclarator]
        X --> Y{Declarator类型?}
        Y -->|函数| Z[buildFunctionDecl]
        Y -->|变量| AA[buildVarDecl]
        Y -->|结构化绑定| BB[parseDecompositionDeclaration<br/>C++17]
        
        Z --> CC[Actions.ActOnFunctionDecl]
        AA --> DD[Actions.ActOnVarDeclFull]
        BB --> EE[Actions.ActOnDecompositionDecl]
        
        N --> L
        Q --> L
        R --> L
        S --> L
        T --> L
        U --> L
        V --> L
        CC --> L
        DD --> L
        EE --> L
        
        style Q fill:#fff4e6,stroke:#ff9800
        style BB fill:#fff4e6,stroke:#ff9800
    end
    
    CC ==> FF
    DD ==> FF
    EE ==> FF
    
    subgraph Sema_Decl["Sema层 - 声明处理"]
        FF[ActOnFunctionDecl / ActOnVarDeclFull / ActOnDecompositionDecl]
        FF --> GG[创建Decl节点]
        GG --> HH[registerDecl注册符号表]
        HH --> II[CurContext->addDecl]
        II --> JJ[返回DeclResult]
        
        style FF fill:#ffe6f0
        style JJ fill:#ffe6f0
    end
    
    JJ ==> KK
    
    subgraph Sema_Expr["Sema层 - 表达式处理"]
        KK[表达式解析分支]
        KK --> LL[ActOnCallExpr<br/>函数调用]
        KK --> MM[ActOnMemberExpr<br/>成员访问]
        KK --> NN[ActOnLambdaExpr<br/>C++11 Lambda]
        KK --> OO[ActOnBinaryOperator<br/>二元运算符]
        KK --> PP[ActOnUnaryOperator<br/>一元运算符]
        KK --> QQ[ActOnCXXThrowExpr<br/>异常throw]
        KK --> RR[ActOnCXXNamedCastExpr<br/>C++ cast]
        
        LL --> SS[名称查找 LookupName]
        MM --> SS
        NN --> TT[创建闭包类 CXXRecordDecl]
        OO --> UU[类型检查 CheckCall]
        PP --> UU
        QQ --> VV[异常语义分析]
        RR --> VV
        
        TT --> WW[创建operator()方法]
        WW --> XX[返回LambdaExpr]
        SS --> YY[重载决议 ResolveOverload]
        UU --> ZZ[隐式转换 performImplicitConversion]
        VV --> AAA[返回ExprResult]
        YY --> AAA
        ZZ --> AAA
        XX --> AAA
        
        style NN fill:#fff4e6,stroke:#ff9800
        style QQ fill:#fff4e6,stroke:#ff9800
    end
    
    AAA ==> BBB
    
    subgraph Sema_Stmt["Sema层 - 语句处理"]
        BBB[语句解析分支]
        BBB --> CCC[ActOnReturnStmt<br/>return语句]
        BBB --> DDD[ActOnIfStmt<br/>if语句]
        BBB --> EEE[ActOnWhileStmt<br/>while循环]
        BBB --> FFF[ActOnForStmt<br/>for循环]
        BBB --> GGG[ActOnSwitchStmt<br/>switch语句]
        BBB --> HHH[ActOnBreakStmt<br/>break语句]
        BBB --> III[ActOnContinueStmt<br/>continue语句]
        BBB --> JJJ[ActOnGotoStmt<br/>goto语句]
        BBB --> KKK[ActOnCXXTryStmt<br/>try-catch]
        
        CCC --> LLL[类型检查 CheckReturn]
        DDD --> MMM[条件检查 CheckCondition]
        EEE --> MMM
        FFF --> MMM
        GGG --> NNN[case值检查 CheckCaseExpression]
        HHH --> OOO[作用域检查 BreakScopeDepth]
        III --> PPP[作用域检查 ContinueScopeDepth]
        JJJ --> QQQ[label查找]
        KKK --> RRR[异常处理语义]
        
        LLL --> SSS[返回StmtResult]
        MMM --> SSS
        NNN --> SSS
        OOO --> SSS
        PPP --> SSS
        QQQ --> SSS
        RRR --> SSS
        
        style KKK fill:#fff4e6,stroke:#ff9800
    end
    
    SSS ==> TTT
    
    subgraph Sema_Template["Sema层 - 模板处理"]
        TTT[模板实例化分支]
        TTT --> UUU[DeduceAndInstantiateFunctionTemplate<br/>函数模板]
        TTT --> VVV[InstantiateClassTemplate<br/>类模板]
        TTT --> WWW[ResolveOverload<br/>重载决议]
        TTT --> XXX[CheckTemplateArguments<br/>参数检查]
        TTT --> YYY[SubstituteTemplateArguments<br/>参数替换]
        TTT --> ZZZ[CheckConstraintSatisfaction<br/>C++20约束]
        
        UUU --> AAAA[创建特化版本]
        VVV --> AAAA
        WWW --> AAAA
        XXX --> BBBB[错误诊断]
        YYY --> AAAA
        ZZZ --> BBBB
        
        AAAA --> CCCC[返回Decl/Expr]
        BBBB --> CCCC
        
        style ZZZ fill:#fff4e6,stroke:#ff9800
    end
    
    CCCC ==> DDDD
    
    subgraph TypeCheck["TypeCheck层 (src/Sema/TypeCheck.cpp)"]
        DDDD[类型检查入口]
        DDDD --> EEEE[CheckCall<br/>函数调用检查]
        DDDD --> FFFF[CheckReturn<br/>return类型检查]
        DDDD --> GGGG[CheckCondition<br/>条件表达式检查]
        DDDD --> HHHH[CheckCaseExpression<br/>case值检查]
        DDDD --> IIII[CheckDirectInitialization<br/>直接初始化]
        DDDD --> JJJJ[CheckCopyInitialization<br/>拷贝初始化]
        DDDD --> KKKK[getCommonType<br/>通用类型计算]
        DDDD --> LLLL[performImplicitConversion<br/>隐式转换]
        
        EEEE --> MMMM[类型兼容性验证]
        FFFF --> MMMM
        GGGG --> MMMM
        HHHH --> MMMM
        IIII --> MMMM
        JJJJ --> MMMM
        KKKK --> MMMM
        LLLL --> MMMM
        
        MMMM --> NNNN[返回检查结果]
        
        style DDDD fill:#e8f5e9
        style NNNN fill:#e8f5e9
    end
    
    NNNN ==> OOOO
    
    subgraph CodeGen["CodeGen层 (src/CodeGen/)"]
        OOOO[IR生成入口]
        OOOO --> PPPP[EmitFunctionDecl<br/>函数IR生成]
        OOOO --> QQQQ[EmitVarDecl<br/>变量IR生成]
        OOOO --> RRRR[EmitCallExpr<br/>调用IR生成]
        OOOO --> SSSS[EmitReturnStmt<br/>return IR生成]
        OOOO --> TTTT[EmitIfStmt<br/>if IR生成]
        OOOO --> UUUU[EmitLoopStmt<br/>循环IR生成]
        OOOO --> VVVV[EmitLambdaExpr<br/>Lambda IR生成]
        OOOO --> WWWW[EmitCXXTryStmt<br/>try-catch IR生成]
        
        PPPP --> XXXX[LLVM IR Module]
        QQQQ --> XXXX
        RRRR --> XXXX
        SSSS --> XXXX
        TTTT --> XXXX
        UUUU --> XXXX
        VVVV --> XXXX
        WWWW --> XXXX
        
        XXXX --> YYYY[优化 Passes]
        YYYY --> ZZZZ[目标代码生成]
        
        style OOOO fill:#f3e5f5
        style ZZZZ fill:#f3e5f5
    end
    
    %% 图例
    subgraph Legend["图例"]
        LEGEND1[C++11及以前特性]
        LEGEND2[C++17/20/23新特性]
        
        style LEGEND1 fill:#e1f5ff
        style LEGEND2 fill:#fff4e6,stroke:#ff9800
    end
```

---

## 📊 流程图说明

### 颜色编码

| 颜色 | 含义 | 示例 |
|------|------|------|
| 🔵 蓝色 | C++11及以前的标准特性 | 函数声明、变量声明、普通表达式 |
| 🟠 橙色边框 | C++17/20/23新特性 | 结构化绑定、Lambda、模块、异常处理 |
| 🟢 绿色 | TypeCheck层 | 所有类型检查函数 |
| 🟣 紫色 | CodeGen层 | IR生成函数 |
| ⚪ 白色 | 控制流节点 | 循环、分支判断 |

### 层级结构

```
Driver层 (1个节点)
  ↓
Parser层 (~40个函数)
  ↓
Sema层 (~155个函数)
  ├─ 声明处理 (~35个ActOn函数)
  ├─ 表达式处理 (~30个ActOn函数)
  ├─ 语句处理 (~19个ActOn函数)
  └─ 模板处理 (~14个函数)
  ↓
TypeCheck层 (~28个Check函数)
  ↓
CodeGen层 (~20个Emit函数)
```

---

## 🔗 详细子图导航

以下是各层的详细流程图，点击可查看：

### Parser层
- [📄 Parser层详细流程图](flowchart_parser_detail.md) - 40个解析函数

### Sema层
- [📄 Sema声明处理](flowchart_sema_decl.md) - 35+个ActOn函数
- [📄 Sema表达式处理](flowchart_sema_expr.md) - 30+个ActOn函数
- [📄 Sema语句处理](flowchart_sema_stmt.md) - 19个ActOn函数
- [📄 Sema模板处理](flowchart_sema_template.md) - 14个模板函数

### TypeCheck层
- [📄 TypeCheck层详细流程图](flowchart_typecheck.md) - 28个Check函数

### C++新特性
- [📄 C++17特性流程](flowchart_cpp17_features.md) - 结构化绑定、折叠表达式等
- [📄 C++20特性流程](flowchart_cpp20_features.md) - 模块、concepts、requires等
- [📄 C++23特性流程](flowchart_cpp23_features.md) - if consteval、lambda属性等

### 特殊功能域
- [📄 Lambda表达式完整流程](flowchart_lambda.md) - 从解析到IR生成
- [📄 异常处理完整流程](flowchart_exceptions.md) - try-catch-throw
- [📄 结构化绑定完整流程](flowchart_decomposition.md) - auto [a,b,c] = tuple

---

## ⚠️ 已知问题

根据Task 2.3的映射分析，发现以下问题：

### 🔴 P0问题（阻塞性）

1. **TypeCheck层与Sema层集成断裂**
   - TypeCheck的28个函数在流程中有定义，但实际调用关系不明确
   - 需要确认每个ActOn函数是否真正调用了相应的Check函数

2. **C++20模块语义完全未实现**
   - Parser层有`parseModuleDeclaration`
   - Sema层有`ActOnModuleDecl`，但只是工厂模式
   - 缺少完整的模块加载、导入、导出语义

3. **Auto类型推导未被集成**
   - `DeduceAutoType`函数存在但从未被调用
   - 需要在`ActOnVarDeclFull`中添加调用

### 🟡 P2问题（中等）

4. **名称查找分散在各处**
   - `LookupName`、`Scope::lookup`、`SymbolTable::lookup`没有统一的入口
   - 建议在流程图中明确标注名称查找的调用点

5. **部分表达式/语句处理缺失**
   - 如`parseFoldExpression`、`parseRequiresExpression`已实现但未在主流程中体现

---

## 📝 维护说明

### 如何更新此图

1. **新增功能**：在对应的subgraph中添加节点
2. **修改流程**：调整箭头连接关系
3. **添加新特性**：使用橙色边框样式 `style XXX fill:#fff4e6,stroke:#ff9800`

### 子图文件命名规范

- `flowchart_{layer}_{detail}.md` - 某层的详细图
- `flowchart_{feature}.md` - 某特性的完整流程
- 所有子图文件放在 `docs/review/reports/` 目录

---

**最后更新**: 2026-04-19 23:35  
**维护人**: AI Assistant  
**相关文档**: 
- [Task 2.3 映射报告](task_2.3_flowchart_mapping.md)
- [Phase 1 流程地图](review_task_1.4_report.md)
