# Task B.7 优化版：IREmitCXX（C++ 特有发射器，4 子文件）

> 生成时间：2026-04-26 | 基于 PhaseB.md 第 814-958 行原始规格
> 所有 API 签名已对照实际代码库验证

---

## 1. 原始规格修正记录

| # | 原始描述 | 实际 API | 修正说明 |
|---|---------|---------|---------|
| 1 | 构造函数 `IREmitCXX(ASTToIRConverter& C, ir::IRBuilder& B)` | `IREmitCXX(ASTToIRConverter& C)` — 仅单参数 | B.5/B.6 已统一为单参数模式，Builder 通过 `Converter_.getBuilder()` 获取 |
| 2 | 同上 `IREmitCXXLayout(ASTToIRConverter& C)` | ✅ 正确 | Layout 不直接操作 Builder |
| 3 | 同上 `IREmitCXXCtorDtor(ASTToIRConverter& C, ir::IRBuilder& B)` | `IREmitCXXCtorDtor(ASTToIRConverter& C)` | 同理通过 Converter 获取 Builder |
| 4 | 同上 `IREmitCXXVTable(ASTToIRConverter& C, ir::IRBuilder& B)` | `IREmitCXXVTable(ASTToIRConverter& C)` | 同上 |
| 5 | 同上 `IREmitCXXInherit(ASTToIRConverter& C, ir::IRBuilder& B)` | `IREmitCXXInherit(ASTToIRConverter& C)` | 同上 |
| 6 | `ComputeClassLayout` 返回 `ir::IRStructType*` | 应返回 `void`，副作用创建 `IRStructType` | CGCXX 实际返回 `SmallVector<uint64_t, 16>`（字段偏移列表），IRStructType 通过 `IRTypeContext::getStructType()` 创建 |
| 7 | `CXXDynamicCastExpr` 无 `getDestType()` | ✅ `getDestType()` 存在于 `AST/Expr.h:1089` | 返回 `QualType`，已验证 |
| 8 | `CXXCatchStmt` 无 `getCaughtType()` | `getExceptionDecl()` → `VarDecl*`，需通过 `VarDecl::getType()` 获取类型 | 无直接 `getCaughtType()`，间接获取 |
| 9 | `IRMangler` 构造函数为 `IRMangler(ASTContext&, const TargetLayout&)` | 实际为 `IRMangler(const ir::TargetLayout&)` | B.8 实现中去掉了 ASTContext 参数（AST 信息直接从节点 getter 获取） |
| 10 | `IRModule::getGlobalVariable()` 按名称查找 | ✅ `getGlobalVariable(StringRef Name)` 存在 | `IRModule.h:131` |
| 11 | `ir::IRStructType` 无 `addField()` | 使用 `setBody(SmallVector<IRType*, 16>)` 一次性设置 | IRStructType 是一次性构造的，不能逐步 addField |
| 12 | VTable 类型声明为 `IROpaqueType` | ✅ `IROpaqueType` 存在于 `IR/Type.h:213` | 构造 `IROpaqueType("vtable")` 即可 |

---

## 2. API 速查表

### 2.1 AST C++ 类型

| 类 | 关键 API | 头文件位置 |
|----|---------|-----------|
| `CXXRecordDecl` | `getName()`, `getParent()`, `bases()`, `methods()`, `fields()`, `getDeclContext()`, `isLambda()`, `hasVTable()` | `AST/Decl.h:647` |
| `CXXMethodDecl` | `getParent()` → `CXXRecordDecl*`, `isVirtual()`, `isConst()`, `isStatic()`, `isPureVirtual()`, `getRefQualifier()`, `getAccess()`, `getParams()` | `AST/Decl.h:754` |
| `CXXConstructorDecl` | `getParent()`, `initializers()`, `isExplicit()`, `getNumInitializers()` | `AST/Decl.h:880` |
| `CXXDestructorDecl` | 继承 `CXXMethodDecl`，构造需 `(Loc, Parent, Body)` | `AST/Decl.h:912` |
| `CXXCtorInitializer` | `getMemberName()`, `getArguments()`, `isBaseInitializer()`, `isDelegatingInitializer()`, `isMemberInitializer()`, `getBaseType()` | `AST/Decl.h:846` |
| `FieldDecl` | `getName()`, `getType()`, `isMutable()`, `getBitWidth()`, `getParent()` | `AST/Decl.h:393` |
| `CXXDynamicCastExpr` | `getSubExpr()`, `getDestType()` → `QualType` | `AST/Expr.h:1082` |
| `CXXCatchStmt` | `getExceptionDecl()` → `VarDecl*`, `getHandlerBlock()`, `isCatchAll()` | `AST/Stmt.h:497` |
| `CXXConstructExpr` | `getConstructor()`, `getArgs()`, `getNumArgs()` | `AST/Expr.h:770` |
| `CXXMemberCallExpr` | 继承 `CallExpr`，`getCallee()`, `getArgs()` | `AST/Expr.h:752` |
| `BaseSpecifier` (嵌套于 CXXRecordDecl) | `getType()` → `QualType`, `isVirtual()`, `isBaseOfClass()`, `getAccessSpecifier()` | `AST/Decl.h:650` |

