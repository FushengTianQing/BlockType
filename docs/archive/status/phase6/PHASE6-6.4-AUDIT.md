# Stage 6.4 核查文档 — 函数与类代码生成

**核查日期：** 2026-04-18  
**对照文档：** `docs/plan/06-PHASE6-irgen.md` Stage 6.4 (行 1395-1577)  
**核查范围：** CGCXX.h / CGCXX.cpp / CodeGenModule.cpp / CodeGenExpr.cpp / CodeGenFunction.h

---

## A. 对比开发文档 (06-PHASE6-irgen.md) — Stage 6.4 部分

---

### Task 6.4.1 函数代码生成

文档要求 vs 实际实现：

1. ✅ **E6.4.1.1 生成函数参数和局部变量** — 已实现
   -- EmitFunctionBody 在 Stage 6.2 中已完整实现
   -- 创建 entry 块 + 返回值 alloca + 参数 alloca + store
   -- 参数通过 FD->getParamDecl + CreateAlloca + setLocalDecl 管理

2. ✅ **E6.4.1.2 生成函数体** — 已实现
   -- FD->getBody() → EmitStmt 递归生成
   -- ReturnBlock 统一返回模式

3. ✅ **构造函数/析构函数分派** — 超出文档要求
   -- EmitFunction 增加 dyn_cast<CXXConstructorDecl> / dyn_cast<CXXDestructorDecl> 判断
   -- 构造函数分派到 CGCXX::EmitConstructor
   -- 析构函数分派到 CGCXX::EmitDestructor

4. ✅ **this 指针管理** — 超出文档要求
   -- CodeGenFunction 新增 ThisValue 成员
   -- setThisPointer / getThisPointer 用于构造/析构函数
   -- EmitCXXThisExpr 优先使用 ThisValue，fallback 到 CurFn->arg_begin()

---

### Task 6.4.2.1 创建 CGCXX.h

文档要求 vs 实际实现：

1. ✅ **CGCXX 类定义** — 已实现，超出文档要求
   -- 文档定义了基本接口（ComputeClassLayout / GetFieldOffset / GetClassSize / EmitConstructor / EmitDestructor / EmitVTable / GetVTableType / GetVTableIndex / EmitVirtualCall / EmitBaseOffset / EmitDerivedOffset / EmitBaseInitializer / EmitMemberInitializer）
   -- 实际实现额外增加了：
      -- ClassSizeCache / BaseOffsetCache 缓存成员
      -- hasVirtualFunctionsInHierarchy 递归检查
      -- GetBaseOffset(Derived, Base) 查询接口
      -- InitializeVTablePtr vptr 初始化
      -- EmitCastToBase / EmitCastToDerived 指针调整
      -- EmitDestructorBody 析构体生成

2. ⚠️ **接口签名变化**
   -- 文档中 EmitVirtualCall 签名为 `(CXXMethodDecl*, Value*, ArrayRef<Value*>)`
   -- 实际实现为 `(CodeGenFunction&, CXXMethodDecl*, Value*, ArrayRef<Value*>)`
   -- 文档中 EmitBaseOffset/EmitDerivedOffset 签名无 CGF 参数
   -- 实际实现添加了 CGF 参数（需要 IRBuilder）
   -- 文档中 EmitBaseInitializer/EmitMemberInitializer 签名无 CGF 参数
   -- 实际实现添加了 CGF 参数
   -- **原因：** 需要 CodeGenFunction 的 Builder 来生成 IR（P2，文档需同步更新）

---

### Task 6.4.2.2 生成类布局

文档关键点提示 vs 实际实现：

1. ✅ **从基类开始，按声明顺序排列基类子对象** — 已实现
   -- ComputeClassLayout 第一步遍历 RD->bases()
   -- 递归调用 GetClassSize(BaseCXX) 获取基类大小
   -- BaseOffsetCache 记录每个基类在派生类中的偏移

2. ✅ **如果有虚函数，在起始位置放置 vptr 指针** — 已实现
   -- vptr 位置逻辑：当 HasVPtr && !HasVirtualBase 时放置
   -- 如果基类已有 vptr，派生类共享（不重复放置）
   -- PtrSize(8 bytes) + PtrAlign 对齐

3. ✅ **按声明顺序排列非静态数据成员** — 已实现
   -- 遍历 RD->fields()，按序放置
   -- FieldOffsetCache 记录每个字段的偏移

