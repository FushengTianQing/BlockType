# Parser层详细流程图

**功能域**: Parser (src/Parse/)  
**函数数量**: ~50个核心解析函数（系统性检查后）  
**创建时间**: 2026-04-19 23:40  
**最后更新**: 2026-04-20 00:05 - 系统性检查补充：parsePostfixExpression, parseCallArguments, parseTemplateArgument, parseTemplateArgumentList

---

## 🗺️ Parser层完整流程

```mermaid
graph TB
    subgraph Entry["入口函数"]
        A[parseTranslationUnit] --> B{Token类型?}
        B -->|EOF| C[返回]
        B -->|#| D[skipPreprocessingDirective]
        B -->|其他| E[parseDeclaration]
        
        D --> B
    end
    
    subgraph DeclParse["声明解析分支"]
        E --> F{声明类型识别}
        
        F -->|module/import/export| G[C++20模块]
        G --> G1[parseModuleDeclaration]
        G1 --> G1a[parseModuleName 模块名解析]
        G1a --> G1b[parseModulePartition 分区解析]
        G1b --> G2[parseImportDeclaration]
        G2 --> G3[parseExportDeclaration]
        
        F -->|template| H[模板声明]
        H --> H1[parseTemplateDeclaration]
        H1 --> H2[parseTemplateParameterList 模板参数]
        H2 --> H3[parseTemplateArgumentList 模板实参列表]
        H3 --> H4[parseTemplateArgument 单个实参]
        
        F -->|namespace| I[命名空间]
        I --> I1[parseNamespaceDeclaration]
        I --> I2[parseNamespaceAlias]
        
        F -->|using| J[using声明]
        J --> J1[parseUsingDeclaration]
        J --> J2[parseUsingDirective]
        J --> J3[parseUsingEnumDeclaration]
        
        F -->|class/struct/enum| K[类/结构体/枚举]
        K --> K1[parseClassDeclaration]
        K --> K2[parseStructDeclaration]
        K --> K3[parseEnumDeclaration]
        K --> K4[parseUnionDeclaration]
        
        F -->|typedef| L[typedef]
        L --> L1[parseTypedefDeclaration]
        
        F -->|默认路径| M[标准声明路径]
        M --> M1[parseDeclSpecifierSeq]
        M1 --> M2[parseDeclarator]
        
        M2 --> N{Declarator类型?}
        N -->|函数| O[buildFunctionDecl]
        N -->|变量| P[buildVarDecl]
        N -->|结构化绑定| Q[parseDecompositionDeclaration]
        
        O --> R[Actions.ActOnFunctionDecl]
        P --> S[Actions.ActOnVarDeclFull]
        Q --> T[Actions.ActOnDecompositionDecl]
        
        G1 & G2 & G3 --> B
        H1 & H2 --> B
        I1 & I2 --> B
        J1 & J2 & J3 --> B
        K1 & K2 & K3 & K4 --> B
        L1 --> B
        R --> B
        S --> B
        T --> B
        
        style G fill:#fff4e6,stroke:#ff9800
        style Q fill:#fff4e6,stroke:#ff9800
    end
    
    subgraph ExprParse["表达式解析分支"]
        U[parseExpression] --> V{表达式类型?}
        
        V -->|后缀表达式| W0[parsePostfixExpression]
        W0 --> W{后缀类型?}
        W -->|函数调用| W1[parseCallExpression]
        W -->|数组下标| W2[parseArraySubscript]
        W -->|成员访问| W3[parseMemberExpression]
        
        W1 --> W4[parseCallArguments 参数列表]
        
        V -->|Lambda| Y[parseLambdaExpression]
        Y --> Y1[parseLambdaIntroducer \[\]]
        Y1 --> Y2[parseLambdaCaptureList]
        Y2 --> Y3[parseLambdaDeclarator]
        Y3 --> Y4[parseCompoundStatement]
        V -->|折叠表达式| Z[parseFoldExpression]
        V -->|requires| AA[parseRequiresExpression]
        V -->|二元运算| BB[parseBinaryOperator]
        V -->|一元运算| CC[parseUnaryOperator]
        V -->|字面量| DD[parseLiteral]
        V -->|标识符| EE[parseIdentifier]
        
        W --> FF[Actions.ActOnCallExpr]
        X --> GG[Actions.ActOnMemberExpr]
        Y --> HH[Actions.ActOnLambdaExpr]
        Z --> II[Actions.ActOnFoldExpr]
        AA --> JJ[Actions.ActOnRequiresExpr]
        BB --> KK[Actions.ActOnBinaryOperator]
        CC --> LL[Actions.ActOnUnaryOperator]
        
        FF --> MM[返回ExprResult]
        GG --> MM
        HH --> MM
        II --> MM
        JJ --> MM
        KK --> MM
        LL --> MM
        DD --> MM
        EE --> MM
        
        style Y fill:#fff4e6,stroke:#ff9800
        style Z fill:#fff4e6,stroke:#ff9800
        style AA fill:#fff4e6,stroke:#ff9800
    end
    
    subgraph StmtParse["语句解析分支"]
        NN[parseStatement] --> OO{语句类型?}
        
        OO -->|if| PP[parseIfStatement]
        OO -->|while| QQ[parseWhileStatement]
        OO -->|for| RR[parseForStatement]
        OO -->|do-while| SS[parseDoStatement]
        OO -->|switch| TT[parseSwitchStatement]
        OO -->|case| UU[parseCaseStatement]
        OO -->|default| VV[parseDefaultStatement]
        OO -->|return| WW[parseReturnStatement]
        OO -->|break| XX[parseBreakStatement]
        OO -->|continue| YY[parseContinueStatement]
        OO -->|goto| ZZ[parseGotoStatement]
        OO -->|try-catch| AAA[parseCXXTryStatement]
        AAA --> AAA1[parseCompoundStatement try块]
        AAA1 --> AAA2{有catch?}
        AAA2 -->|是| AAA3[parseCXXCatchClause]
        AAA3 --> AAA4[parseParameterDeclaration 异常类型]
        AAA4 --> AAA5[parseCompoundStatement catch块]
        AAA2 -->|否| AAA6[err_expected_catch]
        OO -->|复合语句| BBB[parseCompoundStatement]
        OO -->|声明语句| CCC[parseDeclStmt]
        OO -->|协程| DDD[parseCoreturnStatement]
        
        PP --> EEE[Actions.ActOnIfStmt]
        QQ --> FFF[Actions.ActOnWhileStmt]
        RR --> GGG[Actions.ActOnForStmt]
        SS --> HHH[Actions.ActOnDoStmt]
        TT --> III[Actions.ActOnSwitchStmt]
        UU --> JJJ[Actions.ActOnCaseStmt]
        VV --> KKK[Actions.ActOnDefaultStmt]
        WW --> LLL[Actions.ActOnReturnStmt]
        XX --> MMM[Actions.ActOnBreakStmt]
        YY --> NNN[Actions.ActOnContinueStmt]
        ZZ --> OOO[Actions.ActOnGotoStmt]
        AAA --> PPP[Actions.ActOnCXXTryStmt]
        BBB --> QQQ[Actions.ActOnCompoundStmt]
        CCC --> RRR[Actions.ActOnDeclStmt]
        DDD --> SSS[Actions.ActOnCoreturnStmt]
        
        EEE --> TTT[返回StmtResult]
        FFF --> TTT
        GGG --> TTT
        HHH --> TTT
        III --> TTT
        JJJ --> TTT
        KKK --> TTT
        LLL --> TTT
        MMM --> TTT
        NNN --> TTT
        OOO --> TTT
        PPP --> TTT
        QQQ --> TTT
        RRR --> TTT
        SSS --> TTT
        
        style AAA fill:#fff4e6,stroke:#ff9800
        style DDD fill:#fff4e6,stroke:#ff9800
    end
    
    subgraph TypeParse["类型解析分支"]
        UUU[parseType] --> VVV{类型种类?}
        
        VVV -->|基本类型| WWW[parseBuiltinType]
        VVV -->|指针| XXX[parsePointerType]
        VVV -->|引用| YYY[parseReferenceType]
        VVV -->|数组| ZZZ[parseArrayType]
        VVV -->|函数类型| AAAA[parseFunctionType]
        VVV -->|模板特化| BBBB[parseTemplateSpecialization]
        VVV -->|decltype| CCCC[parseDecltypeType]
        VVV -->|auto| DDDD[parseAutoType]
        
        style DDDD fill:#fff4e6,stroke:#ff9800
    end
    
    %% 连接关系
    Entry --> DeclParse
    Entry --> ExprParse
    Entry --> StmtParse
    Entry --> TypeParse
    
    subgraph Legend["图例"]
        LEGEND1[C++11及以前]
        LEGEND2[C++17/20/23新特性]
        
        style LEGEND1 fill:#e1f5ff
        style LEGEND2 fill:#fff4e6,stroke:#ff9800
    end
```

