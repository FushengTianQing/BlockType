# Phase 6：LLVM IR 生成
> **目标：** 实现从 AST 到 LLVM IR 的代码生成，包括类型映射、表达式代码生成、控制流、函数与类布局、调试信息等
> **前置依赖：** Phase 0-5 完成（完整的语义分析 AST + 模板系统）
> **验收标准：** 能将 C++26 程序编译为 LLVM IR；生成的 IR 能被 LLVM 后端正确处理

---

## 📌 阶段总览

```
Phase 6 包含 5 个 Stage，共 14 个 Task，预计 6 周完成。
依赖链：Stage 6.1 → Stage 6.2 → Stage 6.3 → Stage 6.4 → Stage 6.5
```

| Stage | 名称 | 核心交付物 | 建议时长 |
|-------|------|-----------|----------|
| **Stage 6.1** | IRGen 基础设施 | CodeGenModule、CodeGenTypes、CodeGenConstant、CodeGenFunction | 1 周 |
| **Stage 6.2** | 表达式代码生成 | 算术/逻辑/比较运算、函数调用、成员访问、类型转换 | 2 周 |
| **Stage 6.3** | 控制流代码生成 | if/switch/for/while/do-while、break/continue/return/goto | 1.5 周 |
| **Stage 6.4** | 函数与类代码生成 | 函数体、构造/析构、虚函数表、类布局、继承 | 1 周 |
| **Stage 6.5** | 调试信息 + 测试 | DWARF 调试信息、IR 验证、完整测试 | 0.5 周 |

### 已有基础设施（Phase 0-5 已完成）

以下组件已存在且**无需从零创建**，Phase 6 应在它们之上构建：

| 组件 | 位置 | 状态 |
|------|------|------|
| **AST 节点体系** | `include/blocktype/AST/Expr.h` | ✅ 40+ 种 Expr 子类（含所有 BinaryOp/UnaryOp/CallExpr/CastExpr） |
| **Stmt 体系** | `include/blocktype/AST/Stmt.h` | ✅ 20+ 种 Stmt 子类（含 If/Switch/For/While/Do/Try/Catch） |
| **Decl 体系** | `include/blocktype/AST/Decl.h` | ✅ 30+ 种 Decl 子类（含 CXXRecord/Method/Ctor/Dtor/VTable） |
| **Type 体系** | `include/blocktype/AST/Type.h` | ✅ 22 种内建类型 + Pointer/Reference/Array/Function/Record/Enum/TemplateSpecialization 等 |
| **BuiltinKind** | `include/blocktype/AST/BuiltinTypes.def` | ✅ Void/Bool/Char/Short/Int/Long/Float/Double/NullPtr 等 22 种 |
| **QualType** | `include/blocktype/AST/Type.h` | ✅ 完整的 CVR 限定符操作 |
| **ASTContext** | `include/blocktype/AST/ASTContext.h` | ✅ 类型工厂 + 节点分配器 |
| **Sema** | `include/blocktype/Sema/Sema.h` | ✅ 完整的语义分析（名字查找/类型检查/重载决议/模板实例化） |
| **LLVM 依赖** | `CMakeLists.txt` | ✅ LLVM 18+ 已链接（Core, Support, IRReader, OrcJIT, Native） |

**Phase 6 需要新建的组件：**

| 组件 | 文件 | 说明 |
|------|------|------|
| CodeGen 模块 | `include/blocktype/CodeGen/*.h` + `src/CodeGen/*.cpp` | 全新模块，从零创建 |
| IRGen 诊断 ID | `include/blocktype/Basic/DiagnosticIRGenKinds.def` | IR 生成阶段诊断 |

**Phase 6 架构图：**

```
┌─────────────────────────────────────────────────────────────────────┐
│                       CodeGenModule                                  │
│  (管理 LLVM Module、全局变量/函数表、TargetInfo)                       │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐  │
│  │  CodeGenTypes     │  │ CodeGenConstant  │  │  TargetInfo      │  │
│  │                   │  │                  │  │                  │  │
│  │  ConvertType()    │  │  EmitConstant()  │  │  getTypeInfo()   │  │
│  │  GetFunctionType()│  │  EmitInt/Float/  │  │  getTriple()     │  │
│  │  TypeCache        │  │   String/Null()  │  │  getDataLayout() │  │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘  │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                    CodeGenFunction                             │   │
│  │  (函数级 IR 生成：IRBuilder + 局部变量映射 + 控制流栈)           │   │
│  │                                                               │   │
│  │  EmitExpr() → EmitBinaryOp / EmitUnaryOp / EmitCallExpr /    │   │
│  │               EmitMemberExpr / EmitCastExpr / EmitLiteral     │   │
│  │                                                               │   │
│  │  EmitStmt() → EmitIf / EmitSwitch / EmitFor / EmitWhile /    │   │
│  │               EmitDo / EmitReturn / EmitCompound / EmitDecl   │   │
│  │                                                               │   │
│  │  EmitFunctionBody() → 参数 alloc + 局部变量 + 语句序列         │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                    CGCXX (C++ 特有代码生成)                     │   │
│  │  EmitCtor / EmitDtor / EmitVTable / EmitClassLayout /         │   │
│  │  EmitVirtualCall / EmitBaseInit / EmitMemberInit               │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

**数据流：**

```
TranslationUnitDecl
    ↓ CodeGenModule::EmitTranslationUnit()
    ├── VarDecl → CodeGenModule::EmitGlobalVar() → llvm::GlobalVariable
    ├── FunctionDecl → CodeGenModule::EmitFunction() → CodeGenFunction::EmitFunctionBody()
    │                                                     ↓
    │                                                 IRBuilder 生成 LLVM IR
    │                                                     ↓
    │                                               llvm::Function
    └── CXXRecordDecl → CGCXX::EmitVTable() / CGCXX::EmitClassLayout()
