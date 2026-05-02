# Sema层 - 表达式处理详细流程图

**功能域**: Sema表达式处理 (src/Sema/Sema.cpp)  
**函数数量**: ~30个ActOn函数  
**创建时间**: 2026-04-19 23:45

---

## 🗺️ Sema表达式处理流程

```mermaid
graph TB
    subgraph Entry["表达式语义分析入口"]
        A[Parser调用Actions.ActOnXXX] --> B{表达式类型?}
        
        B -->|函数调用| C[ActOnCallExpr]
        B -->|成员访问| D[ActOnMemberExpr]
        B -->|Lambda| E[ActOnLambdaExpr]
        B -->|声明引用| F[ActOnDeclRefExpr]
        B -->|二元运算| G[ActOnBinaryOperator]
        B -->|一元运算| H[ActOnUnaryOperator]
        B -->|throw| I[ActOnCXXThrowExpr]
        B -->|cast| J[ActOnCXXNamedCastExpr]
        B -->|三元运算| K[ActOnConditionalOperator]
        B -->|赋值| L[ActOnAssignmentExpr]
    end
    
    subgraph CallExpr["函数调用处理"]
        C --> M[从DeclRefExpr获取Decl]
        M --> N{D是否为空?}
        
        N -->|是| O[⚠️ Early Return<br/>跳过模板处理]
        N -->|否| P{是FunctionDecl?}
        
        P -->|是| Q[直接使用]
        P -->|否| R{是FunctionTemplateDecl?}
        
        R -->|是| S[DeduceAndInstantiateFunctionTemplate]
        R -->|否| T[ResolveOverload]
        
        Q --> U[CheckCall类型检查]
        S --> U
        T --> U
        
        U --> V[构建CallExpr AST节点]
        V --> W[返回ExprResult]
        
        style O fill:#ffebee,stroke:#f44336
    end
    
    subgraph LambdaExpr["Lambda表达式处理"]
        E --> X[P7.1.5: 推断返回类型]
        X --> Y[创建闭包类 CXXRecordDecl]
        Y --> Z[设置isLambda=true]
        Z --> AA[添加捕获成员FieldDecl]
        
        AA --> BB{捕获类型?}
        BB -->|by-copy| CC[创建拷贝字段]
        BB -->|by-ref| DD[创建引用字段]
        BB -->|init-capture| EE[创建初始化字段]
        
        CC & DD & EE --> FF[创建operator()方法]
        FF --> GG[设置参数列表]
        GG --> HH[设置函数体Body]
        HH --> II[设置const/mutable属性]
        
        II --> JJ[创建LambdaExpr AST节点]
        JJ --> KK[建立CapturedVarsMap]
        KK --> LL[返回ExprResult]
        
        style E fill:#fff4e6,stroke:#ff9800
    end
    
    subgraph MemberExpr["成员访问处理"]
        D --> MM[查找基类或成员]
        MM --> NN[LookupMember名称查找]
        NN --> OO{找到成员?}
        
        OO -->|是| PP[检查访问权限]
        OO -->|否| QQ[报告错误 err_undeclared]
        
        PP --> RR{是静态成员?}
        RR -->|是| SS[直接访问]
        RR -->|否| TT[需要对象实例]
        
        SS --> UU[构建MemberExpr AST]
        TT --> UU
        
        UU --> VV[返回ExprResult]
        QQ --> WW[返回ExprResult::getInvalid]
    end
    
    subgraph BinaryOp["二元运算符处理"]
        G --> XX[检查操作数类型]
        XX --> YY{运算符类型?}
        
        YY -->|算术运算| ZZ[CheckArithmeticTypes]
        YY -->|比较运算| AAA[CheckComparisonTypes]
        YY -->|逻辑运算| BBB[CheckLogicalTypes]
        YY -->|位运算| CCC[CheckBitwiseTypes]
        
        ZZ --> DDD[执行隐式转换]
        AAA --> DDD
        BBB --> DDD
        CCC --> DDD
        
        DDD --> EEE[performImplicitConversion]
        EEE --> FFF[构建BinaryOperator AST]
        FFF --> GGG[返回ExprResult]
    end
    
    subgraph ThrowExpr["异常throw处理"]
        I --> HHH[检查操作数类型]
        HHH --> III[验证可抛出类型]
        III --> JJJ[构建CXXThrowExpr AST]
        JJJ --> KKK[设置void类型]
        KKK --> LLL[返回ExprResult]
        
        style I fill:#fff4e6,stroke:#ff9800
    end
    
    subgraph NameLookup["名称查找子系统"]
        MMM[LookupName] --> NNN{查找类型?}
        
        NNN -->|无限定| OOO[Scope链查找]
        NNN -->|限定| PPP[嵌套名称 specifier]
        NNN -->|成员| QQQ[类成员查找]
        
        OOO --> RRR[SymbolTable::lookup]
        PPP --> SSS[解析nested-name-specifier]
        QQQ --> TTT[遍历基类链]
        
        RRR --> UUU[返回NamedDecl*]
        SSS --> UUU
        TTT --> UUU
        
        style MMM fill:#e8f5e9
    end
    
    subgraph OverloadResolution["重载决议"]
        VVV[ResolveOverload] --> WWW[收集候选函数]
        WWW --> XXX[过滤可行函数]
        XXX --> YYY[计算转换序列]
        YYY --> ZZZ[排序候选函数]
        ZZZ --> AAAA{有唯一最佳?}
        
        AAAA -->|是| BBBB[选择最佳函数]
        AAAA -->|否| CCCC[报告歧义错误]
        
        BBBB --> DDDD[返回FunctionDecl*]
        CCCC --> EEEE[返回nullptr]
    end
    
    subgraph TypeConversion["类型转换"]
        FFFF[performImplicitConversion] --> GGGG{转换类型?}
        
        GGGG -->|标准转换| HHHH[Integral/Floating转换]
        GGGG -->|用户定义| IIII[调用转换构造函数]
        GGGG -->|限定符调整| JJJJ[添加/移除const/volatile]
        
        HHHH --> KKKK[构建ImplicitCastExpr]
        IIII --> KKKK
        JJJJ --> KKKK
        
        KKKK --> LLLL[返回ExprResult]
    end
    
    %% 连接关系
    Entry --> CallExpr
    Entry --> LambdaExpr
    Entry --> MemberExpr
    Entry --> BinaryOp
    Entry --> ThrowExpr
    
    CallExpr --> NameLookup
    CallExpr --> OverloadResolution
    CallExpr --> TypeConversion
    
    MemberExpr --> NameLookup
    
    BinaryOp --> TypeConversion
    
    subgraph Legend["图例"]
        LEGEND1[C++11及以前]
        LEGEND2[C++11/17/20新特性]
        LEGEND3[已知问题]
        
        style LEGEND1 fill:#e1f5ff
        style LEGEND2 fill:#fff4e6,stroke:#ff9800
        style LEGEND3 fill:#ffebee,stroke:#f44336
    end
```