### 2.2 IR 层类型

| 类 | 关键 API | 头文件位置 |
|----|---------|-----------|
| `IRStructType` | `getName()`, `getElements()`, `getNumFields()`, `getFieldType(i)`, `setBody(Elems)`, `getFieldOffset(i, Layout)` | `IR/Type.h:144` |
| `IRGlobalVariable` | `getName()`, `getType()`, `setInitializer(C)`, `isConstant()`, `getAlignment()` | `IR/Module.h:56` |
| `IROpaqueType` | `getName()` — 占位类型 | `IR/Type.h:213` |
| `IRModule` | `getOrInsertGlobal(Name, Ty)`, `getGlobalVariable(Name)`, `getOrInsertFunction(Name, FTy)`, `addGlobal(GV)`, `getTypeContext()` | `IR/Module.h:95` |
| `IRFunction` | `addBasicBlock(Name)`, `getEntryBlock()`, `isDefinition()`, `getArg(i)`, `getNumArgs()`, `getReturnType()` | `IR/Function.h:38` |
| `IRBuilder` | `createAlloca(Ty)`, `createLoad(Ty, Ptr)`, `createStore(Val, Ptr)`, `createGEP(SrcTy, Ptr, Indices)`, `createBitCast(V, DestTy)`, `createCall(Callee, Args)`, `createRet(IRValue*)`, `createRetVoid()`, `createCondBr(Cond, TrueBB, FalseBB)`, `createBr(DestBB)`, `createIntToPtr(V, Ty)`, `createPtrToInt(V, Ty)` | `IR/IRBuilder.h` |
| `IRTypeContext` | `getStructType(Name, Elems)`, `getOpaqueType(Name)`, `getPointerType(Pointee)`, `getInt8Ty()`, `getInt32Ty()`, `getInt64Ty()`, `getVoidType()`, `getStructTypeByName(Name)` | `IR/IRTypeContext.h` |
| `TargetLayout` | `getPointerSizeInBits()`, `getIntSizeInBits()`, `getTypeSizeInBits(T)`, `getTypeAlignInBits(T)` | `IR/TargetLayout.h` |
| `IRMangler` | `mangleVTable(RD)`, `mangleTypeInfo(RD)`, `mangleFunctionName(ND)`, `mangleThunk(MD)`, `mangleTypeInfoName(RD)` | `Frontend/IRMangler.h` |

### 2.3 ASTToIRConverter Accessors

```cpp
// 已有 accessors（ASTToIRConverter.h）
ir::IRBuilder& getBuilder();
ir::IRModule* getModule() const;
IRTypeMapper& getTypeMapper();
ir::IRContext& getIRContext();
ir::IRTypeContext& getTypeContext();
const ir::TargetLayout& getTargetLayout() const;
DiagnosticsEngine& getDiagnostics();
IREmitExpr* getExprEmitter();
IREmitStmt* getStmtEmitter();
IRMangler* getMangler() const;  // B.8 新增
```

---

## 3. 前置条件

### 3.1 已完成依赖
- **B.5** `IREmitExpr` ✅ — 表达式发射器（单参数构造 `IREmitExpr(ASTToIRConverter&)`）
- **B.6** `IREmitStmt` ✅ — 语句发射器（单参数构造 `IREmitStmt(ASTToIRConverter&)`）
- **B.8** `IRMangler` ✅ — 名称修饰器（构造 `IRMangler(const ir::TargetLayout&)`），已集成到 `ASTToIRConverter`

### 3.2 现有参考实现
- `src/CodeGen/CGCXX.cpp` — 2,229 行完整 C++ 代码生成实现
  - `ComputeClassLayout(RD)` → 字段偏移列表
  - `EmitConstructor(Ctor, Fn)` → 构造函数生成
  - `EmitDestructor(Dtor, Fn)` → 析构函数生成
  - `EmitVTable(RD)` → VTable 生成（含 layout 计算、全局变量创建）
  - `EmitDynamicCast(DCE)` → dynamic_cast 实现
- `include/blocktype/CodeGen/CGCXX.h` — CGCXX 类定义
- `tests/unit/CodeGen/ManglerTest.cpp` — 测试 fixture 模式