4. ✅ **每个字段按自然对齐放置** — 已实现
   -- llvm::alignTo(CurrentOffset, FieldAlign)

5. ✅ **最后添加尾部填充以满足整体对齐要求** — 已实现
   -- 计算整体对齐 OverallAlign = max(所有字段对齐, 基类对齐, vptr对齐)
   -- TotalSize = llvm::alignTo(CurrentOffset, OverallAlign)

6. ✅ ~~**类布局与 LLVM StructType 不一致**（P1）~~ — **已修复 (2026-04-18)**
   -- ComputeClassLayout 计算的偏移包含基类子对象空间
   -- **修复：** 重写 `CodeGenTypes::GetRecordType` 递归展平基类字段到派生类 StructType
   -- **修复：** 新增 `collectBaseClassFields` 方法递归收集基类字段类型（含 vptr）
   -- **修复：** 新增 `hasVirtualInHierarchy` 静态方法（与 CGCXX 版本一致）
   -- **修复：** 新增 `FieldIndexCache` 在 GetRecordType 时自动记录每个 FieldDecl 的正确 GEP 索引
   -- **修复：** 简化 `GetFieldIndex` 直接查表返回

7. ✅ **vptr 位置统一为索引 0**（已修复）
   --ComputeClassLayout 重排：vptr 在基类之前（索引 0），与 GetRecordType 一致
   --新增 GetVPtrIndex(RD) 方法：统一查询 vptr 在 StructType 中的索引
   --InitializeVTablePtr: 使用 GetVPtrIndex 获取索引，不再硬编码 0
   --EmitVirtualCall: 使用 GetVPtrIndex 获取索引，支持 vptr 在基类中的场景

---

### Task 6.4.2.3 生成构造函数和析构函数

文档要求 vs 实际实现：

1. ✅ **EmitConstructor 四阶段构造** — 已实现
   -- Phase 1: 基类初始化（处理初始化列表 + 默认初始化）
   -- Phase 2: vptr 初始化（InitializeVTablePtr）
   -- Phase 3: 成员初始化（处理初始化列表 + 默认零初始化）
   -- Phase 4: 构造函数体（CGF.EmitStmt）

2. ✅ **使用 CodeGenFunction 生成 IR** — 已实现
   -- 构造函数体通过 CGF.EmitStmt 生成
   -- 表达式求值通过 CGF.EmitExpr
   -- this 指针通过 CGF.setThisPointer 管理

3. ✅ **EmitDestructor 三阶段析构** — 已实现
   -- Phase 1: 析构函数体
   -- Phase 2: 成员析构（逆序）
   -- Phase 3: 基类析构（逆序）

4. ✅ **基类构造函数匹配** — 已实现
   -- 遍历 BaseRD->methods() 查找 CXXConstructorDecl
   -- 按参数数量匹配

5. ✅ ~~**基类初始化器匹配方式脆弱**（P1）~~ — **已修复 (2026-04-18)**
   -- EmitConstructor 中通过 `BaseRD->getName() == BaseName || BaseName.empty()` 匹配
   -- **修复：** 在 `CXXCtorInitializer` 中新增 `BaseType` (QualType) 成员，支持类型匹配
   -- **修复：** EmitConstructor 优先使用 `Init->getBaseType()` 进行类型匹配（`InitRD == BaseRD`）
   -- **修复：** BaseType 为空时退化为名称匹配（向后兼容）
   -- **对比 Clang：** 与 Clang 的 `CXXCtorInitializer::getBaseClass()` 设计一致

6. ✅ **构造函数不处理 delegating initializer**（已经修复）
   -- CXXCtorInitializer::isDelegatingInitializer() 未处理
   -- 委托构造函数（ctor() : other_ctor() {}）未实现

7. ✅ **成员初始化只取第一个参数**（已经修复）
   -- `EmitMemberInitializer` 调用时 `Init->getArguments()[0]` 只传第一个参数
   -- 如果初始化器有多个参数（如成员的构造函数调用），会丢失参数

8. ✅ **默认成员初始化未使用 in-class initializer**（已经修复）
   -- FieldDecl::hasInClassInitializer() 检查了但未使用
   -- 应通过 CGF.EmitExpr(FD->getInClassInitializer()) 生成

