# Task 2.2.10: Lambda表达式功能域 - 函数清单

**任务ID**: Task 2.2.10  
**功能域**: Lambda表达式 (Lambda Expressions)  
**执行时间**: 2026-04-19 21:30-21:50  
**状态**: ✅ DONE

---

## 📊 扫描结果总览

| 层级 | 文件数 | 函数数 | 说明 |
|------|--------|--------|------|
| Sema层 | 1个文件 | 1个函数 | ActOnLambdaExpr |
| Parser层 | 1个文件 | 2个函数 | parseLambdaExpression, parseLambdaCaptureList |
| AST类 | 1个文件 | 1个类 | LambdaExpr |
| **总计** | **3个文件** | **3个函数 + 1个类** | - |

---

## 🔍 核心函数清单

### 1. Sema::ActOnLambdaExpr - Lambda表达式处理（超复杂函数）

**文件**: `src/Sema/Sema.cpp`  
**行号**: L1853-1973  
**类型**: `ExprResult Sema::ActOnLambdaExpr(SourceLocation Loc, llvm::SmallVectorImpl<LambdaCapture> &Captures, llvm::ArrayRef<ParmVarDecl *> Params, Stmt *Body, bool IsMutable, QualType ReturnType, SourceLocation LBraceLoc, SourceLocation RBraceLoc, TemplateParameterList *TemplateParams, AttributeListDecl *Attrs)`

**功能说明**:
处理Lambda表达式的语义分析，包括闭包类生成、捕获变量处理、operator()方法创建等

**实现代码**（121行，分为5个主要步骤）:

```cpp
ExprResult Sema::ActOnLambdaExpr(SourceLocation Loc,
                                 llvm::SmallVectorImpl<LambdaCapture> &Captures,
                                 llvm::ArrayRef<ParmVarDecl *> Params,
                                 Stmt *Body, bool IsMutable,
                                 QualType ReturnType,
                                 SourceLocation LBraceLoc,
                                 SourceLocation RBraceLoc,
                                 TemplateParameterList *TemplateParams,
                                 AttributeListDecl *Attrs) {
  // Step 1: P7.1.5: Infer return type if not specified
  if (ReturnType.isNull() && Body) {
    // Simple return type deduction: look for return statements in body
    // For now, just check if there's a return with an integer literal
    // TODO: Implement proper return type deduction
    if (auto *CS = llvm::dyn_cast<CompoundStmt>(Body)) {
      for (auto *S : CS->getBody()) {
        if (auto *RS = llvm::dyn_cast<ReturnStmt>(S)) {
          if (auto *RetExpr = RS->getRetValue()) {
            ReturnType = RetExpr->getType();
            break; // Use the first return statement's type
          }
        }
      }
    }
    // If still null, default to void
    if (ReturnType.isNull()) {
      ReturnType = Context.getVoidType();
    }
  }
  
  // Step 2: P7.1.5: Create closure class for lambda
  static unsigned LambdaCounter = 0;
  std::string ClosureName = "__lambda_" + std::to_string(++LambdaCounter);
  
  // 2.1. Create the closure class (anonymous class)
  auto *ClosureClass = Context.create<CXXRecordDecl>(Loc, ClosureName, TagDecl::TK_class);
  ClosureClass->setIsLambda(true);
  
  // P7.1.5 Fix: Mark closure class as complete definition
  // Lambda closure classes are always complete (they have all members defined)
  ClosureClass->setCompleteDefinition(true);
  
  // 2.2. Add capture members to the closure class
  for (auto &Capture : Captures) {
    // P7.1.5 Phase 1: Infer capture type from context
    QualType CaptureType;
    
    if (Capture.Kind == LambdaCapture::InitCopy && Capture.InitExpr) {
      // Init capture: [x = expr] - type from initialization expression
      CaptureType = Capture.InitExpr->getType();
    } else {
      // Named capture: [x] or [&x] - lookup in current scope
      NamedDecl *CapturedDecl = LookupName(Capture.Name);
      Capture.CapturedDecl = CapturedDecl;  // Store for CodeGen
      if (CapturedDecl) {
        if (auto *VD = llvm::dyn_cast<VarDecl>(CapturedDecl)) {
          CaptureType = VD->getType();
          // If captured by reference, keep as reference type
          // If captured by copy, use the value type
          if (Capture.Kind == LambdaCapture::ByCopy) {
            // For by-copy, we might want to strip references
            // For now, keep as-is (TODO: implement proper decay)
          }
        } else {
          // Fallback: not a variable, use int
          CaptureType = Context.getIntType();
        }
      } else {
        // Variable not found in scope, use int as fallback
        CaptureType = Context.getIntType();
      }
    }
    
    auto *Field = Context.create<FieldDecl>(Capture.Loc, Capture.Name, CaptureType);
    ClosureClass->addMember(Field);
    ClosureClass->addField(Field);  // Also add to Fields array for CodeGen
  }
  
  // Step 3: Create operator() method
  // Use the ReturnType from lambda expression (or void if not specified)
  QualType RetTy = ReturnType.isNull() ? Context.getVoidType() : ReturnType;
  QualType OpCallType = QualType(Context.getFunctionType(RetTy.getTypePtr(),
                                                          llvm::ArrayRef<const Type *>(), false));
  
  auto *CallOp = Context.create<CXXMethodDecl>(Loc, "operator()", OpCallType,
                                                Params, ClosureClass,
                                                Body /* Lambda body */,
                                                false /* isStatic */,
                                                !IsMutable /* isConst */,
                                                false /* isVolatile */,
                                                false /* isVirtual */);
  CallOp->setParent(ClosureClass);
  ClosureClass->addMethod(CallOp);
  
  // Step 4: Register closure class in current scope
  if (CurContext) {
    CurContext->addDecl(ClosureClass);
  }
  
  // Step 5: Create LambdaExpr with closure class
  auto *LE = Context.create<LambdaExpr>(Loc, ClosureClass, Captures, Params, Body,
                                         IsMutable, ReturnType, LBraceLoc, RBraceLoc,
                                         TemplateParams, Attrs);
  
  // Set lambda expression type to the closure class type
  QualType LambdaType = Context.getRecordType(ClosureClass);
  LE->setType(LambdaType);
  
  // P7.1.5: Build captured variable to field index mapping
  unsigned FieldIndex = 0;
  for (const auto &Capture : Captures) {
    if (Capture.CapturedDecl) {
      if (auto *VD = llvm::dyn_cast<VarDecl>(Capture.CapturedDecl)) {
        LE->setCapturedVar(VD, FieldIndex);
      }
    }
    ++FieldIndex;
  }
  
  return ExprResult(LE);
}
```

**关键特性**:
- ✅ **完整的闭包类生成**：创建匿名CXXRecordDecl作为闭包类型
- ✅ **捕获变量处理**：支持by-copy/by-ref/init-capture三种方式
- ✅ **operator()方法创建**：将lambda body转换为成员函数
- ✅ **返回类型推导**：简单实现（查找第一个return语句的类型）
- ✅ **mutable支持**：控制operator()的const属性
- ✅ **C++20模板lambda**：支持template parameters
- ✅ **C++23 lambda属性**：支持[[attr]]
- ✅ **捕获变量映射**：建立VarDecl到field index的映射供CodeGen使用

**复杂度**: 🔴 **121行**，是Sema中最复杂的表达式处理函数之一

**实现流程**:
```
输入: Captures, Params, Body, IsMutable, ReturnType
  ↓
Step 1: 推导返回类型（如果未指定）
  ↓
Step 2: 创建闭包类（__lambda_N）
  ├─ 设置isLambda=true
  ├─ 设置completeDefinition=true
  └─ 为每个capture添加field成员
  ↓
Step 3: 创建operator()方法
  ├─ 函数类型: ReturnType(Params...)
  ├─ const属性: !IsMutable
  └─ 将body作为方法体
  ↓
Step 4: 注册闭包类到当前作用域
  ↓
Step 5: 创建LambdaExpr节点
  ├─ 设置类型为闭包类类型
  └─ 建立captured var → field index映射
  ↓
输出: LambdaExpr
```