### 3.3 迁移策略
| CGCXX 概念 | IR EmitCXX 对应 | 关键差异 |
|-----------|----------------|---------|
| `CodeGenModule& CGM` | `ASTToIRConverter& Converter_` | 所有 CGM.xxx 调用改为 Converter_.xxx |
| `CGM.getTarget().getPointerSize()` | `Converter_.getTargetLayout().getPointerSizeInBits() / 8` | 单位不同：字节 vs 位 |
| `CGM.getModule()->getOrInsertGlobal()` | `Converter_.getModule()->getOrInsertGlobal()` | 直接等价 |
| `CGM.getMangler()` | `Converter_.getMangler()` | B.8 已集成 |
| `llvm::IRBuilder<>` | `ir::IRBuilder` | API 等价但类型不同 |
| `llvm::StructType*` | `ir::IRStructType*` | 创建方式不同 |
| `llvm::GlobalVariable*` | `ir::IRGlobalVariable*` | 创建方式不同 |
| `CodeGenFunction CGF` | 直接使用 `Converter_.getBuilder()` | 无需中间层 |

---

## 4. 产出文件清单

| 操作 | 文件路径 | 预估行数 | 说明 |
|------|---------|---------|------|
| 修改 | `include/blocktype/Frontend/IREmitCXX.h` | ~250 | 替换桩，定义 5 个类 |
| 新增 | `src/Frontend/IREmitCXXLayout.cpp` | ~400 | 布局计算 |
| 新增 | `src/Frontend/IREmitCXXCtorDtor.cpp` | ~600 | 构造/析构函数 |
| 新增 | `src/Frontend/IREmitCXXVTable.cpp` | ~700 | VTable/RTTI |
| 新增 | `src/Frontend/IREmitCXXInherit.cpp` | ~500 | 继承/多态 |
| 新增 | `tests/unit/Frontend/IREmitCXXTest.cpp` | ~300 | 单元测试 |
| 修改 | `src/Frontend/CMakeLists.txt` | ~5 | 添加 4 个源文件 |

---

## 5. 类型定义（已验证，单参数构造函数）

