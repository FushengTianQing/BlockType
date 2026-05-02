# 07 — 核心类型定义

## 7.1 Named trait — 注册表基础约束

```rust
// crates/bt-core/src/named.rs

/// 所有可注册组件必须实现的命名 trait
///
/// Frontend/Backend/Dialect/Pass 均继承此 trait，
/// 以满足 Registry<dyn X> 的 T: ?Sized + Named bound。
///
/// 注意：Named: Send + Sync 已在 03-Communication-Bus 3.8 节定义。
/// 具体类型 impl Named 时只需实现 fn name(&self) -> &str。
```

## 7.2 IR 核心类型

```rust
// crates/bt-ir/src/types.rs

#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum IRType {
    Void,
    Integer { bits: u32 },
    Float { bits: u32 },
    Pointer { pointee: Box<IRType> },
    Array { element: Box<IRType>, count: u64 },
    Struct {
        name: String,
        fields: Vec<StructField>,
        packed: bool,
    },
    Function {
        ret: Box<IRType>,
        params: Vec<IRType>,
        variadic: bool,
    },
    Opaque { name: String },
    Tuple { elements: Vec<IRType> },
    Never,

    // ─── 泛型与参数化类型 ───
    Generic {
        name: String,
        constraints: Vec<TraitBound>,
    },
    Parameterized {
        name: String,          // "Vec", "Option", "Result"
        args: Vec<IRType>,
    },
    TraitObject {
        traits: Vec<TraitBound>,
        lifetime: Option<LifetimeId>,
    },
    FnPointer {
        params: Vec<IRType>,
        ret: Box<IRType>,
        abi: Option<String>,
        unsafe_: bool,
    },
    Closure {
        captures: Vec<CaptureType>,
        fn_signature: Box<IRType>,
    },
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct StructField {
    pub name: String,
    pub ty: IRType,
    pub offset: Option<u64>,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct TraitBound {
    pub trait_name: String,
    pub args: Vec<IRType>,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum CaptureType { ByValue, ByRef, ByMutRef }

#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct LifetimeId(pub u32);
```

## 7.3 IR 指令（bt_core 内建 + Dialect 扩展）

```rust
// crates/bt-ir/src/instruction.rs

/// bt_core 内建指令 (Opcode 0-63) — 编译时固定
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum IRInstruction {
    // ─── 算术 ───
    Add { lhs: IRValueId, rhs: IRValueId },
    Sub { lhs: IRValueId, rhs: IRValueId },
    Mul { lhs: IRValueId, rhs: IRValueId },
    Div { lhs: IRValueId, rhs: IRValueId },
    Rem { lhs: IRValueId, rhs: IRValueId },

    // ─── 比较 ───
    ICmp { pred: ICMPredicate, lhs: IRValueId, rhs: IRValueId },
    FCmp { pred: FCMPredicate, lhs: IRValueId, rhs: IRValueId },

    // ─── 内存 ───
    Alloca { ty: IRTypeId },
    Load { ptr: IRValueId },
    Store { value: IRValueId, ptr: IRValueId },
    GEP { ptr: IRValueId, indices: Vec<IRValueId> },

    // ─── 转换 ───
    BitCast { value: IRValueId, target_ty: IRTypeId },
    ZExt { value: IRValueId, target_ty: IRTypeId },
    SExt { value: IRValueId, target_ty: IRTypeId },
    Trunc { value: IRValueId, target_ty: IRTypeId },
    FPToSI { value: IRValueId, target_ty: IRTypeId },
    SIToFP { value: IRValueId, target_ty: IRTypeId },

    // ─── 控制流 ───
    Branch { target: IRBlockId },
    CondBr { cond: IRValueId, then_bb: IRBlockId, else_bb: IRBlockId },
    Switch { value: IRValueId, cases: Vec<(IRValueId, IRBlockId)>, default: IRBlockId },
    Ret { value: Option<IRValueId> },
    Unreachable,

    // ─── 调用 ───
    Call { func: IRValueId, args: Vec<IRValueId> },
    Invoke { func: IRValueId, args: Vec<IRValueId>, normal_bb: IRBlockId, unwind_bb: IRBlockId },

    // ─── 聚合 ───
    ExtractValue { agg: IRValueId, indices: Vec<u32> },
    InsertValue { agg: IRValueId, value: IRValueId, indices: Vec<u32> },

    // ─── 其他 ───
    PHI { incoming: Vec<(IRValueId, IRBlockId)> },
    Select { cond: IRValueId, then_val: IRValueId, else_val: IRValueId },

    // ─── Dialect 扩展（运行时注册） ───
    Dialect(DialectInstruction),
}

/// Dialect 扩展指令 — 运行时通过 DialectRegistry 注册
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DialectInstruction {
    pub dialect: String,
    pub opcode: u16,
    pub operands: Vec<IRValueId>,
    pub attributes: HashMap<String, IRAttribute>,
    pub span: Option<SourceSpan>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum IRAttribute {
    String(String), Integer(i64), Float(f64), Bool(bool),
    Type(IRType), List(Vec<IRAttribute>),
}
```