---

### 2. Parser::parseLambdaExpression - Lambda表达式解析

**文件**: `src/Parse/ParseExprCXX.cpp`  
**行号**: L148-264  
**类型**: `Expr *Parser::parseLambdaExpression()`

**功能说明**:
解析完整的lambda表达式语法

**支持的语法**:
```cpp
lambda-expression ::= 
    [[attr]]? '[' capture-list ']' template-params? '(' params ')' [[attr]]? mutable? -> ret-type? '{' body '}'

capture-list ::= 
    capture-default
  | capture, capture-list

capture ::= 
    identifier
  | & identifier
  | identifier = init-expr
  | this
  | *this

capture-default ::= 
    =   (copy all)
  | &   (ref all)
```

**实现流程**:
```cpp
Expr *Parser::parseLambdaExpression() {
  SourceLocation LambdaLoc = Tok.getLocation();

  // Step 1: C++23: Parse leading attributes [[attr]] before [captures]
  AttributeListDecl *Attrs = nullptr;
  if (Tok.is(TokenKind::l_square) && NextTok.is(TokenKind::l_square)) {
    Attrs = parseAttributeSpecifier(LambdaLoc);
  }

  consumeToken(); // consume '['

  // Step 2: Parse capture list
  llvm::SmallVector<LambdaCapture, 4> Captures;
  if (!Tok.is(TokenKind::r_square)) {
    Captures = parseLambdaCaptureList();
  }

  if (!tryConsumeToken(TokenKind::r_square)) {
    emitError(DiagID::err_expected);
    return createRecoveryExpr(LambdaLoc);
  }

  // Step 3: C++20: Parse template parameters <...> after ] and before (
  TemplateParameterList *TemplateParams = nullptr;
  if (Tok.is(TokenKind::kw_template)) {
    SourceLocation TemplateLoc = Tok.getLocation();
    consumeToken();

    if (!tryConsumeToken(TokenKind::less)) {
      emitError(DiagID::err_expected);
      return createRecoveryExpr(LambdaLoc);
    }

    SourceLocation LAngleLoc = Tok.getLocation();
    llvm::SmallVector<NamedDecl *, 8> TParams;
    parseTemplateParameters(TParams);

    if (!tryConsumeToken(TokenKind::greater)) {
      emitError(DiagID::err_expected);
      skipUntil({TokenKind::greater, TokenKind::l_paren, TokenKind::l_brace});
      tryConsumeToken(TokenKind::greater);
    }
    SourceLocation RAngleLoc = Tok.getLocation();

    TemplateParams = new TemplateParameterList(TemplateLoc, LAngleLoc,
                                               RAngleLoc, TParams);
  }

  // Step 4: Parse parameter list (optional)
  llvm::SmallVector<ParmVarDecl *, 4> Params;
  unsigned ParamIndex = 0;
  if (Tok.is(TokenKind::l_paren)) {
    consumeToken();
    if (!Tok.is(TokenKind::r_paren)) {
      while (Tok.isNot(TokenKind::r_paren) && Tok.isNot(TokenKind::eof)) {
        ParmVarDecl *Param = parseParameterDeclaration(ParamIndex);
        if (Param != nullptr) {
          Params.push_back(Param);
          ++ParamIndex;
        }
        if (!tryConsumeToken(TokenKind::comma)) {
          break;
        }
      }
    }
    if (!tryConsumeToken(TokenKind::r_paren)) {
      emitError(DiagID::err_expected_rparen);
    }
  }

  // Step 5: C++23: Parse trailing attributes [[attr]] after (params)
  if (Tok.is(TokenKind::l_square) && NextTok.is(TokenKind::l_square)) {
    AttributeListDecl *TrailingAttrs = parseAttributeSpecifier(Tok.getLocation());
    if (!Attrs) {
      Attrs = TrailingAttrs;
    }
  }

  // Step 6: Parse mutable (optional)
  bool IsMutable = false;
  if (Tok.is(TokenKind::kw_mutable)) {
    IsMutable = true;
    consumeToken();
  }

  // Step 7: Parse return type (optional)
  QualType ReturnType;
  if (Tok.is(TokenKind::arrow)) {
    consumeToken();
    ReturnType = parseType();
  }

  // Step 8: Parse body
  if (!Tok.is(TokenKind::l_brace)) {
    emitError(DiagID::err_expected_lbrace);
    return createRecoveryExpr(LambdaLoc);
  }

  SourceLocation LBraceLoc = Tok.getLocation();
  
  // P7.1.5: Enter lambda scope for proper capture variable scoping
  Actions.PushScope(ScopeFlags::LambdaScope);
  Stmt *Body = parseCompoundStatement();
  Actions.PopScope();
  
  if (Body == nullptr) {
    Body = Actions.ActOnNullStmt(Tok.getLocation()).get();
  }
  SourceLocation RBraceLoc = Tok.getLocation();

  // Step 9: Call Sema to process lambda
  return Actions.ActOnLambdaExpr(LambdaLoc, Captures, Params, Body,
                                 IsMutable, ReturnType, LBraceLoc, RBraceLoc,
                                 TemplateParams, Attrs).get();
}
```