```cpp
// include/blocktype/Frontend/IREmitCXX.h

#pragma once

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/IR/ADT.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {
namespace frontend {

class ASTToIRConverter;

//===----------------------------------------------------------------------===//
// IREmitCXX — 顶层调度器
//===----------------------------------------------------------------------===//

/// IREmitCXX — Top-level C++ IR emission dispatcher.
///
/// Owns the four sub-emitters (Layout, CtorDtor, VTable, Inherit).
/// Created by ASTToIRConverter::initializeEmitters().
class IREmitCXX {
  ASTToIRConverter& Converter_;

  // Sub-emitters (owned)
  std::unique_ptr<class IREmitCXXLayout> LayoutEmitter_;
  std::unique_ptr<class IREmitCXXCtorDtor> CtorDtorEmitter_;
  std::unique_ptr<class IREmitCXXVTable> VTableEmitter_;
  std::unique_ptr<class IREmitCXXInherit> InheritEmitter_;

public:
  explicit IREmitCXX(ASTToIRConverter& C);
  ~IREmitCXX();

  // Non-copyable
  IREmitCXX(const IREmitCXX&) = delete;
  IREmitCXX& operator=(const IREmitCXX&) = delete;

  //===--- Top-level dispatch ---===//

  /// Emit C++ constructor into IRFn.
  void EmitCXXConstructor(CXXConstructorDecl* Ctor, ir::IRFunction* IRFn);

  /// Emit C++ destructor into IRFn.
  void EmitCXXDestructor(CXXDestructorDecl* Dtor, ir::IRFunction* IRFn);

  /// Emit VTable for the given class.
  void EmitVTable(const CXXRecordDecl* RD);

  /// Emit RTTI typeinfo for the given class.
  void EmitRTTI(const CXXRecordDecl* RD);

  /// Emit thunk for the given virtual method.
  void EmitThunk(const CXXMethodDecl* MD);

  //===--- Sub-emitter access ---===//

  IREmitCXXLayout& getLayoutEmitter() { return *LayoutEmitter_; }
  IREmitCXXCtorDtor& getCtorDtorEmitter() { return *CtorDtorEmitter_; }
  IREmitCXXVTable& getVTableEmitter() { return *VTableEmitter_; }
  IREmitCXXInherit& getInheritEmitter() { return *InheritEmitter_; }

private:
  /// Helper: check if a class has virtual functions in hierarchy.
  static bool hasVirtualFunctionsInHierarchy(const CXXRecordDecl* RD);
  static bool hasVirtualFunctions(const CXXRecordDecl* RD);
};

//===----------------------------------------------------------------------===//
// IREmitCXXLayout — 类布局计算
//===----------------------------------------------------------------------===//

/// IREmitCXXLayout — Computes class layout (field offsets, base offsets).
///
/// Port of CGCXX::ComputeClassLayout, GetFieldOffset, GetClassSize, etc.
/// Results are cached per CXXRecordDecl.
class IREmitCXXLayout {
  ASTToIRConverter& Converter_;

  /// Cache: CXXRecordDecl* → computed field offsets (in bytes)
  llvm::DenseMap<const CXXRecordDecl*, llvm::SmallVector<uint64_t, 16>> FieldOffsetsCache_;

  /// Cache: CXXRecordDecl* → class size (in bytes)
  llvm::DenseMap<const CXXRecordDecl*, uint64_t> ClassSizeCache_;

  /// Cache: {Derived, Base} → base offset (in bytes)
  llvm::DenseMap<std::pair<const CXXRecordDecl*, const CXXRecordDecl*>, uint64_t> BaseOffsetCache_;

  /// Cache: CXXRecordDecl* → IRStructType (IR representation)
  llvm::DenseMap<const CXXRecordDecl*, ir::IRStructType*> StructTypeCache_;

  /// Cache: CXXRecordDecl* → vptr field index in IRStructType
  llvm::DenseMap<const CXXRecordDecl*, unsigned> VPtrIndexCache_;

public:
  explicit IREmitCXXLayout(ASTToIRConverter& C);

  /// Compute and cache class layout. Creates IRStructType in IRTypeContext.
  /// Returns the computed IRStructType.
  ir::IRStructType* ComputeClassLayout(const CXXRecordDecl* RD);

  /// Get the field offset (in bytes) for a FieldDecl.
  uint64_t GetFieldOffset(const FieldDecl* FD);

  /// Get the total class size (in bytes).
  uint64_t GetClassSize(const CXXRecordDecl* RD);

  /// Get the non-virtual base offset (in bytes).
  uint64_t GetBaseOffset(const CXXRecordDecl* Derived, const CXXRecordDecl* Base);

  /// Get the virtual base offset (in bytes) — requires vtable lookup at runtime.
  /// Returns 0 for now; full implementation in Phase C.
  uint64_t GetVirtualBaseOffset(const CXXRecordDecl* Derived, const CXXRecordDecl* VBase);

  /// Get or create the IRStructType for a CXXRecordDecl.
  ir::IRStructType* GetOrCreateStructType(const CXXRecordDecl* RD);

  /// Get the vptr field index within the IRStructType.
  unsigned GetVPtrIndex(const CXXRecordDecl* RD);

  /// Check if class has virtual functions in hierarchy.
  static bool hasVirtualFunctionsInHierarchy(const CXXRecordDecl* RD);
  static bool hasVirtualFunctions(const CXXRecordDecl* RD);
};

//===----------------------------------------------------------------------===//
// IREmitCXXCtorDtor — 构造/析构函数生成
//===----------------------------------------------------------------------===//

/// IREmitCXXCtorDtor — Emits IR for C++ constructors and destructors.
///
/// Port of CGCXX::EmitConstructor, EmitDestructor, etc.
class IREmitCXXCtorDtor {
  ASTToIRConverter& Converter_;

public:
  explicit IREmitCXXCtorDtor(ASTToIRConverter& C);

  /// Emit a complete constructor into IRFn.
  void EmitConstructor(const CXXConstructorDecl* Ctor, ir::IRFunction* IRFn);

  /// Emit base class initializer (call base constructor).
  void EmitBaseInitializer(ir::IRValue* This, const CXXCtorInitializer* Init);

  /// Emit member initializer (initialize field).
  void EmitMemberInitializer(ir::IRValue* This, const CXXCtorInitializer* Init);

  /// Emit delegating constructor (call another ctor of same class).
  void EmitDelegatingConstructor(ir::IRValue* This, const CXXCtorInitializer* Init);

  /// Emit a complete destructor into IRFn.
  void EmitDestructor(const CXXDestructorDecl* Dtor, ir::IRFunction* IRFn);

  /// Emit destructor body (destroy members, call base dtor).
  void EmitDestructorBody(ir::IRValue* This, const CXXDestructorDecl* Dtor);

  /// Emit a call to a destructor on an object.
  void EmitDestructorCall(const CXXDestructorDecl* Dtor, ir::IRValue* Object);
};

//===----------------------------------------------------------------------===//
// IREmitCXXVTable — VTable/RTTI 生成
//===----------------------------------------------------------------------===//

/// IREmitCXXVTable — Emits VTable and RTTI as placeholder global variables.
///
/// Key design: VTables are emitted as IRGlobalVariable with IROpaqueType.
/// The actual byte layout will be filled by the backend (Phase C, Task C.8).
/// This layer only creates the global variable with the correct mangled name.
class IREmitCXXVTable {
  ASTToIRConverter& Converter_;

  /// Cache: CXXRecordDecl* → VTable IRGlobalVariable
  llvm::DenseMap<const CXXRecordDecl*, ir::IRGlobalVariable*> VTableCache_;

  /// Cache: CXXRecordDecl* → RTTI IRGlobalVariable
  llvm::DenseMap<const CXXRecordDecl*, ir::IRGlobalVariable*> RTTICache_;

  /// Cache: CXXMethodDecl* → vtable index
  llvm::DenseMap<const CXXMethodDecl*, uint64_t> VTableIndexCache_;

public:
  explicit IREmitCXXVTable(ASTToIRConverter& C);

  /// Emit VTable as a placeholder global variable.
  void EmitVTable(const CXXRecordDecl* RD);

  /// Get the VTable type (IROpaqueType placeholder).
  ir::IRType* GetVTableType(const CXXRecordDecl* RD);

  /// Get the vtable index for a virtual method.
  uint64_t GetVTableIndex(const CXXMethodDecl* MD);

  /// Initialize the vptr in an object's struct.
  /// Emits: store @vtable, GEP(object, 0, vptrIndex)
  void InitializeVTablePtr(ir::IRValue* Object, const CXXRecordDecl* RD);

  /// Emit VTable initialization code (store function pointers into vtable).
  /// In IR layer, this is a no-op (placeholder). Actual init in Phase C.
  void EmitVTableInitialization(const CXXRecordDecl* RD);

  /// Emit RTTI typeinfo as a placeholder global variable.
  void EmitRTTI(const CXXRecordDecl* RD);

  /// Emit catch typeinfo (used by exception handling).
  void EmitCatchTypeInfo(const CXXCatchStmt* CS);

  /// Get or create VTable global variable.
  ir::IRGlobalVariable* GetOrCreateVTable(const CXXRecordDecl* RD);
};

//===----------------------------------------------------------------------===//
// IREmitCXXInherit — 继承/多态
//===----------------------------------------------------------------------===//

/// IREmitCXXInherit — Emits IR for inheritance-related operations.
///
/// Port of CGCXX's cast/thunk/VTT emission.
class IREmitCXXInherit {
  ASTToIRConverter& Converter_;

public:
  explicit IREmitCXXInherit(ASTToIRConverter& C);

  /// Emit pointer adjustment from derived to base (upcast).
  /// Returns adjusted pointer (This + baseOffset).
  ir::IRValue* EmitCastToBase(ir::IRValue* Object, const CXXRecordDecl* Derived,
                               const CXXRecordDecl* Base);

  /// Emit pointer adjustment from base to derived (downcast).
  /// Returns adjusted pointer (This - baseOffset). Use with caution.
  ir::IRValue* EmitCastToDerived(ir::IRValue* Object, const CXXRecordDecl* Base,
                                  const CXXRecordDecl* Derived);

  /// Compute the byte offset from derived to base.
  uint64_t EmitBaseOffset(const CXXRecordDecl* Derived, const CXXRecordDecl* Base);

  /// Emit dynamic_cast runtime check.
  /// In IR layer, emits: load vptr, check RTTI, adjust pointer.
  ir::IRValue* EmitDynamicCast(ir::IRValue* Object, const CXXDynamicCastExpr* DCE);

  /// Emit a thunk function for virtual method adjustment.
  void EmitThunk(const CXXMethodDecl* MD);

  /// Emit VTT (Virtual Table Table) for virtual inheritance.
  void EmitVTT(const CXXRecordDecl* RD);
};

} // namespace frontend
} // namespace blocktype
```