```

---

## Stage 6.1 — IRGen 基础设施

### Task 6.1.1 CodeGenModule 类

**目标：** 建立代码生成的核心框架

**开发要点：**

- **E6.1.1.1** 创建 `include/blocktype/CodeGen/CodeGenModule.h`：
  ```cpp
  #pragma once

  #include "llvm/IR/LLVMContext.h"
  #include "llvm/IR/Module.h"
  #include "llvm/IR/IRBuilder.h"
  #include "llvm/ADT/DenseMap.h"
  #include "blocktype/AST/ASTContext.h"
  #include "blocktype/AST/Decl.h"

  namespace blocktype {

  class CodeGenFunction;
  class CodeGenTypes;
  class CodeGenConstant;
  class CGCXX;
  class TargetInfo;

  /// CodeGenModule — 模块级代码生成引擎。
  ///
  /// 职责（参照 Clang CodeGenModule）：
  /// 1. 管理 LLVM Module（全局变量、函数声明、元数据）
  /// 2. 协调所有代码生成子组件（Types、Constant、Function、CXX）
  /// 3. 维护 Decl → llvm::GlobalValue 的映射表
  /// 4. 处理全局初始化（全局构造/析构函数列表）
  /// 5. 管理目标平台信息（TargetInfo）
  class CodeGenModule {
    ASTContext &Context;
    llvm::LLVMContext &LLVMCtx;
    std::unique_ptr<llvm::Module> TheModule;
    std::unique_ptr<CodeGenTypes> Types;
    std::unique_ptr<CodeGenConstant> Constants;
    std::unique_ptr<CGCXX> CXX;
    std::unique_ptr<TargetInfo> Target;

    // Decl → llvm::GlobalValue 映射（函数 + 全局变量）
    llvm::DenseMap<const Decl *, llvm::GlobalValue *> GlobalValues;

    // 全局变量延迟发射队列（先声明后定义）
    llvm::SmallVector<VarDecl *, 16> DeferredGlobalVars;

    // 全局构造/析构函数
    llvm::SmallVector<std::pair<FunctionDecl *, int>, 4> GlobalCtors;
    llvm::SmallVector<std::pair<FunctionDecl *, int>, 4> GlobalDtors;

  public:
    CodeGenModule(ASTContext &Ctx, llvm::LLVMContext &LLVMCtx,
                  llvm::StringRef ModuleName, llvm::StringRef TargetTriple);
    ~CodeGenModule();

    // Non-copyable
    CodeGenModule(const CodeGenModule &) = delete;
    CodeGenModule &operator=(const CodeGenModule &) = delete;

    //===------------------------------------------------------------------===//
    // 代码生成入口
    //===------------------------------------------------------------------===//

    /// 生成整个翻译单元的 LLVM IR。
    /// 遍历 TranslationUnitDecl 中的所有顶层声明，
    /// 按顺序生成全局变量、函数、类定义。
    void EmitTranslationUnit(TranslationUnitDecl *TU);

    /// 发射所有延迟定义（在所有声明处理完后调用）。
    void EmitDeferred();

    //===------------------------------------------------------------------===//
    // 全局变量生成
    //===------------------------------------------------------------------===//

    /// 生成全局变量的 LLVM IR。
    /// 创建 llvm::GlobalVariable 并注册到 GlobalValues 表。
    /// @param VD  变量声明
    /// @return    对应的 llvm::GlobalVariable
    llvm::GlobalVariable *EmitGlobalVar(VarDecl *VD);

    /// 获取已生成的全局变量（或 nullptr）。
    llvm::GlobalVariable *GetGlobalVar(VarDecl *VD);

    //===------------------------------------------------------------------===//
    // 函数生成
    //===------------------------------------------------------------------===//

    /// 生成函数的完整 LLVM IR。
    /// 创建 llvm::Function，设置参数名和属性，
    /// 然后委托 CodeGenFunction 生成函数体。
    llvm::Function *EmitFunction(FunctionDecl *FD);

    /// 获取函数的 llvm::Function（已生成或仅声明）。
    /// 对于尚未生成体的函数，创建外部声明。
    llvm::Function *GetFunction(FunctionDecl *FD);

    /// 获取或创建函数声明（不生成函数体）。
    llvm::Function *GetOrCreateFunctionDecl(FunctionDecl *FD);

    //===------------------------------------------------------------------===//
    // C++ 特有生成
    //===------------------------------------------------------------------===//

    /// 生成类的虚函数表。
    void EmitVTable(CXXRecordDecl *RD);

    /// 生成类的布局信息（字段偏移、大小、对齐）。
    void EmitClassLayout(CXXRecordDecl *RD);

    //===------------------------------------------------------------------===//
    // 访问器
    //===------------------------------------------------------------------===//

    llvm::Module *getModule() const { return TheModule.get(); }
    CodeGenTypes &getTypes() const { return *Types; }
    CodeGenConstant &getConstants() const { return *Constants; }
    CGCXX &getCXX() const { return *CXX; }
    TargetInfo &getTarget() const { return *Target; }
    ASTContext &getASTContext() const { return Context; }
    llvm::LLVMContext &getLLVMContext() const { return LLVMCtx; }

    /// 获取模块的数据布局。
    const llvm::DataLayout &getDataLayout() const;

    //===------------------------------------------------------------------===//
    // 全局构造/析构
    //===------------------------------------------------------------------===//

    /// 注册全局构造函数（atexit 模式）。
    void AddGlobalCtor(FunctionDecl *FD, int Priority = 65535);

    /// 注册全局析构函数。
    void AddGlobalDtor(FunctionDecl *FD, int Priority = 65535);

    /// 生成 llvm.global_ctors 和 llvm.global_dtors。
    void EmitGlobalCtorDtors();
  };

  } // namespace blocktype
  ```

- **E6.1.1.2** 实现 `src/CodeGen/CodeGenModule.cpp`：
  ```cpp
  #include "blocktype/CodeGen/CodeGenModule.h"
  #include "blocktype/CodeGen/CodeGenTypes.h"
  #include "blocktype/CodeGen/CodeGenConstant.h"
  #include "blocktype/CodeGen/CodeGenFunction.h"
  #include "blocktype/CodeGen/CGCXX.h"
  #include "blocktype/CodeGen/TargetInfo.h"

  namespace blocktype {

  CodeGenModule::CodeGenModule(ASTContext &Ctx, llvm::LLVMContext &LLVMCtx,
                               llvm::StringRef ModuleName,
                               llvm::StringRef TargetTriple)
      : Context(Ctx), LLVMCtx(LLVMCtx),
        TheModule(std::make_unique<llvm::Module>(ModuleName, LLVMCtx)) {
    TheModule->setTargetTriple(TargetTriple);

    // 初始化子组件
    Types = std::make_unique<CodeGenTypes>(*this);
    Constants = std::make_unique<CodeGenConstant>(*this);
    Target = std::make_unique<TargetInfo>(TargetTriple);
    CXX = std::make_unique<CGCXX>(*this);
  }

  void CodeGenModule::EmitTranslationUnit(TranslationUnitDecl *TU) {
    // 第一遍：生成所有声明（全局变量声明、函数声明、类布局）
    for (Decl *D : TU->getDecls()) {
      if (auto *VD = dyn_cast<VarDecl>(D)) {
        EmitGlobalVar(VD);
      } else if (auto *FD = dyn_cast<FunctionDecl>(D)) {
        GetOrCreateFunctionDecl(FD);
      } else if (auto *RD = dyn_cast<CXXRecordDecl>(D)) {
        EmitClassLayout(RD);
      }
    }

    // 第二遍：生成函数体和变量初始化
    for (Decl *D : TU->getDecls()) {
      if (auto *FD = dyn_cast<FunctionDecl>(D)) {
        if (FD->hasBody()) {
          EmitFunction(FD);
        }
      }
    }

    // 发射延迟定义
    EmitDeferred();

    // 生成全局构造/析构列表
    EmitGlobalCtorDtors();
  }

  llvm::Function *CodeGenModule::EmitFunction(FunctionDecl *FD) {
    llvm::Function *F = GetOrCreateFunctionDecl(FD);
    if (!F) return nullptr;

    // 如果已有定义，跳过
    if (!F->isDeclaration()) return F;

    // 委托 CodeGenFunction 生成函数体
    CodeGenFunction CGF(*this);
    CGF.EmitFunctionBody(FD, F);

    return F;
  }

  } // namespace blocktype
  ```

**开发关键点提示：**
> **核心职责**（参照 Clang CodeGenModule）：
> 1. 管理 LLVM Module 的生命周期
> 2. 维护 Decl → GlobalValue 映射（避免重复生成）
> 3. 两遍扫描：第一遍生成声明，第二遍生成定义（处理前向引用）
> 4. 全局变量延迟发射（处理常量折叠和跨 TU 引用）
>
> **与已有组件的关系**：
> - `ASTContext`：提供类型工厂和 AST 节点分配器
> - `Decl` 体系：`VarDecl`、`FunctionDecl`、`CXXRecordDecl` 是代码生成的入口
> - `Type` 体系：通过 `CodeGenTypes` 映射到 LLVM 类型
>
> **两遍发射策略**（参照 Clang）：
> 1. 第一遍：生成所有函数声明和全局变量声明（确保前向引用可解析）
> 2. 第二遍：生成函数体和全局变量初始化器
> 3. 最后：生成 VTable 和全局构造/析构列表

**Checkpoint：** CodeGenModule 编译通过；能创建 LLVM Module 并设置目标三元组

---

### Task 6.1.2 类型映射（CodeGenTypes）

**目标：** 实现 BlockType 类型系统到 LLVM 类型的完整映射

**开发要点：**

- **E6.1.2.1** 创建 `include/blocktype/CodeGen/CodeGenTypes.h`：
  ```cpp
  #pragma once

  #include "llvm/ADT/DenseMap.h"
  #include "llvm/IR/Type.h"
  #include "llvm/IR/Function.h"
  #include "blocktype/AST/Type.h"

  namespace blocktype {

  class CodeGenModule;

  /// CodeGenTypes — C++ 类型到 LLVM 类型的映射引擎。
  ///
  /// 职责（参照 Clang CodeGenTypes）：
  /// 1. 将 BlockType 的 QualType 映射为 llvm::Type*
  /// 2. 缓存已转换的类型（避免重复创建）
  /// 3. 生成函数类型（处理 this 指针、变参等）
  /// 4. 生成结构体类型（处理继承、虚基类、字段布局）
  /// 5. 处理枚举类型（映射到底层整数类型）
  class CodeGenTypes {
    CodeGenModule &CGM;

    /// QualType → llvm::Type* 缓存（包含 CVR 限定符的分离处理）
    llvm::DenseMap<const Type *, llvm::Type *> TypeCache;

    /// FunctionDecl → llvm::FunctionType* 缓存
    llvm::DenseMap<const FunctionDecl *, llvm::FunctionType *> FunctionTypeCache;

    /// RecordDecl → llvm::StructType* 缓存
    llvm::DenseMap<const RecordDecl *, llvm::StructType *> RecordTypeCache;

  public:
    explicit CodeGenTypes(CodeGenModule &M) : CGM(M) {}

    //===------------------------------------------------------------------===//
    // 类型转换主接口
    //===------------------------------------------------------------------===//

    /// 将 BlockType QualType 转换为 LLVM Type。
    /// 顶层 const/volatile 限定符被忽略（LLVM 不区分）。
    /// 引用类型 T& 被转换为指针类型 T*。
    llvm::Type *ConvertType(QualType T);

    /// 将 BlockType Type* 转换为 LLVM Type（无 CVR 限定符）。
    llvm::Type *ConvertTypeForMem(QualType T);

    /// 将 BlockType Type* 转换为 LLVM Type（用于寄存器值，不涉及内存）。
    /// 例如：数组的值类型在 LLVM 中不存在，需要其他处理。
    llvm::Type *ConvertTypeForValue(QualType T);

    //===------------------------------------------------------------------===//
    // 函数类型
    //===------------------------------------------------------------------===//

    /// 将 BlockType FunctionType 转换为 LLVM FunctionType。
    llvm::FunctionType *GetFunctionType(const FunctionType *FT);

    /// 根据 FunctionDecl 生成 LLVM FunctionType。
    /// 处理 this 指针（成员函数）、变参等特殊情况。
    llvm::FunctionType *GetFunctionTypeForDecl(FunctionDecl *FD);

    //===------------------------------------------------------------------===//
    // 记录类型（struct/class）
    //===------------------------------------------------------------------===//

    /// 将 RecordDecl 转换为 LLVM StructType。
    /// 处理字段布局（偏移、对齐、填充）。
    llvm::StructType *GetRecordType(RecordDecl *RD);

    /// 将 CXXRecordDecl 转换为 LLVM StructType（含虚函数表指针、基类子对象）。
    llvm::StructType *GetCXXRecordType(CXXRecordDecl *RD);

    /// 获取记录类型中字段的索引。
    unsigned GetFieldIndex(FieldDecl *FD);

    //===------------------------------------------------------------------===//
    // 类型信息查询
    //===------------------------------------------------------------------===//

    /// 获取类型的大小（字节）。
    uint64_t GetTypeSize(QualType T) const;

    /// 获取类型的对齐（字节）。
    uint64_t GetTypeAlign(QualType T) const;

    /// 获取类型的大小和偏移信息。
    llvm::Constant *GetSize(uint64_t SizeInBytes);
    llvm::Constant *GetAlign(uint64_t AlignInBytes);

  private:
    //===------------------------------------------------------------------===//
    // 内部类型转换分派
    //===------------------------------------------------------------------===//

    llvm::Type *ConvertBuiltinType(const BuiltinType *BT);
    llvm::Type *ConvertPointerType(const PointerType *PT);
    llvm::Type *ConvertReferenceType(const ReferenceType *RT);
    llvm::Type *ConvertArrayType(const ArrayType *AT);
    llvm::Type *ConvertFunctionType(const FunctionType *FT);
    llvm::Type *ConvertRecordType(const RecordType *RT);
    llvm::Type *ConvertEnumType(const EnumType *ET);
    llvm::Type *ConvertTypedefType(const TypedefType *TT);
    llvm::Type *ConvertTemplateSpecializationType(
        const TemplateSpecializationType *TST);
    llvm::Type *ConvertMemberPointerType(const MemberPointerType *MPT);
    llvm::Type *ConvertAutoType(const AutoType *AT);
    llvm::Type *ConvertDecltypeType(const DecltypeType *DT);
  };

  } // namespace blocktype
  ```

- **E6.1.2.2** 实现 `src/CodeGen/CodeGenTypes.cpp` 核心类型映射：
  ```cpp
  #include "blocktype/CodeGen/CodeGenTypes.h"
  #include "blocktype/CodeGen/CodeGenModule.h"

  namespace blocktype {

  llvm::Type *CodeGenTypes::ConvertType(QualType T) {
    if (T.isNull()) return nullptr;

    // 剥离顶层 CVR 限定符（LLVM 不区分 const/volatile）
    const Type *Ty = T.getTypePtr();

    // 检查缓存
    auto It = TypeCache.find(Ty);
    if (It != TypeCache.end()) return It->second;

    // 按类型分派
    llvm::Type *Result = nullptr;
    switch (Ty->getTypeClass()) {
    case TypeClass::Builtin:
      Result = ConvertBuiltinType(cast<BuiltinType>(Ty)); break;
    case TypeClass::Pointer:
      Result = ConvertPointerType(cast<PointerType>(Ty)); break;
    case TypeClass::LValueReference:
    case TypeClass::RValueReference:
      Result = ConvertReferenceType(cast<ReferenceType>(Ty)); break;
    case TypeClass::ConstantArray:
    case TypeClass::IncompleteArray:
    case TypeClass::VariableArray:
      Result = ConvertArrayType(cast<ArrayType>(Ty)); break;
    case TypeClass::Function:
      Result = ConvertFunctionType(cast<FunctionType>(Ty)); break;
    case TypeClass::Record:
      Result = ConvertRecordType(cast<RecordType>(Ty)); break;
    case TypeClass::Enum:
      Result = ConvertEnumType(cast<EnumType>(Ty)); break;
    case TypeClass::Typedef:
      Result = ConvertTypedefType(cast<TypedefType>(Ty)); break;
    case TypeClass::TemplateSpecialization:
      Result = ConvertTemplateSpecializationType(
          cast<TemplateSpecializationType>(Ty)); break;
    case TypeClass::MemberPointer:
      Result = ConvertMemberPointerType(cast<MemberPointerType>(Ty)); break;
    case TypeClass::Auto:
      Result = ConvertAutoType(cast<AutoType>(Ty)); break;
    case TypeClass::Decltype:
      Result = ConvertDecltypeType(cast<DecltypeType>(Ty)); break;
    // Dependent/Unresolved 类型不应出现在 CodeGen 阶段
    case TypeClass::Dependent:
    case TypeClass::Unresolved:
    case TypeClass::TemplateTypeParm:
    case TypeClass::Elaborated:
      // Fallback: 递归到底层类型或报错
      Result = llvm::Type::getVoidTy(CGM.getLLVMContext()); break;
    }

    if (Result) TypeCache[Ty] = Result;
    return Result;
  }

  llvm::Type *CodeGenTypes::ConvertBuiltinType(const BuiltinType *BT) {
    llvm::LLVMContext &Ctx = CGM.getLLVMContext();
    switch (BT->getKind()) {
    case BuiltinKind::Void:      return llvm::Type::getVoidTy(Ctx);
    case BuiltinKind::Bool:      return llvm::Type::getInt1Ty(Ctx);
    case BuiltinKind::Char:
    case BuiltinKind::SignedChar:
    case BuiltinKind::UnsignedChar:
    case BuiltinKind::Char8:     return llvm::Type::getInt8Ty(Ctx);
    case BuiltinKind::WChar:
    case BuiltinKind::Char16:    return llvm::Type::getInt16Ty(Ctx);
    case BuiltinKind::Char32:    return llvm::Type::getInt32Ty(Ctx);
    case BuiltinKind::Short:     return llvm::Type::getInt16Ty(Ctx);
    case BuiltinKind::Int:       return llvm::Type::getInt32Ty(Ctx);
    case BuiltinKind::Long:      return llvm::Type::getInt64Ty(Ctx);
    case BuiltinKind::LongLong:  return llvm::Type::getInt64Ty(Ctx);
    case BuiltinKind::Int128:    return llvm::Type::getInt128Ty(Ctx);
    case BuiltinKind::UnsignedShort:    return llvm::Type::getInt16Ty(Ctx);
    case BuiltinKind::UnsignedInt:      return llvm::Type::getInt32Ty(Ctx);
    case BuiltinKind::UnsignedLong:     return llvm::Type::getInt64Ty(Ctx);
    case BuiltinKind::UnsignedLongLong: return llvm::Type::getInt64Ty(Ctx);
    case BuiltinKind::UnsignedInt128:   return llvm::Type::getInt128Ty(Ctx);
    case BuiltinKind::Float:     return llvm::Type::getFloatTy(Ctx);
    case BuiltinKind::Double:    return llvm::Type::getDoubleTy(Ctx);
    case BuiltinKind::LongDouble:return llvm::Type::getFP128Ty(Ctx);
    case BuiltinKind::Float128:  return llvm::Type::getFP128Ty(Ctx);
    case BuiltinKind::NullPtr:   return llvm::Type::getInt8PtrTy(Ctx);
    default:                     return llvm::Type::getVoidTy(Ctx);
    }
  }

  llvm::Type *CodeGenTypes::ConvertPointerType(const PointerType *PT) {
    llvm::Type *Pointee = ConvertType(QualType(PT->getPointeeType(), Qualifier::None));
    return llvm::PointerType::get(Pointee, 0); // address space 0
  }

  llvm::Type *CodeGenTypes::ConvertReferenceType(const ReferenceType *RT) {
    // 引用在 LLVM IR 中表示为指针
    return llvm::PointerType::get(
        ConvertType(QualType(RT->getReferencedType(), Qualifier::None)), 0);
  }

  llvm::Type *CodeGenTypes::ConvertArrayType(const ArrayType *AT) {
    llvm::Type *ElemTy = ConvertType(QualType(AT->getElementType(), Qualifier::None));
    if (auto *CAT = dyn_cast<ConstantArrayType>(AT)) {
      return llvm::ArrayType::get(ElemTy, CAT->getSize().getZExtValue());
    }
    // IncompleteArray / VariableArray: 使用零长度数组作为占位
    return llvm::ArrayType::get(ElemTy, 0);
  }

  llvm::Type *CodeGenTypes::ConvertEnumType(const EnumType *ET) {
    // 枚举类型映射到其底层整数类型（默认 int）
    return llvm::Type::getInt32Ty(CGM.getLLVMContext());
  }

  } // namespace blocktype
  ```

**开发关键点提示：**
> **类型映射规则**（参照 Clang CodeGenTypes.h + TTypes.h）：
> - BuiltinType: 一一对应（int→i32, float→float, void→void）
> - PointerType: T* → llvm pointer type
> - ReferenceType: T& → T*（LLVM 中引用就是指针）
> - ArrayType: T[N] → [N x T]
> - FunctionType: R(P1,P2) → R(P1,P2)
> - RecordType: struct → llvm StructType
> - EnumType: → 底层整数类型
> - TypedefType: → 递归到目标类型
> - TemplateSpecializationType: → 递归到实例化后的类型
> - NullPtrType: → i8*
>
> **LLVM 不区分 const/volatile**：顶层 CVR 限定符在转换时被忽略。
>
> **注意指针的地址空间**：默认使用地址空间 0（通用），GPU 目标可能需要不同地址空间。

**Checkpoint：** 所有 22 种 BuiltinType 正确映射；Pointer/Reference/Array/Function 类型映射测试通过

---

### Task 6.1.3 常量生成（CodeGenConstant）

**目标：** 实现常量表达式的 LLVM IR 生成

**开发要点：**

- **E6.1.3.1** 创建 `include/blocktype/CodeGen/CodeGenConstant.h`：
  ```cpp
  #pragma once

  #include "llvm/IR/Constants.h"
  #include "llvm/ADT/APSInt.h"
  #include "llvm/ADT/APFloat.h"
  #include "blocktype/AST/Expr.h"
  #include "blocktype/AST/Type.h"

  namespace blocktype {

  class CodeGenModule;

  /// CodeGenConstant — 常量表达式到 LLVM 常量的生成器。
  ///
  /// 职责（参照 Clang ConstantEmitter）：
  /// 1. 将 AST 常量表达式转换为 llvm::Constant
  /// 2. 处理整数字面量、浮点字面量、字符串字面量
  /// 3. 处理空指针常量、布尔常量
  /// 4. 处理聚合常量（数组初始化列表、结构体初始化列表）
  /// 5. 处理常量表达式（constexpr 求值结果）
  class CodeGenConstant {
    CodeGenModule &CGM;

  public:
    explicit CodeGenConstant(CodeGenModule &M) : CGM(M) {}

    //===------------------------------------------------------------------===//
    // 常量生成主接口
    //===------------------------------------------------------------------===//

    /// 将表达式作为常量发射。
    /// 用于全局变量初始化器、枚举值、模板参数等。
    /// @return llvm::Constant 如果表达式是常量，否则 nullptr
    llvm::Constant *EmitConstant(Expr *E);

    /// 将表达式作为常量发射，指定目标类型。
    llvm::Constant *EmitConstantForType(Expr *E, QualType DestType);

    //===------------------------------------------------------------------===//
    // 字面量常量
    //===------------------------------------------------------------------===//

    /// 整数字面量 → llvm::ConstantInt
    llvm::Constant *EmitIntLiteral(IntegerLiteral *IL);

    /// 浮点字面量 → llvm::ConstantFP
    llvm::Constant *EmitFloatLiteral(FloatingLiteral *FL);

    /// 字符串字面量 → llvm::GlobalVariable (llvm::ConstantDataArray)
    /// 每个字符串字面量创建一个唯一的全局常量。
    llvm::Constant *EmitStringLiteral(StringLiteral *SL);

    /// 布尔字面量 → llvm::ConstantInt(i1)
    llvm::Constant *EmitBoolLiteral(bool Value);

    /// 字符字面量 → llvm::ConstantInt(i32)
    llvm::Constant *EmitCharLiteral(CharacterLiteral *CL);

    /// nullptr 字面量 → llvm::ConstantPointerNull
    llvm::Constant *EmitNullPointer(QualType T);

    //===------------------------------------------------------------------===//
    // 聚合常量
    //===------------------------------------------------------------------===//

    /// 初始化列表 → llvm::ConstantStruct 或 llvm::ConstantArray
    llvm::Constant *EmitInitListExpr(InitListExpr *ILE);

    //===------------------------------------------------------------------===//
    // 零值 / 特殊常量
    //===------------------------------------------------------------------===//

    /// 发射类型的零值（用于默认初始化）。
    llvm::Constant *EmitZeroValue(QualType T);

    /// 发射类型的未定义值（用于未初始化变量，仅特定场景使用）。
    llvm::Constant *EmitUndefValue(QualType T);

    //===------------------------------------------------------------------===//
    // 类型转换常量
    //===------------------------------------------------------------------===//

    /// 整数类型之间的静态转换常量。
    llvm::Constant *EmitIntCast(llvm::Constant *C, QualType From, QualType To);

    /// 浮点到整数的转换常量。
    llvm::Constant *EmitFloatToIntCast(llvm::Constant *C, QualType From, QualType To);

    /// 整数到浮点的转换常量。
    llvm::Constant *EmitIntToFloatCast(llvm::Constant *C, QualType From, QualType To);

    //===------------------------------------------------------------------===//
    // 工具方法
    //===------------------------------------------------------------------===//

    /// 获取类型的空指针常量。
    llvm::ConstantPointerNull *GetNullPointer(QualType T);

    /// 获取整数类型的零值。
    llvm::ConstantInt *GetIntZero(QualType T);
    llvm::ConstantInt *GetIntOne(QualType T);

    /// 获取 LLVM 上下文。
    llvm::LLVMContext &getLLVMContext() const;
  };

  } // namespace blocktype
  ```

**Checkpoint：** 常量生成正确；IntegerLiteral、FloatingLiteral、StringLiteral、BoolLiteral 测试通过

---

### Task 6.1.4 目标平台信息（TargetInfo）

**目标：** 封装目标平台相关的类型大小和对齐信息

**开发要点：**

- **E6.1.4.1** 创建 `include/blocktype/CodeGen/TargetInfo.h`：
  ```cpp
  #pragma once

  #include "llvm/IR/DataLayout.h"
  #include "llvm/ADT/StringRef.h"
  #include "blocktype/AST/Type.h"

  namespace blocktype {

  /// TargetInfo — 目标平台类型信息。
  ///
  /// 职责（参照 Clang TargetInfo）：
  /// 1. 提供类型大小和对齐信息
  /// 2. 管理 LLVM DataLayout
  /// 3. 提供目标三元组信息
  /// 4. 定义 ABI 特定规则（如 struct 返回值、this 指针传递方式）
  class TargetInfo {
    llvm::DataLayout DL;
    llvm::StringRef Triple;

  public:
    explicit TargetInfo(llvm::StringRef TargetTriple);

    //===------------------------------------------------------------------===//
    // 类型大小和对齐
    //===------------------------------------------------------------------===//

    /// 获取类型的大小（字节）。
    uint64_t getTypeSize(QualType T) const;

    /// 获取类型的对齐（字节）。
    uint64_t getTypeAlign(QualType T) const;

    /// 获取内建类型的大小。
    uint64_t getBuiltinSize(BuiltinKind K) const;

    /// 获取内建类型的对齐。
    uint64_t getBuiltinAlign(BuiltinKind K) const;

    /// 获取指针大小。
    uint64_t getPointerSize() const { return DL.getPointerSize(0); }

    /// 获取指针对齐。
    uint64_t getPointerAlign() const { return DL.getPointerABIAlignment(0); }

    //===------------------------------------------------------------------===//
    // DataLayout 访问
    //===------------------------------------------------------------------===//

    const llvm::DataLayout &getDataLayout() const { return DL; }
    llvm::StringRef getTriple() const { return Triple; }

    //===------------------------------------------------------------------===//
    // ABI 查询
    //===------------------------------------------------------------------===//

    /// 结构体是否通过寄存器返回（而非内存）。
    bool isStructReturnInRegister(QualType T) const;

    /// this 指针是否通过寄存器传递。
    bool isThisPassedInRegister() const { return true; }

    /// 枚举类型的底层整数大小。
    uint64_t getEnumSize() const { return 32; } // 默认 4 字节
  };

  } // namespace blocktype
  ```

**Checkpoint：** TargetInfo 编译通过；能正确报告 AArch64/x86_64 的类型大小和对齐

---

## Stage 6.2 — 表达式代码生成

### Task 6.2.1 CodeGenFunction 类

**目标：** 实现函数级代码生成的核心框架

**开发要点：**

- **E6.2.1.1** 创建 `include/blocktype/CodeGen/CodeGenFunction.h`：
  ```cpp
  #pragma once

  #include "llvm/IR/IRBuilder.h"
  #include "llvm/IR/Function.h"
  #include "llvm/ADT/DenseMap.h"
  #include "llvm/ADT/SmallVector.h"
  #include "blocktype/AST/Expr.h"
  #include "blocktype/AST/Stmt.h"
  #include "blocktype/AST/Decl.h"
  #include "blocktype/AST/Type.h"

  namespace blocktype {

  class CodeGenModule;

  /// IRBuilderTy — 类型别名，简化 IRBuilder 使用。
  using IRBuilderTy = llvm::IRBuilder<>;

  /// CodeGenFunction — 函数级代码生成引擎。
  ///
  /// 职责（参照 Clang CodeGenFunction）：
  /// 1. 管理当前函数的 IRBuilder（插入点）
  /// 2. 维护局部变量映射表（VarDecl → AllocaInst）
  /// 3. 管理控制流栈（break/continue/return 目标）
  /// 4. 生成表达式求值代码（EmitExpr → llvm::Value*）
  /// 5. 生成语句执行代码（EmitStmt）
  class CodeGenFunction {
    CodeGenModule &CGM;
    IRBuilderTy Builder;

    /// 当前正在生成的 LLVM 函数。
    llvm::Function *CurFn = nullptr;

    /// 当前函数的 FunctionDecl。
    FunctionDecl *CurFD = nullptr;

    /// 局部变量映射：VarDecl → AllocaInst*
    llvm::DenseMap<const VarDecl *, llvm::AllocaInst *> LocalDecls;

    //===------------------------------------------------------------------===//
    // 控制流栈
    //===------------------------------------------------------------------===//

    /// Break/Continue 跳转目标栈（用于循环和 switch）
    struct BreakContinue {
      llvm::BasicBlock *BreakBB;
      llvm::BasicBlock *ContinueBB;
    };
    llvm::SmallVector<BreakContinue, 4> BreakContinueStack;

    /// Return 目标基本块（用于在函数末尾插入统一的 return 逻辑）
    llvm::BasicBlock *ReturnBlock = nullptr;

    /// 当前函数的返回值 alloca（用于在多个 return 路径中统一存储返回值）
    llvm::AllocaInst *ReturnValue = nullptr;

  public:
    explicit CodeGenFunction(CodeGenModule &M);

    //===------------------------------------------------------------------===//
    // 函数体生成
    //===------------------------------------------------------------------===//

    /// 生成函数的完整 LLVM IR。
    /// @param FD  函数声明
    /// @param Fn  已创建的 llvm::Function
    void EmitFunctionBody(FunctionDecl *FD, llvm::Function *Fn);

    //===------------------------------------------------------------------===//
    // 表达式生成（返回 llvm::Value*）
    //===------------------------------------------------------------------===//

    /// 生成表达式的求值代码，返回结果值。
    llvm::Value *EmitExpr(Expr *E);

    /// 生成表达式并将其存储到内存地址（用于 glvalue 表达式）。
    llvm::Value *EmitExprForStore(Expr *E);

    //===------------------------------------------------------------------===//
    // 语句生成
    //===------------------------------------------------------------------===//

    /// 生成语句的执行代码。
    void EmitStmt(Stmt *S);

    /// 生成语句序列。
    void EmitStmts(llvm::ArrayRef<Stmt *> Stmts);

    //===------------------------------------------------------------------===//
    // 基本块管理
    //===------------------------------------------------------------------===//

    /// 创建一个新的基本块并设置插入点。
    llvm::BasicBlock *createBasicBlock(llvm::StringRef Name);

    /// 将基本块追加到当前函数并设置插入点。
    void EmitBlock(llvm::BasicBlock *BB);

    /// 获取当前基本块。
    llvm::BasicBlock *getCurrentBlock() const { return Builder.GetInsertBlock(); }

    /// 当前是否有有效的插入点。
    bool haveInsertPoint() const { return Builder.GetInsertBlock() != nullptr; }

    //===------------------------------------------------------------------===//
    // 局部变量管理
    //===------------------------------------------------------------------===//

    /// 为局部变量创建 alloca 指令。
    llvm::AllocaInst *CreateAlloca(QualType T, llvm::StringRef Name = "");

    /// 注册局部变量的 alloca。
    void setLocalDecl(VarDecl *VD, llvm::AllocaInst *Alloca);

    /// 获取局部变量的 alloca。
    llvm::AllocaInst *getLocalDecl(VarDecl *VD) const;

    /// 加载局部变量的值。
    llvm::Value *LoadLocalVar(VarDecl *VD);

    /// 存储值到局部变量。
    void StoreLocalVar(VarDecl *VD, llvm::Value *Val);

    //===------------------------------------------------------------------===//
    // 辅助方法
    //===------------------------------------------------------------------===//

    /// 获取 IRBuilder。
    IRBuilderTy &getBuilder() { return Builder; }

    /// 获取 CodeGenModule。
    CodeGenModule &getCGM() const { return CGM; }

    /// 获取当前函数。
    llvm::Function *getCurrentFunction() const { return CurFn; }

    /// 获取当前的 FunctionDecl。
    FunctionDecl *getCurrentFunctionDecl() const { return CurFD; }

    //===------------------------------------------------------------------===//
    // 表达式生成子方法（内部分派）
    //===------------------------------------------------------------------===//

    llvm::Value *EmitBinaryOperator(BinaryOperator *BO);
    llvm::Value *EmitUnaryOperator(UnaryOperator *UO);
    llvm::Value *EmitCallExpr(CallExpr *CE);
    llvm::Value *EmitMemberExpr(MemberExpr *ME);
    llvm::Value *EmitDeclRefExpr(DeclRefExpr *DRE);
    llvm::Value *EmitCastExpr(CastExpr *CE);
    llvm::Value *EmitArraySubscriptExpr(ArraySubscriptExpr *ASE);
    llvm::Value *EmitConditionalOperator(ConditionalOperator *CO);
    llvm::Value *EmitInitListExpr(InitListExpr *ILE);
    llvm::Value *EmitCXXConstructExpr(CXXConstructExpr *CCE);
    llvm::Value *EmitCXXThisExpr(CXXThisExpr *TE);
    llvm::Value *EmitCXXNewExpr(CXXNewExpr *NE);
    llvm::Value *EmitCXXDeleteExpr(CXXDeleteExpr *DE);

    //===------------------------------------------------------------------===//
    // 语句生成子方法（内部分派）
    //===------------------------------------------------------------------===//

    void EmitCompoundStmt(CompoundStmt *CS);
    void EmitIfStmt(IfStmt *IS);
    void EmitSwitchStmt(SwitchStmt *SS);
    void EmitForStmt(ForStmt *FS);
    void EmitWhileStmt(WhileStmt *WS);
    void EmitDoStmt(DoStmt *DS);
    void EmitReturnStmt(ReturnStmt *RS);
    void EmitDeclStmt(DeclStmt *DS);
    void EmitBreakStmt(BreakStmt *BS);
    void EmitContinueStmt(ContinueStmt *CS);
    void EmitGotoStmt(GotoStmt *GS);
    void EmitLabelStmt(LabelStmt *LS);
    void EmitCXXTryStmt(CXXTryStmt *TS);
  };

  } // namespace blocktype
  ```

**开发关键点提示：**
> **IRBuilder 使用模式**（参照 Clang CodeGenFunction）：
> - `Builder.CreateAdd/Sub/Mul/SDiv/SRem` — 整数算术
> - `Builder.CreateFAdd/FSub/FMul/FDiv` — 浮点算术
> - `Builder.CreateICmp/FCmp` — 比较
> - `Builder.CreateCondBr/Br` — 分支
> - `Builder.CreateCall` — 函数调用
> - `Builder.CreateLoad/Store` — 内存访问
> - `Builder.CreateAlloca` — 栈分配
> - `Builder.CreateGEP` — 结构体/数组字段访问
>
> **控制流栈**：
> - 进入循环时 push {BreakBB, ContinueBB}
> - 遇到 break/continue 时跳到栈顶的目标
> - 退出循环时 pop
>
> **ReturnBlock 模式**（参照 Clang）：
> - 创建统一的 ReturnBlock，所有 return 语句跳转至此
> - 在 ReturnBlock 中执行 return 值的加载和 ret 指令
> - 避免在每个 return 路径生成独立的 ret

**Checkpoint：** CodeGenFunction 编译通过；能生成空函数体（仅含 ret void）

---

### Task 6.2.2 算术与逻辑表达式

**目标：** 实现算术和逻辑表达式的代码生成

**开发要点：**

- **E6.2.2.1** 实现二元运算代码生成：
  ```cpp
  llvm::Value *CodeGenFunction::EmitBinaryOperator(BinaryOperator *BO) {
    // 短路运算需要特殊处理
    auto Opcode = BO->getOpcode();
    if (Opcode == BinaryOpKind::LAnd) return EmitLogicalAnd(BO);
    if (Opcode == BinaryOpKind::LOr)  return EmitLogicalOr(BO);

    // 赋值运算需要特殊处理
    if (Opcode == BinaryOpKind::Assign) return EmitAssignment(BO);
    // 复合赋值: +=, -=, *=, /= 等
    // ... (通过递归到基础运算 + 赋值)

    llvm::Value *LHS = EmitExpr(BO->getLHS());
    llvm::Value *RHS = EmitExpr(BO->getRHS());
    QualType ResultTy = BO->getType();

    if (ResultTy->isIntegerType()) {
      // 整数运算（注意有符号/无符号区分）
      bool IsSigned = true; // 根据类型判断
      switch (Opcode) {
      case BinaryOpKind::Add: return Builder.CreateAdd(LHS, RHS, "add");
      case BinaryOpKind::Sub: return Builder.CreateSub(LHS, RHS, "sub");
      case BinaryOpKind::Mul: return Builder.CreateMul(LHS, RHS, "mul");
      case BinaryOpKind::Div:
        return IsSigned ? Builder.CreateSDiv(LHS, RHS, "sdiv")
                        : Builder.CreateUDiv(LHS, RHS, "udiv");
      case BinaryOpKind::Rem:
        return IsSigned ? Builder.CreateSRem(LHS, RHS, "srem")
                        : Builder.CreateURem(LHS, RHS, "urem");
      case BinaryOpKind::Shl: return Builder.CreateShl(LHS, RHS, "shl");
      case BinaryOpKind::Shr:
        return IsSigned ? Builder.CreateAShr(LHS, RHS, "ashr")
                        : Builder.CreateLShr(LHS, RHS, "lshr");
      case BinaryOpKind::And: return Builder.CreateAnd(LHS, RHS, "and");
      case BinaryOpKind::Or:  return Builder.CreateOr(LHS, RHS, "or");
      case BinaryOpKind::Xor: return Builder.CreateXor(LHS, RHS, "xor");
      // 比较运算
      case BinaryOpKind::LT: case BinaryOpKind::GT:
      case BinaryOpKind::LE: case BinaryOpKind::GE:
      case BinaryOpKind::EQ: case BinaryOpKind::NE:
        return EmitIntegerComparison(Opcode, LHS, RHS, IsSigned);
      default: break;
      }
    } else if (ResultTy->isFloatingType()) {
      // 浮点运算
      switch (Opcode) {
      case BinaryOpKind::Add: return Builder.CreateFAdd(LHS, RHS, "fadd");
      case BinaryOpKind::Sub: return Builder.CreateFSub(LHS, RHS, "fsub");
      case BinaryOpKind::Mul: return Builder.CreateFMul(LHS, RHS, "fmul");
      case BinaryOpKind::Div: return Builder.CreateFDiv(LHS, RHS, "fdiv");
      case BinaryOpKind::Rem: return Builder.CreateFRem(LHS, RHS, "frem");
      default: break;
      }
    }
    // 指针运算（指针 + 整数偏移 → GEP）
    // ...
    return nullptr;
  }
  ```

- **E6.2.2.2** 实现一元运算：
  ```cpp
  llvm::Value *CodeGenFunction::EmitUnaryOperator(UnaryOperator *UO) {
    switch (UO->getOpcode()) {
    case UnaryOpKind::Minus:
      return EmitUnaryMinus(UO);
    case UnaryOpKind::Plus:
      return EmitExpr(UO->getSubExpr());
    case UnaryOpKind::Not:
      return Builder.CreateNot(EmitExpr(UO->getSubExpr()), "not");
    case UnaryOpKind::LNot:
      return Builder.CreateICmpEQ(
          EmitExpr(UO->getSubExpr()),
          llvm::ConstantInt::get(getLLVMContext(), llvm::APInt(1, 0)),
          "lnot");
    case UnaryOpKind::Deref:
      return Builder.CreateLoad(EmitExpr(UO->getSubExpr()), "deref");
    case UnaryOpKind::AddrOf:
      return EmitLValue(UO->getSubExpr()); // 返回地址而非值
    case UnaryOpKind::PreInc:  case UnaryOpKind::PreDec:
    case UnaryOpKind::PostInc: case UnaryOpKind::PostDec:
      return EmitIncDec(UO);
    default: return nullptr;
    }
  }
  ```

**开发关键点提示：**
> **有符号 vs 无符号**：
> - BlockType 的 BuiltinType 提供 `isSignedInteger()` / `isUnsignedInteger()` 来判断
> - SDiv/UDiv, SRem/URem, AShr/LShr 必须区分
> - 比较运算也必须区分（ICmp 的谓词不同）
>
> **短路求值**（&&, ||）：
> - `a && b` → 生成 a 的求值，如果为 false 则短路跳到结果为 false 的块
> - `a || b` → 生成 a 的求值，如果为 true 则短路跳到结果为 true 的块
> - 结果来自 phi 节点合并两条路径

**Checkpoint：** 整数/浮点四则运算、比较运算、逻辑运算代码生成正确

---

### Task 6.2.3 函数调用

**目标：** 实现函数调用的代码生成

**开发要点：**

- **E6.2.3.1** 实现 CallExpr 代码生成：
  ```cpp
  llvm::Value *CodeGenFunction::EmitCallExpr(CallExpr *CE) {
    // 1. 获取被调用函数
    FunctionDecl *CalleeDecl = CE->getCalleeDecl();
    if (!CalleeDecl) return nullptr;

    llvm::Function *Callee = CGM.GetFunction(CalleeDecl);
    if (!Callee) return nullptr;

    // 2. 生成参数
    llvm::SmallVector<llvm::Value *, 8> Args;
    for (unsigned I = 0; I < CE->getNumArgs(); ++I) {
      llvm::Value *ArgVal = EmitExpr(CE->getArgs()[I]);
      // 参数类型转换（隐式转换）
      if (ArgVal) Args.push_back(ArgVal);
    }

    // 3. 成员函数调用：添加 this 指针
    if (auto *MD = dyn_cast<CXXMethodDecl>(CalleeDecl)) {
      if (!MD->isStatic()) {
        // this 指针作为第一个参数
        llvm::Value *This = EmitCXXThisExpr(nullptr);
        Args.insert(Args.begin(), This);
      }
    }

    // 4. 生成调用指令
    llvm::CallInst *Call = Builder.CreateCall(Callee, Args);

    // 5. 设置调用属性（根据函数类型）
    if (CalleeDecl->hasAttr<NoReturnAttr>()) {
      Call->setDoesNotReturn();
    }
    if (CalleeDecl->hasAttr<NoInlineAttr>()) {
      Call->setIsNoInline();
    }

    return Call;
  }
  ```

**Checkpoint：** 函数调用代码生成正确（含参数传递、返回值处理）

---

### Task 6.2.4 成员访问与类型转换

**目标：** 实现成员访问表达式和类型转换的代码生成

**开发要点：**

- **E6.2.4.1** 实现成员访问：`obj.member`、`ptr->member`
  ```cpp
  llvm::Value *CodeGenFunction::EmitMemberExpr(MemberExpr *ME) {
    // 获取对象基地址
    llvm::Value *Base = EmitExpr(ME->getBase());
    if (!Base) return nullptr;

    // obj.member → Base 是对象指针，需要 GEP
    // ptr->member → Base 已经是指针
    if (!ME->isArrow()) {
      // obj.member: Base 是对象本身，需要取地址
      // 简化处理：假设 Base 已经是指针
    }

    // 获取字段索引
    FieldDecl *FD = ME->getMemberDecl();
    unsigned FieldIdx = CGM.getTypes().GetFieldIndex(FD);

    // 生成 GEP 指令
    llvm::Value *GEP = Builder.CreateStructGEP(
        CGM.getTypes().ConvertType(ME->getBase()->getType()), Base, FieldIdx,
        FD->getName());

    // 加载字段值
    return Builder.CreateLoad(
        CGM.getTypes().ConvertType(ME->getType()), GEP, FD->getName());
  }
  ```

- **E6.2.4.2** 实现类型转换表达式：
  ```cpp
  llvm::Value *CodeGenFunction::EmitCastExpr(CastExpr *CE) {
    llvm::Value *SubExpr = EmitExpr(CE->getSubExpr());
    QualType FromTy = CE->getSubExpr()->getType();
    QualType ToTy = CE->getType();

    switch (CE->getCastKind()) {
    case CastKind::IntegralCast:
      return EmitIntegralCast(SubExpr, FromTy, ToTy);
    case CastKind::FloatingCast:
      return EmitFloatingCast(SubExpr, FromTy, ToTy);
    case CastKind::IntegralToFloating:
      return Builder.CreateSIToFP(SubExpr, CGM.getTypes().ConvertType(ToTy));
    case CastKind::FloatingToIntegral:
      return Builder.CreateFPToSI(SubExpr, CGM.getTypes().ConvertType(ToTy));
    case CastKind::PointerToIntegral:
      return Builder.CreatePtrToInt(SubExpr, CGM.getTypes().ConvertType(ToTy));
    case CastKind::IntegralToPointer:
      return Builder.CreateIntToPtr(SubExpr, CGM.getTypes().ConvertType(ToTy));
    case CastKind::BitCast:
    case CastKind::ReinterpretCast:
      return Builder.CreateBitCast(SubExpr, CGM.getTypes().ConvertType(ToTy));
    case CastKind::DerivedToBase:
      return EmitDerivedToBaseCast(SubExpr, CE);
    case CastKind::BaseToDerived:
      return EmitBaseToDerivedCast(SubExpr, CE);
    case CastKind::LValueToRValue:
      return Builder.CreateLoad(CGM.getTypes().ConvertType(ToTy), SubExpr);
    case CastKind::NoOp:
      return SubExpr;
    default:
      return SubExpr;
    }
  }
  ```

**Checkpoint：** 成员访问（. 和 →）和类型转换代码生成正确

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
    Cond = Builder.CreateICmpNE(Cond,
        llvm::ConstantInt::get(llvm::Type::getInt1Ty(getLLVMContext()), 0),
        "ifcond");

    // 创建基本块
    llvm::BasicBlock *ThenBB = createBasicBlock("if.then");
    llvm::BasicBlock *ElseBB = createBasicBlock("if.else");
    llvm::BasicBlock *MergeBB = createBasicBlock("if.end");

    // 生成分支
    Builder.CreateCondBr(Cond, ThenBB, ElseBB);

    // 生成 then 分支
    EmitBlock(ThenBB);
    if (IS->getThen()) EmitStmt(IS->getThen());
    if (haveInsertPoint()) Builder.CreateBr(MergeBB);

    // 生成 else 分支
    EmitBlock(ElseBB);
    if (IS->getElse()) {
      // else if 链：递归处理嵌套的 IfStmt
      EmitStmt(IS->getElse());
    }
    if (haveInsertPoint()) Builder.CreateBr(MergeBB);

    // 合并点
    EmitBlock(MergeBB);
  }
  ```