**关键特性**:
- ✅ **C++23属性支持**：leading和trailing attributes
- ✅ **C++20模板lambda**：`[]<typename T>(T x) { ... }`
- ✅ **Lambda作用域管理**：PushScope/PopScope确保正确的捕获变量作用域
- ✅ **完整的错误恢复**：多处检查并提供诊断

**复杂度**: 🔴 **117行**，是Parser中较复杂的表达式解析

---

### 3. Parser::parseLambdaCaptureList - 捕获列表解析

**文件**: `src/Parse/ParseExprCXX.cpp`  
**行号**: L266-311  
**类型**: `llvm::SmallVector<LambdaCapture, 4> Parser::parseLambdaCaptureList()`

**功能说明**:
解析lambda的捕获列表

**支持的捕获形式**:
```cpp
[x]           // by copy
[&x]          // by reference
[x = expr]    // init capture
[=]           // default capture by copy (consume only)
[&]           // default capture by reference (not fully implemented)
[this]        // capture this pointer (not shown in code)
[*this]       // capture *this by copy (not shown in code)
```

**实现代码**:
```cpp
llvm::SmallVector<LambdaCapture, 4> Parser::parseLambdaCaptureList() {
  llvm::SmallVector<LambdaCapture, 4> Captures;

  while (Tok.isNot(TokenKind::r_square)) {
    LambdaCapture Capture;
    SourceLocation CaptureLoc = Tok.getLocation();

    // Check for default capture
    if (Tok.is(TokenKind::amp)) {
      // & = capture by reference
      consumeToken();
      if (Tok.is(TokenKind::identifier)) {
        Capture.Kind = LambdaCapture::ByRef;
        Capture.Name = Tok.getText();
        Capture.Loc = CaptureLoc;
        consumeToken();
      }
    } else if (Tok.is(TokenKind::equal)) {
      // = = capture by copy (default)
      consumeToken();
      continue;  // ← Just consume, don't add to Captures
    } else if (Tok.is(TokenKind::identifier)) {
      // identifier
      Capture.Kind = LambdaCapture::ByCopy;
      Capture.Name = Tok.getText();
      Capture.Loc = CaptureLoc;
      consumeToken();

      // Check for init-capture: identifier = expr
      if (Tok.is(TokenKind::equal)) {
        consumeToken();
        Capture.Kind = LambdaCapture::InitCopy;
        Capture.InitExpr = parseExpression();
      }
    }

    Captures.push_back(Capture);

    // Check for comma
    if (!tryConsumeToken(TokenKind::comma)) {
      break;
    }
  }

  return Captures;
}
```