---

## 6. 实现约束（已验证可行性）

### 6.1 不依赖 CodeGenModule

所有文件仅 `#include "blocktype/Frontend/ASTToIRConverter.h"`，不引入 `CodeGen/*.h`。

### 6.2 构造函数统一为单参数

```cpp
// 所有子发射器统一:
explicit IREmitCXXLayout(ASTToIRConverter& C);
explicit IREmitCXXCtorDtor(ASTToIRConverter& C);
explicit IREmitCXXVTable(ASTToIRConverter& C);
explicit IREmitCXXInherit(ASTToIRConverter& C);

// Builder 获取:
auto& Builder = Converter_.getBuilder();
```

### 6.3 VTable 占位模式

```cpp
// 创建 VTable 全局变量（占位）:
auto* OpaqueTy = Converter_.getTypeContext().getOpaqueType("vtable");
auto MangledName = Converter_.getMangler()->mangleVTable(RD);
auto* VTableGV = Converter_.getModule()->getOrInsertGlobal(MangledName, OpaqueTy);
VTableGV->setConstant(true);
```

### 6.4 类布局 → IRStructType 映射

```cpp
ir::IRStructType* IREmitCXXLayout::ComputeClassLayout(const CXXRecordDecl* RD) {
  // 1. 检查缓存
  auto It = StructTypeCache_.find(RD);
  if (It != StructTypeCache_.end()) return It->second;

  // 2. 收集字段类型列表
  llvm::SmallVector<ir::IRType*, 16> FieldTypes;
  auto& TypeCtx = Converter_.getTypeContext();
  auto& Layout = Converter_.getTargetLayout();

  // 2a. vptr (if needed)
  if (hasVirtualFunctionsInHierarchy(RD)) {
    FieldTypes.push_back(TypeCtx.getPointerType(TypeCtx.getInt8Ty()));
  }

  // 2b. non-virtual bases
  for (const auto& Base : RD->bases()) {
    if (Base.isVirtual()) continue;
    // ... 递归获取 BaseCXX → GetOrCreateStructType(BaseCXX) → add fields
  }

  // 2c. own fields
  for (const FieldDecl* FD : RD->fields()) {
    auto* IRFieldTy = Converter_.getTypeMapper().mapType(FD->getType());
    FieldTypes.push_back(IRFieldTy);
  }

  // 2d. virtual bases (at end)
  // ...

  // 3. 创建 IRStructType
  auto* StructTy = TypeCtx.getStructType(RD->getName(), std::move(FieldTypes));
  StructTypeCache_[RD] = StructTy;

  // 4. 计算字段偏移（缓存）
  // ... (使用 Layout.getTypeSizeInBits / getTypeAlignInBits)

  return StructTy;
}
```