- **E6.3.1.2** 实现 switch 语句（参照 Clang 的跳转表/二分查找模式）

**Checkpoint：** if/else if/else 和 switch/case 代码生成正确

---

### Task 6.3.2 循环语句

**目标：** 实现循环语句的代码生成

**开发要点：**

- **E6.3.2.1** 实现 for 循环：
  ```cpp
  void CodeGenFunction::EmitForStmt(ForStmt *FS) {
    // 生成初始化
    if (FS->getInit()) EmitStmt(FS->getInit());

    llvm::BasicBlock *CondBB = createBasicBlock("for.cond");
    llvm::BasicBlock *BodyBB = createBasicBlock("for.body");
    llvm::BasicBlock *IncBB  = createBasicBlock("for.inc");
    llvm::BasicBlock *EndBB  = createBasicBlock("for.end");

    // Push break/continue 目标
    BreakContinueStack.push_back({EndBB, IncBB});

    // 条件块
    EmitBlock(CondBB);
    if (FS->getCond()) {
      llvm::Value *Cond = EmitExpr(FS->getCond());
      Cond = Builder.CreateICmpNE(Cond,
          llvm::ConstantInt::get(llvm::Type::getInt1Ty(getLLVMContext()), 0));
      Builder.CreateCondBr(Cond, BodyBB, EndBB);
    } else {
      Builder.CreateBr(BodyBB); // 无条件 → 无限循环
    }

    // 循环体
    EmitBlock(BodyBB);
    if (FS->getBody()) EmitStmt(FS->getBody());
    if (haveInsertPoint()) Builder.CreateBr(IncBB);

    // 增量
    EmitBlock(IncBB);
    if (FS->getInc()) EmitExpr(FS->getInc());
    Builder.CreateBr(CondBB);

    // 结束
    EmitBlock(EndBB);

    // Pop break/continue 目标
    BreakContinueStack.pop_back();
  }
  ```