**关键特性**:
- ✅ 支持by-copy: `[x]`
- ✅ 支持by-reference: `[&x]`
- ✅ 支持init-capture: `[x = expr]`
- ⚠️ **默认捕获不完整**：`[=]`只consume不记录，`[&]`未实现
- ⚠️ **缺少this捕获**：`[this]`和`[*this]`未实现

---

## 📦 AST类定义

### LambdaExpr类

**文件**: `include/blocktype/AST/Expr.h`  
**行号**: L1125-1187

**成员变量**:
```cpp
class LambdaExpr : public Expr {
  CXXRecordDecl *ClosureClass;  // P7.1.5: The closure type
  llvm::SmallVector<LambdaCapture, 4> Captures;
  llvm::SmallVector<ParmVarDecl *, 4> Params;
  Stmt *Body;  // CompoundStmt
  bool IsMutable = false;
  QualType ReturnType;
  SourceLocation LBraceLoc;
  SourceLocation RBraceLoc;
  TemplateParameterList *TemplateParams = nullptr; // C++20: lambda-template
  class AttributeListDecl *Attrs = nullptr;         // C++23: lambda attributes
  
  // P7.1.5: Map from captured VarDecl to field index in closure
  llvm::DenseMap<const class VarDecl *, unsigned> CapturedVarsMap;
};
```

**方法**:
- `getCaptures()`: 获取捕获列表
- `getParams()`: 获取参数列表
- `getBody()`: 获取函数体
- `isMutable()`: 是否mutable
- `getReturnType()`: 获取返回类型
- `getTemplateParameters()`: 获取模板参数（C++20）
- `getAttributes()`: 获取属性（C++23）
- `getClosureClass()`: 获取闭包类
- `getCapturedVarsMap()`: 获取捕获变量映射
- `setCapturedVar(VD, FieldIndex)`: 设置捕获变量映射
- `isTypeDependent()`: 返回false（lambda类型不依赖）

---

### LambdaCapture结构体（推测）

**位置**: 可能在`include/blocktype/AST/Expr.h`或其他头文件中

**推测结构**:
```cpp
struct LambdaCapture {
  enum CaptureKind {
    ByCopy,     // [x]
    ByRef,      // [&x]
    InitCopy,   // [x = expr]
    This,       // [this]
    StarThis    // [*this]
  };
  
  CaptureKind Kind;
  llvm::StringRef Name;
  SourceLocation Loc;
  Expr *InitExpr = nullptr;        // For init-capture
  NamedDecl *CapturedDecl = nullptr; // Resolved declaration (set by Sema)
};
```

---

## 🔄 完整调用链图

```mermaid
graph TB
    subgraph Lexer_Layer [Lexer层]
        L1['[' token]
        L2[kw_template token]
        L3[kw_mutable token]
    end
    
    subgraph Parser_Layer [Parser层]
        P1[parseLambdaExpression<br/>L148]
        P2[parseLambdaCaptureList<br/>L266]
        P3[parseAttributeSpecifier<br/>attributes]
        P4[parseTemplateParameters<br/>C++20]
        P5[parseParameterDeclaration<br/>params]
        P6[parseCompoundStatement<br/>body]
    end
    
    subgraph Sema_Layer [Sema层]
        S1[ActOnLambdaExpr<br/>L1853]
    end
    
    subgraph Scope_Management [作用域管理]
        SM1[PushScope LambdaScope]
        SM2[PopScope]
    end
    
    subgraph AST_Creation [AST创建]
        A1[CXXRecordDecl<br/>closure class]
        A2[FieldDecl<br/>capture fields]
        A3[CXXMethodDecl<br/>operator]
        A4[LambdaExpr]
    end
    
    L1 --> P1
    L2 --> P1
    L3 --> P1
    
    P1 --> P2
    P1 --> P3
    P1 --> P4
    P1 --> P5
    P1 --> P6
    
    P1 --> SM1
    P6 --> SM2
    
    P1 --> S1
    
    S1 --> A1
    A1 --> A2
    A1 --> A3
    S1 --> A4
    
    style S1 fill:#ff9999
    style P1 fill:#ffcccc
    style A1 fill:#99ccff
```

