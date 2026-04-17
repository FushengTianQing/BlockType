# Phase 6：LLVM IR 生成
> **目标：** 实现从 AST 到 LLVM IR 的代码生成，包括类型映射、表达式代码生成、控制流、调试信息等
> **前置依赖：** Phase 0-5 完成（完整的语义分析 AST）
> **验收标准：** 能将 C++26 程序编译为 LLVM IR；生成的 IR 能被 LLVM 后端正确处理

---

## 📌 阶段总览

```
Phase 6 包含 5 个 Stage，共 14 个 Task，预计 6 周完成。
依赖链：Stage 6.1 → Stage 6.2 → Stage 6.3 → Stage 6.4 → Stage 6.5
```

| Stage | 名称 | 核心交付物 | 建议时长 |
|-------|------|-----------|----------|
| **Stage 6.1** | IRGen 基础设施 | CodeGenModule、类型映射、常量生成 | 1 周 |
| **Stage 6.2** | 表达式代码生成 | 算术、逻辑、调用、成员访问 | 2 周 |
| **Stage 6.3** | 控制流代码生成 | if、switch、循环、异常处理 | 1.5 周 |
| **Stage 6.4** | 函数与类代码生成 | 函数、构造/析构、虚函数、继承 | 1 周 |
| **Stage 6.5** | 调试信息 + 测试 | DWARF 调试信息、完整测试 | 0.5 周 |

---

## Stage 6.1 — IRGen 基础设施

### Task 6.1.1 CodeGenModule 类

**目标：** 建立代码生成的核心框架

**开发要点：**

- **E6.1.1.1** 创建 `include/zetacc/CodeGen/CodeGenModule.h`：
  ```cpp
  #pragma once
  
  #include "llvm/IR/LLVMContext.h"
  #include "llvm/IR/Module.h"
  #include "llvm/IR/IRBuilder.h"
  #include "zetacc/AST/ASTContext.h"
  #include "zetacc/AST/Decl.h"
  
  namespace zetacc {
  
  class CodeGenFunction;
  class CodeGenTypes;
  
  class CodeGenModule {
    ASTContext &Context;
    llvm::LLVMContext &LLVMCtx;
    std::unique_ptr<llvm::Module> TheModule;
    std::unique_ptr<CodeGenTypes> Types;
    
    // 全局变量和函数
    std::map<const Decl*, llvm::GlobalValue*> GlobalValues;
    
  public:
    CodeGenModule(ASTContext &Ctx, llvm::LLVMContext &LLVMCtx,
                  StringRef ModuleName);
    
    /// 代码生成入口
    void EmitTranslationUnit(TranslationUnitDecl *TU);
    
    /// 生成全局变量
    llvm::GlobalVariable* EmitGlobalVar(VarDecl *VD);
    
    /// 生成函数
    llvm::Function* EmitFunction(FunctionDecl *FD);
    
    /// 获取模块
    llvm::Module* getModule() const { return TheModule.get(); }
    
    /// 获取类型映射
    CodeGenTypes& getTypes() const { return *Types; }
    
    /// 获取 AST 上下文
    ASTContext& getASTContext() const { return Context; }
    
    /// 获取 LLVM 上下文
    llvm::LLVMContext& getLLVMContext() const { return LLVMCtx; }
  };
  
  } // namespace zetacc
  ```

- **E6.1.1.2** 实现 `src/CodeGen/CodeGenModule.cpp`

**开发关键点提示：**
> 请为 BlockType 实现 CodeGenModule。
>
> **核心职责**：
> - 管理 LLVM Module
> - 协调代码生成
> - 维护全局变量和函数表
>
> **关键方法**：
> - EmitTranslationUnit：生成整个翻译单元
> - EmitGlobalVar：生成全局变量
> - EmitFunction：生成函数
>
> **与其他组件的关系**：
> - CodeGenTypes：类型映射
> - CodeGenFunction：函数内代码生成

**Checkpoint：** CodeGenModule 编译通过

---

### Task 6.1.2 类型映射

**目标：** 实现 C++ 类型到 LLVM 类型的映射

**开发要点：**