- **E6.3.2.2** 实现 while 循环
- **E6.3.2.3** 实现 do-while 循环

**Checkpoint：** for/while/do-while 循环代码生成正确；break/continue 正确跳转

---

### Task 6.3.3 跳转语句

**目标：** 实现跳转语句的代码生成

**开发要点：**

- **E6.3.3.1** 实现 break、continue、return、goto：
  ```cpp
  void CodeGenFunction::EmitReturnStmt(ReturnStmt *RS) {
    if (RS->getReturnExpr()) {
      llvm::Value *RetVal = EmitExpr(RS->getReturnExpr());
      if (RetVal && ReturnValue) {
        Builder.CreateStore(RetVal, ReturnValue);
      } else if (RetVal) {
        Builder.CreateRet(RetVal);
        return; // 直接 ret，不跳到 ReturnBlock
      }
    }
    // 跳到统一的 ReturnBlock
    if (ReturnBlock) {
      Builder.CreateBr(ReturnBlock);
    } else {
      Builder.CreateRetVoid();
    }
  }

  void CodeGenFunction::EmitBreakStmt(BreakStmt *BS) {
    assert(!BreakContinueStack.empty() && "break outside loop/switch");
    Builder.CreateBr(BreakContinueStack.back().BreakBB);
  }

  void CodeGenFunction::EmitContinueStmt(ContinueStmt *CS) {
    assert(!BreakContinueStack.empty() && "continue outside loop");
    Builder.CreateBr(BreakContinueStack.back().ContinueBB);
  }
  ```