### 6.5 IRBuilder GEP 替代方案

IR 层 `createGEP` 需要 `IRValue*` 索引。对于编译时常量索引（如 vptr field index），需要先创建 `IRConstantInt`：

```cpp
// 获取 vptr 指针:
auto* Zero = ir::IRConstantInt::get(TypeCtx.getInt32Ty(), 0);
auto* VPtrIdx = ir::IRConstantInt::get(TypeCtx.getInt32Ty(), VPtrIndex);
auto* VPtrPtr = Builder.createGEP(StructTy, Object, {Zero, VPtrIdx}, "vptr");

// 存储 vtable 地址:
auto* VTableRef = /* 创建指向 VTableGV 的常量引用 */;
Builder.createStore(VTableRef, VPtrPtr);
```

### 6.6 EmitConstructor 模式

```cpp
void IREmitCXXCtorDtor::EmitConstructor(const CXXConstructorDecl* Ctor,
                                          ir::IRFunction* IRFn) {
  auto& Builder = Converter_.getBuilder();
  auto* Class = Ctor->getParent();

  // 创建 entry BB
  auto* EntryBB = IRFn->addBasicBlock("entry");
  Builder.setInsertPoint(EntryBB);

  // this 指针 = 第一个参数
  ir::IRValue* This = IRFn->getArg(0);

  // 1. 初始化 vptr（如果有虚函数）
  if (IREmitCXXLayout::hasVirtualFunctionsInHierarchy(Class)) {
    Converter_.getVTableEmitter().InitializeVTablePtr(This, Class);
  }

  // 2. 基类初始化器
  for (const CXXCtorInitializer* Init : Ctor->initializers()) {
    if (Init->isBaseInitializer()) {
      EmitBaseInitializer(This, Init);
    }
  }

  // 3. 成员初始化器
  for (const CXXCtorInitializer* Init : Ctor->initializers()) {
    if (Init->isMemberInitializer()) {
      EmitMemberInitializer(This, Init);
    }
  }

  // 4. 委托构造函数（如果有）
  for (const CXXCtorInitializer* Init : Ctor->initializers()) {
    if (Init->isDelegatingInitializer()) {
      EmitDelegatingConstructor(This, Init);
    }
  }

  // 5. 构造函数体
  if (Ctor->getBody()) {
    Converter_.getStmtEmitter()->EmitCompoundStmt(
        llvm::cast<CompoundStmt>(Ctor->getBody()));
  }

  // 6. return void
  Builder.createRetVoid();
}
```

---

## 7. 验收标准

### V1: 类布局计算
```cpp
ASTContext Ctx;
ir::IRTypeContext TypeCtx;
ir::TargetLayout Layout("arm64-apple-macosx14.0");
// ... 创建 ASTToIRConverter ...
auto* RD = Ctx.create<CXXRecordDecl>(SourceLocation(), "Foo", TagDecl::TK_class);
auto* IntTy = Ctx.getBuiltinType(BuiltinKind::Int);
auto* F1 = Ctx.create<FieldDecl>(SourceLocation(), "x", QualType(IntTy, Qualifier::None));
RD->addField(F1);

auto* StructTy = CXXLayout.ComputeClassLayout(RD);
assert(StructTy != nullptr);
assert(llvm::isa<ir::IRStructType>(StructTy));
assert(StructTy->getName() == "Foo");
```