---

## ⚠️ 发现的问题

### P1问题 #1: 返回类型推导过于简化

**位置**: `ActOnLambdaExpr` L1863-1881

**当前实现**:
```cpp
if (ReturnType.isNull() && Body) {
  // Simple return type deduction: look for return statements in body
  // For now, just check if there's a return with an integer literal
  // TODO: Implement proper return type deduction
  if (auto *CS = llvm::dyn_cast<CompoundStmt>(Body)) {
    for (auto *S : CS->getBody()) {
      if (auto *RS = llvm::dyn_cast<ReturnStmt>(S)) {
        if (auto *RetExpr = RS->getRetValue()) {
          ReturnType = RetExpr->getType();
          break; // Use the first return statement's type
        }
      }
    }
  }
  // If still null, default to void
  if (ReturnType.isNull()) {
    ReturnType = Context.getVoidType();
  }
}
```

**问题**:
1. **只检查顶层return语句**：不会递归检查嵌套的if/while等
2. **只用第一个return的类型**：如果有多个return且类型不同，会出错
3. **未处理decltype(auto)**：C++14的decltype(auto)返回类型
4. **未处理多分支一致性检查**：应该验证所有return路径类型一致

**建议修复**:
```cpp
QualType Sema::deduceLambdaReturnType(Stmt *Body) {
  llvm::SmallVector<QualType, 4> ReturnTypes;
  
  // Recursively collect all return types
  collectReturnTypes(Body, ReturnTypes);
  
  if (ReturnTypes.empty()) {
    return Context.getVoidType();
  }
  
  // Check all return types are the same
  QualType CommonType = ReturnTypes[0];
  for (size_t i = 1; i < ReturnTypes.size(); ++i) {
    if (!TC.isSameType(CommonType, ReturnTypes[i])) {
      Diags.report(..., DiagID::err_inconsistent_return_types);
      return QualType();
    }
  }
  
  return CommonType;
}

void Sema::collectReturnTypes(Stmt *S, llvm::SmallVectorImpl<QualType> &Types) {
  if (auto *RS = llvm::dyn_cast<ReturnStmt>(S)) {
    if (auto *RetExpr = RS->getRetValue()) {
      Types.push_back(RetExpr->getType());
    } else {
      Types.push_back(Context.getVoidType());
    }
  } else if (auto *CS = llvm::dyn_cast<CompoundStmt>(S)) {
    for (auto *SubStmt : CS->getBody()) {
      collectReturnTypes(SubStmt, Types);
    }
  } else if (auto *IS = llvm::dyn_cast<IfStmt>(S)) {
    collectReturnTypes(IS->getThen(), Types);
    if (IS->getElse()) {
      collectReturnTypes(IS->getElse(), Types);
    }
  }
  // Handle other control flow statements...
}
```

---

### P2问题 #2: 捕获类型推断不完整

**位置**: `ActOnLambdaExpr` L1896-1929