**Checkpoint：** return/break/continue/goto 代码生成正确

---

## Stage 6.4 — 函数与类代码生成

### Task 6.4.1 函数代码生成

**目标：** 实现函数的完整代码生成

**开发要点：**

- **E6.4.1.1** 生成函数参数和局部变量
- **E6.4.1.2** 生成函数体

```cpp
void CodeGenFunction::EmitFunctionBody(FunctionDecl *FD, llvm::Function *Fn) {
  CurFn = Fn;
  CurFD = FD;

  // 创建入口块
  llvm::BasicBlock *EntryBB = createBasicBlock("entry");
  Builder.SetInsertPoint(EntryBB);

  // 为返回值创建 alloca（非 void 返回类型）
  QualType RetType = FD->getReturnType();
  if (!RetType.isNull() && !RetType->isVoidType()) {
    ReturnBlock = createBasicBlock("return");
    ReturnValue = CreateAlloca(RetType, "retval");
  }

  // 为函数参数创建 alloca 并拷贝参数值
  unsigned ArgIdx = 0;
  for (auto I = Fn->arg_begin(); I != Fn->arg_end(); ++I, ++ArgIdx) {
    ParmVarDecl *PVD = FD->getParamDecl(ArgIdx);
    llvm::AllocaInst *Alloca = CreateAlloca(PVD->getType(), PVD->getName());
    Builder.CreateStore(&*I, Alloca);
    setLocalDecl(PVD, Alloca);
  }

  // 生成函数体
  if (Stmt *Body = FD->getBody()) {
    EmitStmt(Body);
  }

  // 处理 ReturnBlock
  if (ReturnBlock) {
    if (haveInsertPoint()) Builder.CreateBr(ReturnBlock);
    EmitBlock(ReturnBlock);
    llvm::Value *RetVal = Builder.CreateLoad(
        CGM.getTypes().ConvertType(RetType), ReturnValue, "ret");
    Builder.CreateRet(RetVal);
  } else {
    // void 函数：确保有终止指令
    if (haveInsertPoint()) Builder.CreateRetVoid();
  }
}
```