### V2: 构造函数生成
```cpp
auto* Ctor = Ctx.create<CXXConstructorDecl>(SourceLocation(), RD,
    llvm::ArrayRef<ParmVarDecl*>{});
auto* IRFn = Converter.getModule()->getOrInsertFunction("_ZN3FooC1Ev", FTy);

CtorDtor.EmitConstructor(Ctor, IRFn);
assert(IRFn->isDefinition());
assert(IRFn->getEntryBlock() != nullptr);
```

### V3: VTable 占位全局变量
```cpp
VTableEmitter.EmitVTable(RD);
auto* VTableGV = Converter.getModule()->getGlobalVariable("_ZTV3Foo");
assert(VTableGV != nullptr);
assert(llvm::isa<ir::IROpaqueType>(VTableGV->getType()));
```

### V4: 继承偏移计算
```cpp
// Derived 继承 Base
auto* Base = Ctx.create<CXXRecordDecl>(SourceLocation(), "Base", TagDecl::TK_class);
auto* Derived = Ctx.create<CXXRecordDecl>(SourceLocation(), "Derived", TagDecl::TK_class);
// ... add base specifier ...

uint64_t Offset = Inherit.EmitBaseOffset(Derived, Base);
// Base offset should be non-negative
assert(Offset >= 0);
```

---

## 8. 测试方案

### 8.1 测试文件
`tests/unit/Frontend/IREmitCXXTest.cpp`

### 8.2 测试 Fixture
```cpp
class IREmitCXXTest : public ::testing::Test {
protected:
  ASTContext ASTCtx;
  ir::IRContext IRCtx;
  ir::IRTypeContext TypeCtx;
  ir::TargetLayout Layout;
  DiagnosticsEngine Diags;
  std::unique_ptr<ASTToIRConverter> Converter;

  IREmitCXXTest()
    : Layout("arm64-apple-macosx14.0"),
      Diags(/* ... */) {
    // 创建 ASTToIRConverter
    Converter = std::make_unique<ASTToIRConverter>(IRCtx, TypeCtx, Layout, Diags);
  }

  // Helper: 创建简单 CXXRecordDecl
  CXXRecordDecl* makeCXXRecord(llvm::StringRef Name) {
    auto* RD = ASTCtx.create<CXXRecordDecl>(SourceLocation(), Name, TagDecl::TK_class);
    RD->setCompleteDefinition(true);
    return RD;
  }

  // Helper: 添加字段
  FieldDecl* addField(CXXRecordDecl* RD, llvm::StringRef Name, QualType T) {
    auto* F = ASTCtx.create<FieldDecl>(SourceLocation(), Name, T);
    RD->addField(F);
    return F;
  }

  // Helper: 添加虚方法
  CXXMethodDecl* addVirtualMethod(CXXRecordDecl* RD, llvm::StringRef Name, QualType FTy) {
    auto* MD = ASTCtx.create<CXXMethodDecl>(SourceLocation(), Name, FTy,
        llvm::ArrayRef<ParmVarDecl*>{}, RD, nullptr,
        false, false, false, true /*isVirtual*/);
    RD->addMethod(MD);
    return MD;
  }

  // Helper: 添加基类
  void addBase(CXXRecordDecl* Derived, CXXRecordDecl* Base, bool IsVirtual = false) {
    auto BaseType = ASTCtx.getRecordType(Base);
    Derived->addBase(CXXRecordDecl::BaseSpecifier(
        BaseType, SourceLocation(), IsVirtual, false, 2));
  }
};
```

### 8.3 测试用例（10 个）

| # | 测试名 | 测试内容 | 预期 |
|---|--------|---------|------|
| 1 | `EmptyClass` | 空 CXXRecordDecl 布局 | IRStructType 无字段（或 1 字节 padding） |
| 2 | `SimpleField` | 1 个 int 字段 | IRStructType 含 1 个 int, 偏移=0 |
| 3 | `VirtualMethod` | 1 个虚方法 | IRStructType 首字段为 vptr (ptr) |
| 4 | `SingleInheritance` | Derived → Base 各 1 个字段 | Base 偏移正确 |
| 5 | `Constructor` | 简单构造函数 | IRFn 有 entry BB + ret void |
| 6 | `ConstructorWithInit` | 构造函数含成员初始化列表 | store 指令到正确偏移 |
| 7 | `Destructor` | 简单析构函数 | IRFn 有 entry BB + ret void |
| 8 | `VTablePlaceholder` | 有虚函数的类 | getGlobalVariable("_ZTV...") != null |
| 9 | `VTableIndex` | 虚方法在 vtable 中的索引 | index >= 2 (ott+RTTI 之后) |
| 10 | `CastToBase` | upcast 指针调整 | GEP + byte offset 计算 |