## 7.4 Dialect 系统（运行时注册）

```rust
// crates/bt-dialect-core/src/lib.rs

/// Dialect trait — 运行时可注册（MLIR 风格）
/// 继承 Named 以满足 Registry<dyn Dialect> 的 T: ?Sized + Named bound
pub trait Dialect: Named {
    fn name(&self) -> &str;
    fn opcode_range(&self) -> (u16, u16);
    fn operations(&self) -> Vec<OperationDef>;

    /// Dialect 指令降级到 bt_core
    fn lower_to_core(
        &self, inst: &DialectInstruction, builder: &mut IRBuilder,
    ) -> Result<Vec<IRInstruction>, DialectError>;

    /// 验证单条 Dialect 指令
    fn verify(&self, inst: &DialectInstruction, module: &IRModule) -> Result<(), VerifyError>;

    /// Dialect 级别模块验证（跨指令约束）
    fn verify_module(&self, module: &IRModule) -> Result<(), VerifyError> { Ok(()) }
}

#[derive(Debug, Clone, Serialize)]
pub struct OperationDef {
    pub opcode: u16,
    pub name: String,
    pub operands: Vec<OperandDef>,
    pub attributes: Vec<AttributeDef>,
    pub description: String,
}

/// Dialect 注册表（与 03-Communication-Bus 中的 Registry<dyn Dialect> 一致）
pub type DialectRegistry = Registry<dyn Dialect>;

/// 通用注册表定义（参见 03-Communication-Bus 3.8 节）
/// Registry<dyn Dialect> 内部存储为 Arc<dyn Dialect>，无双重间接
///
/// 额外的 Dialect 专用方法通过扩展 trait 实现：
pub trait DialectRegistryExt {
    fn lower_all(&self, module: &mut IRModule) -> Result<DialectLowerResult, DialectError>;
}
```

### bt_rust Dialect 操作码 (224-239)

| Opcode | 名称 | 降级目标 |
|--------|------|---------|
| 224 | `ownership_transfer` | memcpy / refcount ops |
| 225 | `borrow_check` | 编译期检查，无运行时指令 |
| 226 | `drop_glue` | 条件 Call(drop_fn) |
| 227 | `enum_tag_check` | ICmp + Branch |
| 228 | `trait_dispatch` | GEP + Load + Call (vtable) |
| 229 | `slice_from_raw_parts` | Struct 构造 |
| 230 | `async_await` | Coroutine 状态机转换 |
| 231 | `closure_capture` | Struct field access |
| 232 | `pattern_destructure` | ExtractValue + Branch chain |
| 233 | `generics_monomorphize` | 函数克隆 + 类型替换 |

### bt_ts Dialect 操作码 (240-254)

| Opcode | 名称 | 降级目标 |
|--------|------|---------|
| 240 | `type_narrow` | ICmp + Branch |
| 241 | `union_tag_check` | ICmp + Branch |
| 242 | `nullish_coalesce` | ICmp + Select |
| 243 | `optional_chain` | null check + GEP |
| 244 | `spread_assign` | memcpy 序列 |
| 245 | `destructure_assign` | ExtractValue 序列 |
| 246 | `prototype_dispatch` | vtable lookup + Call |
| 247 | `ts_await` | coroutine 状态机 |
| 248 | `type_guard` | 运行时类型检查 |
| 249 | `as_assertion` | BitCast + 运行时检查（可选） |