**Checkpoint：** 函数代码生成正确（含参数传递、返回值、局部变量）

---

### Task 6.4.2 类代码生成（CGCXX）

**目标：** 实现 C++ 类的代码生成

**开发要点：**

- **E6.4.2.1** 创建 `include/blocktype/CodeGen/CGCXX.h`：
  ```cpp
  #pragma once

  #include "llvm/ADT/SmallVector.h"
  #include "llvm/ADT/DenseMap.h"
  #include "blocktype/AST/Decl.h"

  namespace blocktype {

  class CodeGenModule;

  /// CGCXX — C++ 特有代码生成（类、构造/析构、虚函数表、继承）。
  ///
  /// 职责（参照 Clang CGCXX + CGClass + CGVTables）：
  /// 1. 生成类布局（字段偏移、大小、对齐、填充）
  /// 2. 生成构造函数（基类初始化、成员初始化、构造函数体）
  /// 3. 生成析构函数（成员析构、基类析构）
  /// 4. 生成虚函数表（vtable 布局、vptr 初始化）
  /// 5. 处理继承（单一/多重/虚继承）
  /// 6. 生成成员函数调用（虚/非虚分派）
  class CGCXX {
    CodeGenModule &CGM;

    /// VTable 缓存：CXXRecordDecl → llvm::GlobalVariable
    llvm::DenseMap<const CXXRecordDecl *, llvm::GlobalVariable *> VTables;

  public:
    explicit CGCXX(CodeGenModule &M) : CGM(M) {}

    //===------------------------------------------------------------------===//
    // 类布局
    //===------------------------------------------------------------------===//

    /// 计算类的字段布局。
    /// 返回每个字段的偏移量（字节）。
    llvm::SmallVector<uint64_t, 16> ComputeClassLayout(CXXRecordDecl *RD);

    /// 获取字段的偏移量。
    uint64_t GetFieldOffset(FieldDecl *FD);

    /// 获取类的完整大小（含填充和虚基类）。
    uint64_t GetClassSize(CXXRecordDecl *RD);

    //===------------------------------------------------------------------===//
    // 构造函数 / 析构函数
    //===------------------------------------------------------------------===//

    /// 生成构造函数的代码。
    void EmitConstructor(CXXConstructorDecl *Ctor, llvm::Function *Fn);

    /// 生成析构函数的代码。
    void EmitDestructor(CXXDestructorDecl *Dtor, llvm::Function *Fn);

    //===------------------------------------------------------------------===//
    // 虚函数表
    //===------------------------------------------------------------------===//

    /// 生成虚函数表。
    llvm::GlobalVariable *EmitVTable(CXXRecordDecl *RD);

    /// 获取虚函数表的类型。
    llvm::ArrayType *GetVTableType(CXXRecordDecl *RD);

    /// 计算虚函数表中方法的索引。
    unsigned GetVTableIndex(CXXMethodDecl *MD);

    /// 生成虚函数调用。
    llvm::Value *EmitVirtualCall(CXXMethodDecl *MD, llvm::Value *This,
                                  llvm::ArrayRef<llvm::Value *> Args);

    //===------------------------------------------------------------------===//
    // 继承
    //===------------------------------------------------------------------===//

    /// 生成派生类到基类的偏移量（用于指针调整）。
    llvm::Value *EmitBaseOffset(llvm::Value *DerivedPtr,
                                 CXXRecordDecl *Base);

    /// 生成基类到派生类的偏移量。
    llvm::Value *EmitDerivedOffset(llvm::Value *BasePtr,
                                    CXXRecordDecl *Derived);

    //===------------------------------------------------------------------===//
    // 成员初始化
    //===------------------------------------------------------------------===//

    /// 生成基类初始化器。
    void EmitBaseInitializer(CXXRecordDecl *Class, llvm::Value *This,
                              CXXBaseSpecifier *Base, Expr *Init);

    /// 生成成员初始化器。
    void EmitMemberInitializer(CXXRecordDecl *Class, llvm::Value *This,
                                FieldDecl *Field, Expr *Init);
  };

  } // namespace blocktype
  ```