### 8.4 对比测试（推荐）

用相同的 AST 构造分别调用 `IREmitCXX` 和 `CGCXX`，比较：
- 类布局字段偏移列表
- VTable mangled name
- 基类偏移

---

## 9. 实现步骤

| 步骤 | 操作 | 文件 | 预估行数 |
|------|------|------|---------|
| 1 | 替换 IREmitCXX.h 桩为完整头文件（5 个类定义） | `include/blocktype/Frontend/IREmitCXX.h` | ~250 |
| 2 | 实现 IREmitCXXLayout（ComputeClassLayout + 缓存） | `src/Frontend/IREmitCXXLayout.cpp` | ~400 |
| 3 | 实现 IREmitCXXCtorDtor（EmitConstructor + EmitDestructor） | `src/Frontend/IREmitCXXCtorDtor.cpp` | ~600 |
| 4 | 实现 IREmitCXXVTable（EmitVTable 占位 + GetVTableIndex） | `src/Frontend/IREmitCXXVTable.cpp` | ~700 |
| 5 | 实现 IREmitCXXInherit（EmitCastToBase + EmitDynamicCast + EmitThunk） | `src/Frontend/IREmitCXXInherit.cpp` | ~500 |
| 6 | 更新 ASTToIRConverter 的 initializeEmitters() | `src/Frontend/ASTToIRConverter.cpp` | ~15 |
| 7 | 编写单元测试 | `tests/unit/Frontend/IREmitCXXTest.cpp` | ~300 |
| 8 | 更新 CMakeLists.txt | `src/Frontend/CMakeLists.txt` | ~5 |

---

## 10. 关键注意事项

### 10.1 ASTToIRConverter 集成

IREmitCXX 桩已存在于 `ASTToIRConverter`：
- 成员：`IREmitCXX* CXXEmitter_ = nullptr;` (ASTToIRConverter.h:85)
- 需要在 `initializeEmitters()` 中替换桩创建为完整实现

```cpp
// ASTToIRConverter::initializeEmitters() 中:
CXXEmitter_ = new IREmitCXX(*this);  // 替换现有的桩创建
```

### 10.2 IRStructType 创建注意事项

`IRTypeContext::getStructType(Name, Elems)` 如果同名已存在，会返回已有类型。对于类布局，需要确保：
- 首次创建时使用正确的字段列表
- 不允许多次创建同名但不同的 struct（会断言或返回旧类型）

### 10.3 IRBuilder 无 createConstGEP

当前 IRBuilder 只有 `createGEP(SrcTy, Ptr, ArrayRef<IRValue*> Indices)`。常量索引需要先创建 `IRConstantInt`：
```cpp
auto* Zero = ir::IRConstantInt::get(TypeCtx.getInt32Ty(), 0);
auto* Idx = ir::IRConstantInt::get(TypeCtx.getInt32Ty(), FieldIndex);
auto* FieldPtr = Builder.createGEP(StructTy, Object, {Zero, Idx});
```

### 10.4 VTable 全局变量常量引用

IR 层需要一种方式引用全局变量的地址。检查 `IRConstantGlobalRef` 是否可用：
- `IRConstantGlobalRef` 存在于 `IR/IRConstant.h:117`，接受 `IRGlobalVariable*`

### 10.5 IRMangler 构造函数差异

B.8 实际实现中 IRMangler 构造函数为 `IRMangler(const ir::TargetLayout&)`，去掉了 `ASTContext&` 参数。通过 `Converter_.getMangler()` 获取已创建的实例。

### 10.6 BaseSpecifier 的类型解析

`CXXRecordDecl::BaseSpecifier::getType()` 返回 `QualType`，需要从中提取 `CXXRecordDecl*`：
```cpp
for (const auto& Base : RD->bases()) {
  QualType BT = Base.getType();
  if (auto* RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
    if (auto* BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
      // 使用 BaseRD
    }
  }
}
```

---

## 11. CMake 变更

### `src/Frontend/CMakeLists.txt`
```cmake
# 新增源文件
IREmitCXXLayout.cpp
IREmitCXXCtorDtor.cpp
IREmitCXXVTable.cpp
IREmitCXXInherit.cpp
```

### `tests/unit/Frontend/CMakeLists.txt`
```cmake
# 新增测试文件
IREmitCXXTest.cpp
```