## 7.5 IR 模块

```rust
// crates/bt-ir/src/module.rs

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IRModule {
    pub id: Uuid,
    pub name: String,
    pub source_language: String,
    pub target_triple: String,
    pub functions: Vec<IRFunction>,
    pub global_variables: Vec<IRGlobalVariable>,
    pub imported_functions: Vec<IRFunctionDecl>,
    pub types: Vec<IRTypeDef>,
    pub active_dialects: Vec<String>,
    pub features: IRFeature,
    pub debug_info: Option<IRDebugInfo>,
    pub source_hash: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IRFunction {
    pub id: IRFunctionId,
    pub name: String,
    pub mangled_name: Option<String>,
    pub linkage: Linkage,
    pub visibility: Visibility,
    pub signature: IRType,
    pub basic_blocks: Vec<IRBasicBlock>,
    pub attributes: Vec<FunctionAttribute>,
    pub span: Option<SourceSpan>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IRBasicBlock {
    pub id: IRBlockId,
    pub label: String,
    pub instructions: Vec<(IRValueId, IRInstruction)>,
    pub terminator: IRTerminator,
}
```

## 7.6 Frontend trait

```rust
// crates/bt-frontend-common/src/frontend.rs

#[async_trait]
pub trait Frontend: Named {
    fn name(&self) -> &str;
    fn language(&self) -> &str;
    fn extensions(&self) -> &[&str];

    async fn compile(
        &self,
        source: &SourceInput,
        options: &FrontendOptions,
    ) -> Result<FrontendOutput, FrontendError>;

    fn can_handle(&self, filename: &str) -> bool {
        self.extensions().iter().any(|ext| filename.ends_with(ext))
    }
}

#[derive(Debug)]
pub struct SourceInput {
    pub content: String,
    pub file_path: Option<String>,
    pub language: String,
}

#[derive(Debug)]
pub struct FrontendOutput {
    pub ir_module: IRModule,
    pub diagnostics: Vec<Diagnostic>,
    pub tokens: Option<serde_json::Value>,
    pub hir: Option<serde_json::Value>,
}
```

## 7.7 Backend trait

```rust
// crates/bt-backend-common/src/backend.rs

#[async_trait]
pub trait Backend: Named {
    fn name(&self) -> &str;
    fn targets(&self) -> Vec<TargetTriple>;
    fn can_handle(&self, target: &TargetTriple) -> bool;

    async fn emit_object(
        &self, module: &IRModule, target: &TargetTriple, options: &BackendOptions,
    ) -> Result<BackendOutput, BackendError>;

    async fn emit_assembly(
        &self, module: &IRModule, target: &TargetTriple, options: &BackendOptions,
    ) -> Result<String, BackendError>;

    async fn optimize(
        &self, module: &mut IRModule, level: OptLevel,
    ) -> Result<(), BackendError>;
}

#[derive(Debug)]
pub struct BackendOutput {
    pub object_data: Vec<u8>,
    pub debug_info: Option<Vec<u8>>,
    pub symbol_table: Vec<SymbolEntry>,
}
```

## 7.8 Pass trait（含依赖声明）

```rust
// crates/bt-passes/src/pass.rs

#[async_trait]
pub trait Pass: Named {
    fn name(&self) -> &str;
    fn category(&self) -> PassCategory;

    /// 此 Pass 依赖哪些 Pass 先执行
    fn dependencies(&self) -> &[&str] { &[] }

    /// 此 Pass 执行后使哪些分析结果失效
    fn invalidates(&self) -> &[&str] { &[] }

    async fn run(&self, module: &mut IRModule, ctx: &PassContext) -> PassResult;
}

#[derive(Debug, Clone)]
pub enum PassCategory {
    Analysis,
    Transformation,
    Verification,
    DialectLowering,
    AIAnalysis,
}

#[derive(Debug)]
pub struct PassContext {
    pub tracer: Option<Span>,
    pub metrics: PassMetrics,
    pub diagnostics: Vec<Diagnostic>,
    pub cancelled: Arc<AtomicBool>,
}

#[derive(Debug)]
pub struct PassResult {
    pub modified: bool,
    pub metrics: PassMetrics,
    pub diagnostics: Vec<Diagnostic>,
}
```