- **E6.4.2.2** 生成类布局
- **E6.4.2.3** 生成构造函数和析构函数
- **E6.4.2.4** 生成虚函数表

**开发关键点提示：**
> **类布局算法**（参照 Clang ASTContext::getASTRecordLayout）：
> 1. 从基类开始，按声明顺序排列基类子对象
> 2. 如果有虚函数，在起始位置放置 vptr 指针（8 字节）
> 3. 按声明顺序排列非静态数据成员
> 4. 每个字段按自然对齐放置
> 5. 最后添加尾部填充以满足整体对齐要求
>
> **VTable 布局**（参照 Clang VTableLayout）：
> 1. 首先是 RTTI 指针（typeinfo）
> 2. 偏移到顶部（offset-to-top，用于多重继承的 this 调整）
> 3. 虚函数指针数组（按声明顺序，含覆盖和继承的方法）

**Checkpoint：** 类布局正确；构造/析构函数代码生成正确；虚函数表生成正确

---

## Stage 6.5 — 调试信息 + 测试

### Task 6.5.1 调试信息生成

**目标：** 生成 DWARF 调试信息

**开发要点：**

- **E6.5.1.1** 创建 `include/blocktype/CodeGen/CGDebugInfo.h`：
  ```cpp
  #pragma once

  #include "llvm/IR/DIBuilder.h"
  #include "llvm/ADT/DenseMap.h"
  #include "blocktype/AST/Decl.h"
  #include "blocktype/AST/Type.h"

  namespace blocktype {

  class CodeGenModule;

  /// CGDebugInfo — DWARF 调试信息生成。
  ///
  /// 职责（参照 Clang CGDebugInfo）：
  /// 1. 生成编译单元信息（文件名、命令行、语言标准）
  /// 2. 生成类型调试信息（Builtin/Pointer/Array/Record/Enum/Function）
  /// 3. 生成函数和变量的调试信息（名称、类型、位置）
  /// 4. 生成行号信息（源位置 → IR 位置的映射）
  /// 5. 生成作用域信息（词法块、命名空间）
  class CGDebugInfo {
    CodeGenModule &CGM;
    std::unique_ptr<llvm::DIBuilder> DIB;

    /// 编译单元
    llvm::DICompileUnit *CU = nullptr;

    /// 类型缓存：QualType → DIType
    llvm::DenseMap<const Type *, llvm::DIType *> TypeCache;

    /// 函数缓存：FunctionDecl → DISubprogram
    llvm::DenseMap<const FunctionDecl *, llvm::DISubprogram *> FnCache;

  public:
    explicit CGDebugInfo(CodeGenModule &M);
    ~CGDebugInfo();

    /// 初始化调试信息（创建编译单元）。
    void Initialize(llvm::StringRef FileName, llvm::StringRef Directory);

    /// 完成调试信息生成（finalize DIBuilder）。
    void Finalize();

    //===------------------------------------------------------------------===//
    // 类型调试信息
    //===------------------------------------------------------------------===//

    /// 获取类型的调试信息。
    llvm::DIType *GetDIType(QualType T);

    /// 获取内建类型的调试信息。
    llvm::DIType *GetBuiltinDIType(const BuiltinType *BT);

    /// 获取记录类型的调试信息。
    llvm::DIType *GetRecordDIType(const RecordType *RT);

    /// 获取枚举类型的调试信息。
    llvm::DIType *GetEnumDIType(const EnumType *ET);

    //===------------------------------------------------------------------===//
    // 函数/变量调试信息
    //===------------------------------------------------------------------===//

    /// 为函数生成调试信息。
    llvm::DISubprogram *GetFunctionDI(FunctionDecl *FD);

    /// 为局部变量生成调试信息。
    void EmitLocalVarDI(VarDecl *VD, llvm::AllocaInst *Alloca);

    //===------------------------------------------------------------------===//
    // 行号信息
    //===------------------------------------------------------------------===//

    /// 设置当前的源位置。
    void setLocation(SourceLocation Loc);
  };

  } // namespace blocktype
  ```