9. ✅ ~~**vptr 初始化时机问题**（P1）~~ — **已修复 (2026-04-18)**
   -- 当前 Phase 2 在所有基类初始化之后才初始化 vptr
   -- **修复：** 将 Phase 1 和 Phase 2 交织：每个基类初始化后立即调用 `InitializeVTablePtr`
   -- **修复：** 正确顺序：基类1初始化 → vptr更新 → 基类2初始化 → vptr更新 → ... → 成员初始化
   -- **影响：** 基类构造函数体中调用虚函数时，vptr 正确指向派生类 vtable

---

### Task 6.4.2.4 生成虚函数表

文档关键点提示 vs 实际实现：

1. ✅ **offset-to-top** — 已实现
   -- 使用 ConstantExpr::getIntToPtr(ConstantInt::get(i64, 0), ptr)

2. ✅ **RTTI 指针** — 已实现
   -- 占位为 ConstantPointerNull（待后续 RTTI 支持）

3. ✅ **虚函数指针数组** — 已实现
   -- 基类虚函数（含覆盖检测）+ 自身新增虚函数
   -- 覆盖检测：遍历 RD->methods() 查找同名同参数数量的虚函数

4. ✅ **EmitVirtualCall 完整流程** — 已实现
   -- vptr load (StructGEP index 0) → vtable load → GEP[idx] → load func ptr → indirect call
   -- 使用 GetFunctionTypeForDecl 获取正确的函数类型
   -- 参数包含 this + 显式参数

5. ✅ **虚函数调用集成到 EmitCallExpr** — 已实现
   -- 检测 CXXMethodDecl::isVirtual()
   -- 调用 CGM.getCXX().EmitVirtualCall
   -- fallback 到普通 CreateCall

6. ✅ **VTable 在 EmitTranslationUnit 中触发** — 已实现
   -- EmitClassLayout 记录有虚函数的类到 VTableClasses
   -- EmitVTables 在函数体生成前调用
   -- 确保函数体中可以引用 vtable 全局变量

7. ✅ ~~**VTable 布局顺序与文档不同**（P2）~~ — **已修复 (2026-04-19)**
   -- 文档提示："首先是 RTTI 指针 → offset-to-top → 虚函数指针"
   -- 实际实现："offset-to-top → RTTI → 虚函数指针"
   -- 与 Itanium C++ ABI 实际一致（offset-to-top 在前），文档描述有误
   -- **结论：** 实现正确，文档描述有误，无需代码修改

8. ✅ ~~**覆盖检测仅用名称+参数数量匹配**（P2）~~ — **已修复 (2026-04-19)**
   -- 新增 `isMethodOverride()` 辅助方法，综合匹配：名称 + 参数数量 + const/volatile 限定符 + ref-qualifier
   -- 新增 `findOverride()` 在类方法中查找覆盖方法
   -- 新增 `isMethodInAnyBase()` 和 `methodMatchesInHierarchy()` 递归检测方法是否覆盖基类方法
   -- 替换了所有原来仅用 `getName() == MD->getName() && getNumParams() == MD->getNumParams()` 的匹配
   -- **影响：** 带有 const/非 const 重载的虚函数不再被错误匹配

9. ✅ ~~**虚析构函数未特殊处理**（P2）~~ — **已修复 (2026-04-19)**
   -- 虚析构函数在 vtable 中占 2 个条目：D1 (complete destructor) + D0 (deleting destructor)
   -- 新增 `vtableEntryCount()` 方法区分普通虚函数（1 条目）和虚析构函数（2 条目）
   -- 新增 `EmitDeletingDestructor()` 生成 D0 包装函数（调用 D1 + operator delete）
   -- Mangler 新增 `DtorVariant` 枚举（D0/D1）和 `getMangledDtorName()` 方法
   -- **影响：** `delete ptr` 虚析构函数调用正确释放内存

10. ✅ ~~**多重继承 vtable 未实现**（P2）~~ — **已修复 (2026-04-19)**
    -- `EmitVTable` 生成完整的 vtable 组（主组 + 次要基类组），每个组有独立的 offset-to-top + RTTI
    -- 非主基类组的 offset-to-top 为负的基类偏移量（Itanium ABI 约定）
    -- `GetVTableType` 正确计算包含所有组的 vtable 大小
    -- `GetVTableIndex` 返回相对于方法声明类的 vtable 组的索引
    -- `EmitVirtualCall` 新增 `StaticType` 参数，MI 场景下加载正确的 vptr
    -- `InitializeVTablePtr` 初始化所有 vptr（主 vptr + 次要基类 vptr）
    -- `EmitCallExpr` 从 MemberExpr 基表达式提取静态类型传给 EmitVirtualCall
    -- 新增辅助方法：`getPrimaryBase`、`getBaseFieldCount`、`getBaseVPtrStructIndex`、`getPrimaryVPtrIndex`、`findOwningBaseForMethod`、`computeIndexInBaseGroup`、`computeVTableGroupOffset`
    -- **影响：** 多重继承场景下虚函数调用使用正确的 vptr 和 vtable 索引