---

## 📊 函数清单

### 声明解析函数（~20个）

| 函数名 | 文件位置 | 功能 | C++版本 |
|--------|---------|------|---------|
| parseModuleDeclaration | ParseDecl.cpp L723 | 模块声明 | C++20 |
| parseModuleName | ParseDecl.cpp L994 | 模块名解析（支持点号分隔） | C++20 |
| parseModulePartition | ParseDecl.cpp | 模块分区解析 | C++20 |
| parseImportDeclaration | ParseDecl.cpp L888 | import声明 | C++20 |
| parseExportDeclaration | ParseDecl.cpp L972 | export声明 | C++20 |
| parseExportBlock | ParseDecl.cpp | export块解析 | C++20 |
| parseTemplateDeclaration | ParseTemplate.cpp L37 | 模板声明 | C++11 |
| parseTemplateParameterList | ParseDecl.cpp | 模板参数列表 | C++11 |
| parseTemplateArgumentList | ParseTemplate.cpp L790 | 模板实参列表解析 | C++11 |
| parseTemplateArgument | ParseTemplate.cpp L719 | 单个模板实参解析 | C++11 |
| parseNamespaceDeclaration | ParseDecl.cpp | 命名空间声明 | C++11 |
| parseUsingDeclaration | ParseDecl.cpp | using声明 | C++11 |
| parseClassDeclaration | ParseDecl.cpp | class声明 | C++11 |
| parseTypedefDeclaration | ParseDecl.cpp | typedef声明 | C++11 |
| parseDeclSpecifierSeq | ParseDecl.cpp | 声明说明符序列 | C++11 |
| parseDeclarator | ParseDecl.cpp | 声明器 | C++11 |
| buildFunctionDecl | ParseDecl.cpp | 构建函数声明 | C++11 |
| buildVarDecl | ParseDecl.cpp | 构建变量声明 | C++11 |
| parseDecompositionDeclaration | ParseDecl.cpp | 结构化绑定声明 | C++17 |