**Checkpoint：** 调试信息正确；生成的 IR 包含有效的 DWARF 元数据

---

### Task 6.5.2 IRGen 测试

**目标：** 建立 IRGen 的完整测试覆盖

**开发要点：**

- **E6.5.2.1** 创建单元测试文件：
  ```
  tests/unit/CodeGen/
  ├── CodeGenModuleTest.cpp        # CodeGenModule 测试
  ├── CodeGenTypesTest.cpp         # 类型映射测试（22 种内建类型 + 派生类型）
  ├── CodeGenConstantTest.cpp      # 常量生成测试
  ├── CodeGenExprTest.cpp          # 表达式代码生成测试
  ├── CodeGenStmtTest.cpp          # 控制流代码生成测试
  ├── CodeGenFunctionTest.cpp      # 函数代码生成测试
  ├── CodeGenClassTest.cpp         # 类代码生成测试
  └── CodeGenDebugInfoTest.cpp     # 调试信息测试
  ```

- **E6.5.2.2** 创建 lit 回归测试：
  ```
  tests/lit/CodeGen/
  ├── basic-types.test             # 类型映射验证
  ├── arithmetic.test              # 算术表达式 IR 验证
  ├── function-call.test           # 函数调用 IR 验证
  ├── control-flow.test            # if/for/while IR 验证
  ├── class-layout.test            # 类布局和字段访问
  ├── virtual-call.test            # 虚函数调用
  ├── inheritance.test             # 单/多重继承
  └── debug-info.test              # 调试信息验证
  ```

**测试覆盖重点：**

| 模块 | 核心测试用例 |
|------|-------------|
| CodeGenTypes | 22 种内建类型映射、指针、引用、数组、函数、结构体、枚举 |
| CodeGenConstant | 整数/浮点/字符串/布尔字面量、零值、空指针 |
| CodeGenExpr | 算术运算、比较运算、逻辑运算、赋值、函数调用、成员访问、类型转换 |
| CodeGenStmt | if/else、switch/case、for/while/do-while、break/continue/return |
| CodeGenFunction | 参数传递、返回值、局部变量、递归调用 |
| CGCXX | 类布局、构造/析构、虚函数表、虚调用、继承 |
| CGDebugInfo | 类型调试信息、函数调试信息、行号信息 |

**Checkpoint：** 测试覆盖率 ≥ 80%；所有测试通过

---

## 📋 Phase 6 验收检查清单

```
Stage 6.1 — IRGen 基础设施
[ ] CodeGenModule 类实现完成（两遍发射、全局变量/函数表）
[ ] CodeGenTypes 实现完成（22 种内建类型 + 所有派生类型映射）
[ ] CodeGenConstant 实现完成（字面量常量 + 聚合常量 + 零值）
[ ] TargetInfo 实现完成（类型大小/对齐 + DataLayout）
[ ] 所有头文件接口完整（方法、enum、classof、构造函数）

Stage 6.2 — 表达式代码生成
[ ] CodeGenFunction 类实现完成（IRBuilder + 局部变量映射 + 控制流栈）
[ ] 算术运算代码生成正确（整数 + 浮点，有符号/无符号区分）
[ ] 比较运算代码生成正确（ICmp/FCmp）
[ ] 逻辑运算代码生成正确（短路求值）
[ ] 函数调用代码生成正确（参数传递 + 返回值）
[ ] 成员访问代码生成正确（. 和 → + GEP）
[ ] 类型转换代码生成正确（static_cast/reinterpret_cast 等）

Stage 6.3 — 控制流代码生成
[ ] if/else if/else 代码生成正确
[ ] switch/case/default 代码生成正确
[ ] for 循环代码生成正确（含 init/cond/inc/body）
[ ] while 循环代码生成正确
[ ] do-while 循环代码生成正确
[ ] break/continue/return/goto 代码生成正确
[ ] 控制流栈管理正确（嵌套循环 break/continue）

Stage 6.4 — 函数与类代码生成
[ ] 函数体代码生成正确（参数 alloca + 局部变量 + 语句序列）
[ ] 类布局计算正确（字段偏移、大小、对齐、填充）
[ ] 构造函数代码生成正确（基类初始化 + 成员初始化 + 函数体）
[ ] 析构函数代码生成正确（成员析构 + 基类析构）
[ ] 虚函数表生成正确（vtable 布局 + vptr 初始化）
[ ] 虚函数调用正确（vtable 查找 + this 调整）
[ ] 继承处理正确（单一/多重继承的指针调整）

Stage 6.5 — 调试信息 + 测试
[ ] CGDebugInfo 实现完成（DIBuilder + 类型/函数/行号信息）
[ ] 生成的 IR 包含有效的 DWARF 元数据
[ ] 单元测试覆盖所有 CodeGen 子模块
[ ] lit 回归测试覆盖关键场景
[ ] 测试覆盖率 ≥ 80%
```

---

## 📁 Phase 6 新增文件清单

```
include/blocktype/CodeGen/
├── CodeGenModule.h              # 模块级代码生成引擎
├── CodeGenTypes.h               # C++ → LLVM 类型映射
├── CodeGenConstant.h            # 常量表达式代码生成
├── CodeGenFunction.h            # 函数级代码生成引擎
├── CGCXX.h                      # C++ 特有代码生成（类/构造/析构/VTable）
├── CGDebugInfo.h                # DWARF 调试信息生成
└── TargetInfo.h                 # 目标平台类型信息

src/CodeGen/
├── CMakeLists.txt               # CodeGen 模块构建文件
├── CodeGenModule.cpp            # 模块级代码生成实现
├── CodeGenTypes.cpp             # 类型映射实现
├── CodeGenConstant.cpp          # 常量生成实现
├── CodeGenFunction.cpp          # 函数级代码生成实现
├── CodeGenExpr.cpp              # 表达式代码生成（EmitBinaryOp/EmitUnaryOp/EmitCallExpr/...）
├── CodeGenStmt.cpp              # 控制流代码生成（EmitIf/EmitFor/EmitWhile/...）
├── CGCXX.cpp                    # C++ 特有代码生成实现
├── CGCXXClass.cpp               # 类布局和构造/析构
├── CGCXXVTable.cpp              # 虚函数表生成
├── CGDebugInfo.cpp              # 调试信息生成实现
└── TargetInfo.cpp               # 目标平台信息实现

include/blocktype/Basic/
└── DiagnosticIRGenKinds.def     # IRGen 阶段诊断 ID
```

---

*Phase 6 完成标志：能将 C++26 程序编译为 LLVM IR；生成的 IR 能被 LLVM 后端正确处理；所有接口完整（方法、enum、classof、构造函数），后续阶段的 AI 开发者可直接使用；调试信息完整；测试通过，覆盖率 ≥ 80%。*