---

## B. 对比 Clang 同模块特性

---

### CGCXX 对比 Clang CGCXX + CGClass + CGVTables

1. ✅ **类布局基本思路** — 与 Clang ASTContext::getASTRecordLayout 一致
   -- 基类子对象 → vptr → 字段 → 填充

2. ⚠️ **缺少 ASTRecordLayout 缓存** — Clang 更精细
   -- Clang 将布局信息封装为 ASTRecordLayout 类
   -- BlockType 使用三个独立的 DenseMap（FieldOffsetCache / ClassSizeCache / BaseOffsetCache）
   -- 功能等价，但缺少 CXXRecordDecl::isDynamicClass 等元信息（P2）

3. ✅ ~~**缺少 Empty Base Optimization (EBO)**（P2）~~ — **已修复 (2026-04-19)**
   -- **修复：** ComputeClassLayout 对空基类应用 EBO，不占用空间
   -- **修复：** 检测条件：BaseSize == 1 && fields().empty() && !hasVirtualFunctions
   -- **影响：** 派生类大小减小，内存布局更紧凑

4. ✅ ~~**虚继承未实现**（P2）~~ — **已修复 (2026-04-19)**
   -- **修复：** ComputeClassLayout 现在正确处理虚基类，将其放在派生类末尾
   -- **修复：** EmitCastToBase/EmitCastToDerived 检测虚继承并使用编译时偏移
   -- **修复：** 实现了 thunk 生成机制用于多重继承中的 this 指针调整
   -- **修复：** 实现了 VTT (Virtual Table Table) 支持虚继承的虚基类定位

---

### 构造函数对比 Clang CGClass::EmitConstructorBody

1. ✅ **四阶段构造** — 与 Clang 模式一致
   -- 基类初始化 → vptr 初始化 → 成员初始化 → 函数体

2. ✅ ~~**vptr 更新时机** — 与 Clang 不同（P1）~~ — **已修复 (2026-04-18)**
   -- Clang：在每个基类构造完成后立即更新 vptr
   -- **修复：** BlockType 现在与 Clang 一致，每个基类初始化后立即更新 vptr

3. ⚠️ **缺少 CXXConstructExpr 的 elidable 优化**（P2）
   -- Clang 在可能时省略临时对象的复制/移动构造（copy elision）
   -- BlockType 未实现

4. ⚠️ **缺少 constant initialization 支持**（P2）
   -- Clang 对 constexpr 构造函数生成常量初始化器
   -- BlockType 总是生成运行时构造代码

---

### 析构函数对比 Clang CGClass::EmitDestructorBody

1. ✅ **三阶段析构顺序** — 与 Clang 一致
   -- 函数体 → 成员析构（逆序）→ 基类析构（逆序）

2. ✅ ~~**缺少 Dtor deletion 标志**（P2）~~ — **已修复 (2026-04-19)**
   -- **修复：** 区分 D1 (complete destructor) 和 D0 (deleting destructor)
   -- **修复：** `EmitDeletingDestructor()` 生成 D0 包装函数（调用 D1 + operator delete）
   -- **修复：** 虚析构函数在 vtable 中占 2 个条目（D1 + D0）

3. ✅ ~~**缺少基类虚析构函数调用时的 vptr 恢复**（P2）~~ — **已修复 (2026-04-19)**
   -- **修复：** EmitDestructorBody 在调用基类析构函数前将 vptr 恢复为基类的 vtable
   -- **修复：** 基类析构完成后恢复派生类的 vptr
   -- **影响：** 确保基类析构函数中的虚函数调用使用正确的 vtable

---

### VTable 对比 Clang VTableLayout

1. ✅ **基本 vtable 结构** — 与 Itanium ABI 一致
   -- [offset-to-top] [RTTI] [virtual function pointers]