- **E6.1.2.1** 创建 `include/zetacc/CodeGen/CodeGenTypes.h`：
  ```cpp
  class CodeGenTypes {
    CodeGenModule &CGM;
    std::map<QualType, llvm::Type*> TypeCache;
    
  public:
    CodeGenTypes(CodeGenModule &M) : CGM(M) {}
    
    /// 将 C++ 类型转换为 LLVM 类型
    llvm::Type* ConvertType(QualType T);
    
    /// 将函数类型转换为 LLVM 函数类型
    llvm::FunctionType* GetFunctionType(const FunctionType *FT);
    
    /// 获取类型大小
    llvm::Constant* GetSize(QualType T);
    
    /// 获取类型对齐
    llvm::Constant* GetAlign(QualType T);
  };
  ```

- **E6.1.2.2** 实现类型映射规则：
  - BuiltinType：void -> void, int -> i32, float -> float, ...
  - PointerType：T* -> T*
  - ReferenceType：T& -> T*
  - ArrayType：T[N] -> [N x T]
  - FunctionType：R(P1, P2) -> R (P1, P2)
  - RecordType：struct/class -> 结构体类型

**Checkpoint：** 类型映射正确

---

### Task 6.1.3 常量生成

**目标：** 实现常量表达式的代码生成

**开发要点：**

- **E6.1.3.1** 创建 `include/zetacc/CodeGen/CodeGenConstant.h`：
  ```cpp
  class CodeGenConstant {
    CodeGenModule &CGM;
    
  public:
    CodeGenConstant(CodeGenModule &M) : CGM(M) {}
    
    llvm::Constant* EmitConstant(Expr *E);
    llvm::Constant* EmitIntLiteral(IntegerLiteral *IL);
    llvm::Constant* EmitFloatLiteral(FloatingLiteral *FL);
    llvm::Constant* EmitStringLiteral(StringLiteral *SL);
    llvm::Constant* EmitNullPointer(QualType T);
  };
  ```

**Checkpoint：** 常量生成正确

---

## Stage 6.2 — 表达式代码生成

### Task 6.2.1 CodeGenFunction 类

**目标：** 实现函数内代码生成框架

**开发要点：**

- **E6.2.1.1** 创建 `include/zetacc/CodeGen/CodeGenFunction.h`：
  ```cpp
  class CodeGenFunction {
    CodeGenModule &CGM;
    llvm::Function *CurFn;
    llvm::IRBuilder<> Builder;
    
    // 当前函数的局部变量
    std::map<const VarDecl*, llvm::AllocaInst*> LocalDecls;
    
  public:
    CodeGenFunction(CodeGenModule &M);
    
    /// 生成函数体
    void EmitFunctionBody(FunctionDecl *FD);
    
    /// 生成表达式
    llvm::Value* EmitExpr(Expr *E);
    
    /// 生成语句
    void EmitStmt(Stmt *S);
    
    /// 创建基本块
    llvm::BasicBlock* createBasicBlock(StringRef Name);
    
    /// 获取当前插入点
    llvm::BasicBlock* getCurrentBlock() const { return Builder.GetInsertBlock(); }
  };
  ```

**Checkpoint：** CodeGenFunction 编译通过

---

### Task 6.2.2 算术与逻辑表达式

**目标：** 实现算术和逻辑表达式的代码生成

**开发要点：**

- **E6.2.2.1** 实现二元运算：
  ```cpp
  llvm::Value* CodeGenFunction::EmitBinaryOperator(BinaryOperator *BO) {
    llvm::Value *LHS = EmitExpr(BO->getLHS());
    llvm::Value *RHS = EmitExpr(BO->getRHS());
    
    switch (BO->getOpcode()) {
      case BO_Add: return Builder.CreateAdd(LHS, RHS);
      case BO_Sub: return Builder.CreateSub(LHS, RHS);
      case BO_Mul: return Builder.CreateMul(LHS, RHS);
      case BO_Div: return Builder.CreateSDiv(LHS, RHS);
      case BO_Rem: return Builder.CreateSRem(LHS, RHS);
      // ... 其他运算
    }
  }
  ```

- **E6.2.2.2** 实现一元运算

**Checkpoint：** 算术表达式代码生成正确

---

### Task 6.2.3 函数调用

**目标：** 实现函数调用的代码生成

**开发要点：**

- **E6.2.3.1** 实现 CallExpr 代码生成：
  ```cpp
  llvm::Value* CodeGenFunction::EmitCallExpr(CallExpr *CE) {
    // 1. 获取被调用函数
    llvm::Function *Callee = CGM.GetFunction(CE->getCalleeDecl());
    
    // 2. 生成参数
    std::vector<llvm::Value*> Args;
    for (Expr *Arg : CE->arguments()) {
      Args.push_back(EmitExpr(Arg));
    }
    
    // 3. 生成调用
    return Builder.CreateCall(Callee, Args);
  }
  ```