**当前实现**:
```cpp
for (auto &Capture : Captures) {
  QualType CaptureType;
  
  if (Capture.Kind == LambdaCapture::InitCopy && Capture.InitExpr) {
    // Init capture: [x = expr] - type from initialization expression
    CaptureType = Capture.InitExpr->getType();
  } else {
    // Named capture: [x] or [&x] - lookup in current scope
    NamedDecl *CapturedDecl = LookupName(Capture.Name);
    Capture.CapturedDecl = CapturedDecl;
    if (CapturedDecl) {
      if (auto *VD = llvm::dyn_cast<VarDecl>(CapturedDecl)) {
        CaptureType = VD->getType();
        // If captured by reference, keep as reference type
        // If captured by copy, use the value type
        if (Capture.Kind == LambdaCapture::ByCopy) {
          // For by-copy, we might want to strip references
          // For now, keep as-is (TODO: implement proper decay)
        }
      } else {
        // Fallback: not a variable, use int
        CaptureType = Context.getIntType();
      }
    } else {
      // Variable not found in scope, use int as fallback
      CaptureType = Context.getIntType();
    }
  }
  
  auto *Field = Context.create<FieldDecl>(Capture.Loc, Capture.Name, CaptureType);
  ClosureClass->addMember(Field);
  ClosureClass->addField(Field);
}
```

**问题**:
1. **By-copy未去除引用**：`[x]`捕获引用类型变量时，field应该是值类型而非引用
2. **未处理数组退化**：数组应该退化为指针
3. **未处理函数退化**：函数应该退化为函数指针
4. **Fallback使用int不合理**：应该报错而非静默使用int

**建议修复**:
```cpp
for (auto &Capture : Captures) {
  QualType CaptureType;
  
  if (Capture.Kind == LambdaCapture::InitCopy && Capture.InitExpr) {
    // Init capture: [x = expr] - type from initialization expression
    CaptureType = Capture.InitExpr->getType();
  } else {
    // Named capture: [x] or [&x] - lookup in current scope
    NamedDecl *CapturedDecl = LookupName(Capture.Name);
    Capture.CapturedDecl = CapturedDecl;
    
    if (!CapturedDecl) {
      Diags.report(Capture.Loc, DiagID::err_undeclared_var_capture, Capture.Name);
      return ExprResult::getInvalid();
    }
    
    auto *VD = llvm::dyn_cast<VarDecl>(CapturedDecl);
    if (!VD) {
      Diags.report(Capture.Loc, DiagID::err_invalid_capture_target, Capture.Name);
      return ExprResult::getInvalid();
    }
    
    QualType OriginalType = VD->getType();
    
    if (Capture.Kind == LambdaCapture::ByCopy) {
      // By-copy: strip references, apply decay
      CaptureType = TC.performDecay(OriginalType);
    } else {
      // ByRef: keep as reference
      if (!OriginalType->isReferenceType()) {
        CaptureType = Context.getLValueReferenceType(OriginalType);
      } else {
        CaptureType = OriginalType;
      }
    }
  }
  
  auto *Field = Context.create<FieldDecl>(Capture.Loc, Capture.Name, CaptureType);
  ClosureClass->addMember(Field);
  ClosureClass->addField(Field);
}
```

---

### P2问题 #3: 默认捕获不完整

**位置**: `parseLambdaCaptureList` L283-286

**当前实现**:
```cpp
} else if (Tok.is(TokenKind::equal)) {
  // = = capture by copy (default)
  consumeToken();
  continue;  // ← Just consume, don't add to Captures
}
```

**问题**:
- `[=]`默认捕获只是consume，未记录
- Sema无法知道需要捕获哪些变量
- 需要在Sema层分析lambda body中的所有odr-used变量

**建议修复**:
```cpp
// In Parser:
} else if (Tok.is(TokenKind::equal)) {
  // = = capture by copy (default)
  consumeToken();
  
  // Create a special "default capture" marker
  LambdaCapture DefaultCapture;
  DefaultCapture.Kind = LambdaCapture::DefaultByCopy;
  DefaultCapture.Name = "";
  Captures.push_back(DefaultCapture);
  
  // Don't continue - we need to check for additional captures
  if (!tryConsumeToken(TokenKind::comma)) {
    break;
  }
  continue;
}

// In Sema::ActOnLambdaExpr:
// After creating closure class, analyze body for odr-used variables
if (hasDefaultCapture(Captures)) {
  llvm::SmallVector<VarDecl *, 8> ODRUsedVars;
  collectODRUsedVars(Body, ODRUsedVars);
  
  for (auto *VD : ODRUsedVars) {
    // Skip if already explicitly captured
    if (isExplicitlyCaptured(VD, Captures))
      continue;
    
    // Add implicit capture
    LambdaCapture ImplicitCapture;
    ImplicitCapture.Kind = getDefaultCaptureKind(Captures);
    ImplicitCapture.Name = VD->getName();
    ImplicitCapture.CapturedDecl = VD;
    Captures.push_back(ImplicitCapture);
    
    // Add field to closure class
    QualType FieldType = ...;
    auto *Field = Context.create<FieldDecl>(..., VD->getName(), FieldType);
    ClosureClass->addMember(Field);
    ClosureClass->addField(Field);
  }
}
```