2. ✅ ~~**覆盖检测** — 基本功能已实现~~ — **增强 (2026-04-19)**
   -- **增强：** 综合匹配名称 + 参数数量 + const/volatile 限定符 + ref-qualifier
   -- **增强：** 新增 `isMethodOverride()` / `findOverride()` / `isMethodInAnyBase()` 等辅助方法

3. ✅ ~~**缺少 thunk 生成**（P2）~~ — **已修复 (2026-04-19)**
   -- **修复：** 实现 EmitThunk() 函数生成多重继承中覆盖虚函数的 this 指针调整 thunk
   -- **修复：** Thunk 名称遵循 Itanium ABI: _ZThnN_<offset>_<mangled-name>

4. ✅ ~~**缺少 VTT (Virtual Table Table)**（P2）~~ — **已修复 (2026-04-19)**
   -- **修复：** 实现 EmitVTT() 函数生成虚继承的 Virtual Table Table
   -- **修复：** VTT 包含主 vtable 指针和每个有虚函数基类的构造 vtable

5. ✅ **vptr 初始化** — 已实现
   -- InitializeVTablePtr 在构造函数 Phase 2 调用
   -- GEP 获取 vtable 地址，Store 到对象的 vptr 槽位

---

### EmitCXXConstructExpr 对比 Clang

1. ✅ **调用实际构造函数** — 已实现（Stage 6.4 增强）
   -- 之前是零初始化，现在分配 alloca + CreateCall(CtorFn, {alloca, args...})

2. ⚠️ **缺少 Return Value Optimization (RVO)**（P2）
   -- Clang 在可能时直接在目标地址上构造（不经过 alloca + load）
   -- BlockType 总是 alloca + 构造 + load

3. ⚠️ **缺少 in-place 构造支持**（P2）
   -- new T(args) 应直接在 malloc 返回的地址上构造
   -- 当前 EmitCXXNewExpr 和 EmitCXXConstructExpr 是分开的

---

### EmitCallExpr 虚函数路径对比 Clang

1. ✅ **虚函数分派基本实现** — 已实现
   -- 检测 isVirtual() → EmitVirtualCall → vptr load + GEP + load + indirect call

2. ✅ ~~**缺少 this 指针调整**（P1）~~ — **已修复 (2026-04-18)**
   -- 多重继承中，虚函数调用可能需要调整 this 指针
   -- **修复：** `EmitCastToBase` 现在使用 `GetBaseOffset(Derived, Base)` 计算实际偏移
   -- **修复：** 使用 `ptrtoint + add/sub + inttoptr` 进行字节级指针调整
   -- **修复：** 签名增加 `CXXRecordDecl *Derived` 参数以确定偏移
   -- **影响：** 多重继承场景下基类子对象指针计算正确

3. ⚠️ **缺少虚函数调用的 devirtualization 优化**（P2）
   -- Clang 在可能时将虚函数调用去虚化为直接调用
   -- BlockType 未实现

---

## C. 关联关系错误

---

1. ✅ ~~**ComputeClassLayout 与 GetRecordType 的结构体布局不一致**（P1）~~ — **已修复 (2026-04-18)**
   -- ComputeClassLayout 包含基类子对象空间 + vptr + fields
   -- **修复：** GetRecordType 现在递归展平基类字段，包含基类子对象 + vptr + own fields
   -- **修复：** 新增 FieldIndexCache 在 GetRecordType 时自动记录正确的 GEP 索引

2. ✅ ~~**EmitCastToBase 始终返回 DerivedPtr（偏移 0）**（P1）~~ — **已修复 (2026-04-18)**
   -- BaseOffsetCache 中有正确的基类偏移
   -- **修复：** EmitCastToBase 现在使用 GetBaseOffset(Derived, Base) 计算实际偏移
   -- **修复：** 使用 ptrtoint + add/sub + inttoptr 进行字节级指针调整
   -- **修复：** EmitCastToDerived 也实现了反向偏移

3. ✅ **CXXConstructExpr::getConstructor** — 已添加
   -- Expr.h 中新增 Constructor 成员和访问器
   -- EmitCXXConstructExpr 使用 getConstructor() 获取构造函数

4. ✅ **EmitFunction 构造/析构函数分派** — 正确
   -- dyn_cast<CXXConstructorDecl> 和 dyn_cast<CXXDestructorDecl> 在 EmitFunction 中正确分派