**Checkpoint：** 函数调用代码生成正确

---

### Task 6.2.4 成员访问

**目标：** 实现成员访问表达式的代码生成

**开发要点：**

- **E6.2.4.1** 实现成员访问：`obj.member`、`ptr->member`
- **E6.2.4.2** 处理虚函数调用

**Checkpoint：** 成员访问代码生成正确

---

## Stage 6.3 — 控制流代码生成

### Task 6.3.1 条件语句

**目标：** 实现条件语句的代码生成

**开发要点：**

- **E6.3.1.1** 实现 if 语句：
  ```cpp
  void CodeGenFunction::EmitIfStmt(IfStmt *IS) {
    // 生成条件
    llvm::Value *Cond = EmitExpr(IS->getCond());
    Cond = Builder.CreateICmpNE(Cond, Builder.getInt1(0));
    
    // 创建基本块
    llvm::BasicBlock *ThenBB = createBasicBlock("if.then");
    llvm::BasicBlock *ElseBB = createBasicBlock("if.else");
    llvm::BasicBlock *MergeBB = createBasicBlock("if.end");
    
    // 生成分支
    Builder.CreateCondBr(Cond, ThenBB, ElseBB);
    
    // 生成 then 分支
    EmitBlock(ThenBB);
    EmitStmt(IS->getThen());
    Builder.CreateBr(MergeBB);
    
    // 生成 else 分支
    EmitBlock(ElseBB);
    if (IS->getElse()) {
      EmitStmt(IS->getElse());
    }
    Builder.CreateBr(MergeBB);
    
    EmitBlock(MergeBB);
  }
  ```

- **E6.3.1.2** 实现 switch 语句

**Checkpoint：** 条件语句代码生成正确

---

### Task 6.3.2 循环语句

**目标：** 实现循环语句的代码生成

**开发要点：**

- **E6.3.2.1** 实现 for 循环
- **E6.3.2.2** 实现 while 循环
- **E6.3.2.3** 实现 do-while 循环

**Checkpoint：** 循环语句代码生成正确

---

### Task 6.3.3 跳转语句

**目标：** 实现跳转语句的代码生成

**开发要点：**

- **E6.3.3.1** 实现 break、continue、return、goto

**Checkpoint：** 跳转语句代码生成正确

---

## Stage 6.4 — 函数与类代码生成

### Task 6.4.1 函数代码生成

**目标：** 实现函数的完整代码生成

**开发要点：**

- **E6.4.1.1** 生成函数参数和局部变量
- **E6.4.1.2** 生成函数体

**Checkpoint：** 函数代码生成正确

---

### Task 6.4.2 类代码生成

**目标：** 实现类的代码生成

**开发要点：**

- **E6.4.2.1** 生成类布局
- **E6.4.2.2** 生成构造函数和析构函数
- **E6.4.2.3** 生成虚函数表

**Checkpoint：** 类代码生成正确

---

## Stage 6.5 — 调试信息 + 测试

### Task 6.5.1 调试信息生成

**目标：** 生成 DWARF 调试信息

**开发要点：**

- **E6.5.1.1** 集成 LLVM 调试信息生成

**Checkpoint：** 调试信息正确

---

### Task 6.5.2 IRGen 测试

**目标：** 建立 IRGen 的完整测试覆盖

**Checkpoint：** 测试覆盖率 ≥ 80%

---

## 📋 Phase 6 验收检查清单

```
[ ] CodeGenModule 类实现完成
[ ] 类型映射正确
[ ] 常量生成正确
[ ] CodeGenFunction 类实现完成
[ ] 算术表达式代码生成正确
[ ] 函数调用代码生成正确
[ ] 成员访问代码生成正确
[ ] 条件语句代码生成正确
[ ] 循环语句代码生成正确
[ ] 跳转语句代码生成正确
[ ] 函数代码生成正确
[ ] 类代码生成正确
[ ] 调试信息生成正确
[ ] 测试覆盖率 ≥ 80%
```

---

*Phase 6 完成标志：能将 C++26 程序编译为 LLVM IR；生成的 IR 能被 LLVM 后端正确处理；调试信息完整；测试通过，覆盖率 ≥ 80%。*