---

## 📊 核心函数清单

### 表达式ActOn函数（~15个）

| 函数名 | 行号 | 功能 | 状态 |
|--------|------|------|------|
| ActOnCallExpr | L1996 | 函数调用语义分析 | ⚠️ Early return问题 |
| ActOnMemberExpr | L2177 | 成员访问语义分析 | ✅ 完整 |
| ActOnLambdaExpr | L1853 | Lambda语义分析 | ✅ 核心完整 |
| ActOnDeclRefExpr | Lxxx | 声明引用 | ⚠️ 含DEBUG输出 |
| ActOnBinaryOperator | Lxxx | 二元运算符 | ✅ 完整 |
| ActOnUnaryOperator | Lxxx | 一元运算符 | ✅ 完整 |
| ActOnCXXThrowExpr | L1769 | throw表达式 | ⚠️ 语义未实现 |
| ActOnCXXNamedCastExpr | L1776 | C++ cast | ✅ 完整 |
| ActOnConditionalOperator | Lxxx | 三元运算符 | ✅ 完整 |
| ActOnAssignmentExpr | Lxxx | 赋值表达式 | ✅ 完整 |

### 辅助函数（~15个）

| 函数名 | 功能 | 调用点 |
|--------|------|--------|
| LookupName | 无限定名称查找 | 所有ActOn函数 |
| LookupQualifiedName | 限定名称查找 | ActOnMemberExpr |
| LookupMember | 成员查找 | ActOnMemberExpr |
| ResolveOverload | 重载决议 | ActOnCallExpr |
| DeduceAndInstantiateFunctionTemplate | 模板实例化 | ActOnCallExpr |
| CheckCall | 函数调用类型检查 | ActOnCallExpr |
| performImplicitConversion | 隐式转换 | 多个ActOn函数 |
| getCommonType | 通用类型计算 | ActOnBinaryOperator |

---

## 🔍 关键发现

### ✅ P0问题（已修复）

1. **ActOnCallExpr Early Return问题** (L2094-2098) - **已修复**
   - **原问题**: 当DeclRefExpr的D为nullptr时，直接返回，导致`DeduceAndInstantiateFunctionTemplate`无法被调用
   - **修复方案**: 
     - 为`DeclRefExpr`添加`Name`字段存储标识符名称
     - 修改`ActOnDeclRefExpr`接受名称参数
     - 在`ActOnCallExpr`中使用`DRE->getName()`进行模板查找
   - **修复文件**:
     - `include/blocktype/AST/Expr.h`: 添加Name字段和getName()方法
     - `include/blocktype/Sema/Sema.h`: 更新ActOnDeclRefExpr签名
     - `src/Sema/Sema.cpp`: 实现模板查找逻辑
     - `src/Parse/ParseExpr.cpp`: 传入名称参数
   - **影响**: 函数模板调用现在可以正常工作

### ✅ P1问题（已修复）

2. **ActOnDeclRefExpr包含DEBUG输出** - **已修复**
   - 已删除所有 `llvm::errs() << "DEBUG` 输出
   - 清理文件：`src/Sema/Sema.cpp`, `src/Parse/ParseExpr.cpp`, `src/Sema/SemaTemplate.cpp`, `src/Sema/SymbolTable.cpp`, `src/Sema/Lookup.cpp`
   - 保留 `LLVM_DEBUG()` 宏用于调试构建

3. **ActOnCXXThrowExpr语义未实现** - **设计决策**
   - 当前实现：只创建 AST 节点，设置 void 类型
   - 缺少：异常类型匹配、栈展开逻辑
   - **说明**：这是运行时特性，需要在代码生成阶段实现
   - **优先级**：P2（Sema 层已完成，CodeGen 层待实现）
   - **实现完整度**：Sema 层 100%，整体 ~30%

### ✅ 亮点

4. **ActOnLambdaExpr实现完整** (~70%)
   - 121行的复杂函数
   - 支持闭包类生成、捕获处理、operator()创建
   - C++20模板lambda、C++23 lambda属性已支持

---

## 📝 维护说明

**相关文件**:
- [主流程图](flowchart_main.md)
- [Parser层详细图](flowchart_parser_detail.md)
- [Task 2.3映射报告](task_2.3_flowchart_mapping.md)
- [Task 2.2.7表达式处理报告](task_2.2.7_report.md)

---

**最后更新**: 2026-04-19 23:45