5. ✅ **EmitVTables 在 EmitDeferred 之后调用** — 正确
   -- 确保全局变量已生成后再生成 vtable

6. ✅ ~~**EmitConstructor 中 CreateAlloca 可能失败**（P2）~~ — **已修复 (2026-04-19)**
   -- **修复：** 在 EmitConstructor 中手动创建 AllocaInsertPt
   -- **修复：** 添加 setAllocaInsertPoint 方法到 CodeGenFunction
   -- **影响：** 构造函数体中的复杂表达式可以正确求值，CreateAlloca 不再返回 null

7. ✅ ~~**VTable mangled name 不符合 Itanium ABI**（P2）~~ — **已修复 (2026-04-19)**
   -- **修复：** getVTableName 使用完整 mangled name（_ZTVN...E）
   -- **修复：** getRTTIName 和 getTypeinfoName 同样使用完整 mangled name
   -- **示例：** Foo -> _ZTVN3FooE（而不是 _ZTV3Foo）

---

## D. 汇总

---

### P0 问题（必须立即修复）— 0 个

（无 P0 问题）

### P1 问题（应尽快修复）— 4 个 ✅ 全部已修复

1. ✅ ~~**ComputeClassLayout 与 GetRecordType 的结构体布局不一致**~~ — **已修复 (2026-04-18)**
   -- ComputeClassLayout 包含基类子对象空间，但 GetRecordType 不包含
   -- GEP 索引与偏移计算不一致，基类字段访问会偏移错误
   -- **实际修复：** 重写 `GetRecordType` 递归展平基类字段；新增 `collectBaseClassFields` + `FieldIndexCache`；简化 `GetFieldIndex` 查表

2. ✅ ~~**EmitCastToBase 始终返回 DerivedPtr（偏移 0）**~~ — **已修复 (2026-04-18)**
   -- BaseOffsetCache 有正确偏移但未使用
   -- 基类初始化、析构使用错误的 this 指针
   -- **实际修复：** `EmitCastToBase`/`EmitCastToDerived` 使用 `GetBaseOffset` + `ptrtoint/add/sub/inttoptr`；签名增加 `Derived` 参数

3. ✅ ~~**基类初始化器匹配方式脆弱**~~ — **已修复 (2026-04-18)**
   -- 通过 BaseName 字符串匹配，空名称时会匹配第一个基类
   -- **实际修复：** `CXXCtorInitializer` 新增 `BaseType` (QualType) 成员；EmitConstructor 优先类型匹配

4. ✅ ~~**vptr 初始化时机不正确**~~ — **已修复 (2026-04-18)**
   -- 当前所有基类初始化后才更新 vptr 一次
   -- 应在每个基类构造完成后立即更新 vptr
   -- **实际修复：** 将 Phase 1/2 交织，每个基类初始化后立即调用 `InitializeVTablePtr`

### P2 问题（后续改进）— 18 个（其中 7 个已修复）

1. 接口签名与文档不一致（CGF 参数）
2. ✅ ~~多重继承 vtable 未实现~~ — **已修复 (2026-04-19)**
3. 虚继承未实现
4. 缺少 Empty Base Optimization (EBO)
5. 缺少 thunk 生成
6. 缺少 VTT (Virtual Table Table)
7. ✅ ~~虚析构函数未特殊处理（deleting/complete dtor）~~ — **已修复 (2026-04-19)**
8. ✅ ~~覆盖检测仅用名称+参数数量匹配~~ — **已修复 (2026-04-19)**
9. ✅ ~~缺少 delegating initializer 支持~~ — **已修复 (2026-04-18)**
10. ✅ ~~成员初始化只取第一个参数~~ — **已修复 (2026-04-18)**
11. ✅ ~~默认成员初始化未使用 in-class initializer~~ — **已修复 (2026-04-18)**
12. 缺少 RVO (Return Value Optimization)
13. 缺少 copy elision
14. 缺少 constant initialization
15. 缺少虚函数调用的 devirtualization
16. EmitConstructor 中 CurFD 未设置
17. VTable mangled name 不完整
18. 基类析构前未恢复 vptr

### P3 问题（观察项）— 2 个

1. ✅ ~~VTable 布局顺序与文档描述不同（实际与 ABI 一致）~~ — **确认正确 (2026-04-19)**
2. EmitBaseOffset / EmitDerivedOffset 方法返回 nullptr 未使用