---

### P3问题 #4: 缺少this捕获支持

**观察**:
- `parseLambdaCaptureList`中未处理`[this]`和`[*this]`
- C++17之前的lambda需要捕获this才能访问成员
- C++23允许`[*this]`按值捕获*this

**建议添加**:
```cpp
// In parseLambdaCaptureList:
} else if (Tok.is(TokenKind::kw_this)) {
  // [this] - capture this pointer
  consumeToken();
  Capture.Kind = LambdaCapture::This;
  Capture.Name = "this";
  Captures.push_back(Capture);
} else if (Tok.is(TokenKind::star)) {
  // Check for [*this]
  consumeToken();
  if (Tok.is(TokenKind::kw_this)) {
    consumeToken();
    Capture.Kind = LambdaCapture::StarThis;
    Capture.Name = "*this";
    Captures.push_back(Capture);
  } else {
    emitError(DiagID::err_expected);
  }
}
```

---

## 📈 统计数据

| 指标 | 数值 |
|------|------|
| 核心函数总数 | 3个（1个Sema + 2个Parser） |
| AST类数量 | 1个（LambdaExpr） |
| Sema复杂度 | ActOnLambdaExpr: 121行 |
| Parser复杂度 | parseLambdaExpression: 117行, parseLambdaCaptureList: 46行 |
| 发现问题数 | 4个（P1×1, P2×2, P3×1） |
| 代码行数估算 | ~284行 |
| **实现完整度** | **~70%**（核心功能完整，细节待完善） |

---

## 🎯 总结

### ✅ 优点

1. **核心架构完整**：闭包类生成、operator()创建、捕获变量处理都已实现
2. **C++20/C++23支持**：模板lambda、lambda属性都有支持
3. **作用域管理正确**：PushScope/PopScope确保捕获变量作用域正确
4. **CodeGen友好**：建立了captured var → field index映射
5. **Sema实现详细**：121行的ActOnLambdaExpr覆盖所有关键步骤

### ⚠️ 待改进

1. **P1: 返回类型推导过于简化**：只检查顶层return，未递归检查
2. **P2: 捕获类型推断不完整**：by-copy未去除引用，fallback使用int不合理
3. **P2: 默认捕获不完整**：`[=]`和`[&]`未实际实现
4. **P3: 缺少this捕获**：`[this]`和`[*this]`未支持

### 🔗 与其他功能域的关联

- **Task 2.2.3 (名称查找)**: Lambda捕获需要通过LookupName查找变量
- **Task 2.2.5 (声明处理)**: 闭包类是特殊的CXXRecordDecl
- **Task 2.2.7 (表达式处理)**: Lambda调用通过operator()实现
- **CodeGen阶段**: 需要使用CapturedVarsMap生成正确的字段访问

### 🚨 紧急程度评估

**Lambda表达式是C++的核心特性**，当前实现已经可以工作，但存在以下限制：
- **短期**：基本lambda可以使用，但默认捕获和this捕获不可用
- **中期**：需要完善返回类型推导和捕获类型推断
- **长期**：需要支持泛型lambda的所有边缘情况

---

**报告生成时间**: 2026-04-19 21:50  
**下一步**: Task 2.2.11 - 结构化绑定功能域