### 表达式解析函数（~15个）

| 函数名 | 文件位置 | 功能 | C++版本 |
|--------|---------|------|---------|
| parseCallExpression | ParseExpr.cpp L1292 | 函数调用解析 | C++11 |
| parseCallArguments | ParseExpr.cpp L1311 | 参数列表解析 | C++11 |
| parsePostfixExpression | ParseExpr.cpp L479 | 后缀表达式解析（含调用/下标/成员） | C++11 |
| parseMemberExpression | ParseExpr.cpp | 成员访问 | C++11 |
| parseLambdaExpression | ParseExprCXX.cpp L148 | Lambda表达式 | C++11 |
| parseLambdaCaptureList | ParseExprCXX.cpp | Lambda捕获列表 | C++11 |
| parseFoldExpression | ParseExprCXX.cpp L317 | 折叠表达式 | C++17 |
| parseRequiresExpression | ParseExprCXX.cpp | requires表达式 | C++20 |
| parseBinaryOperator | ParseExpr.cpp | 二元运算符 | C++11 |
| parseUnaryOperator | ParseExpr.cpp | 一元运算符 | C++11 |

### 语句解析函数（~19个）

| 函数名 | 文件位置 | 功能 | C++版本 |
|--------|---------|------|---------|
| parseIfStatement | ParseStmt.cpp | if语句 | C++11 |
| parseWhileStatement | ParseStmt.cpp | while循环 | C++11 |
| parseForStatement | ParseStmt.cpp | for循环 | C++11 |
| parseSwitchStatement | ParseStmt.cpp | switch语句 | C++11 |
| parseReturnStatement | ParseStmt.cpp | return语句 | C++11 |
| parseBreakStatement | ParseStmt.cpp | break语句 | C++11 |
| parseContinueStatement | ParseStmt.cpp | continue语句 | C++11 |
| parseGotoStatement | ParseStmt.cpp | goto语句 | C++11 |
| parseCXXTryStatement | ParseStmtCXX.cpp L24 | try-catch语句 | C++11 |
| parseCXXCatchClause | ParseStmtCXX.cpp L55 | catch子句 | C++11 |
| parseCoreturnStatement | ParseStmtCXX.cpp L112 | 协程return | C++20 |

---

## 🔍 关键发现

### 1. C++新特性支持情况

✅ **已实现**:
- C++17: 结构化绑定 (`parseDecompositionDeclaration`)
- C++17: 折叠表达式 (`parseFoldExpression`)
- C++20: 模块系统 (`parseModule/Import/Export`)
- C++20: requires表达式 (`parseRequiresExpression`)
- C++20: 协程 (`parseCoreturnStatement`)

⚠️ **需要注意**:
- 这些新特性的Parser层实现完整，但Sema层语义可能不完整
- 需要结合Sema层的映射报告查看集成情况

### 2. 错误恢复机制

所有解析函数都包含错误恢复逻辑：
```cpp
// 典型模式
if (Tok.isNot(tok::r_paren)) {
    Diags.report(Loc, diag::err_expected_rparen);
    SkipUntil(tok::r_paren);  // 跳过直到找到)
}
```

### 3. 与Sema的集成点

每个解析函数的最后都会调用 `Actions.ActOnXXX`：
- `buildFunctionDecl` → `Actions.ActOnFunctionDecl`
- `parseLambdaExpression` → `Actions.ActOnLambdaExpr`
- `parseCXXTryStatement` → `Actions.ActOnCXXTryStmt`

这是Parser和Sema的关键集成点。

---

## 📝 维护说明

**如何更新此图**:
1. 新增解析函数：在对应的subgraph中添加节点
2. 修改流程：调整箭头连接
3. C++新特性：使用橙色边框样式

**相关文件**:
- [主流程图](flowchart_main.md)
- [Task 2.3映射报告](task_2.3_flowchart_mapping.md)

---

**最后更新**: 2026-04-19 23:40
