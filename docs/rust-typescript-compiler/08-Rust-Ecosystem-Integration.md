# 08 — Rust 生态集成方案：BlockType Rust

> **定位**：BlockType Rust 是 **Rust 编译器的下一代架构演进**。
> 复用 rustc 前端（parse → typeck → borrowck → MIR），对接 BlockType 的创新中间层
> （BTIR + Dialect + AI + tower::Service + 全链路可观测性）。
>
> **核心决策**：不另建 Rust 前端。rustc 的前端已经是最好的 Rust 前端，无需重复。

---

## 8.1 架构定位

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    BlockType Rust 架构定位                               │
│                                                                         │
│   rustc 现有架构：                                                       │
│   源码 → [parse → HIR → typeck → borrowck → MIR → LLVM] → 机器码       │
│                                     ↑ 全部耦合在一起                     │
│                                                                         │
│   BlockType Rust 架构：                                                  │
│   源码 → [rustc 前端] → MIR → [BTIR 桥接] → [创新层] → [后端] → 机器码   │
│          ↑ 复用 rustc     ↑ 适配层    ↑ BlockType 价值    ↑ 可插拔      │
│                                                                         │
│   BlockType 不替代 rustc 前端，而是在 rustc 前端之后注入全新的：           │
│   - 统一服务抽象 (tower::Service)                                       │
│   - AI 原生分析 (AIOrchestrator)                                        │
│   - 全链路可观测性 (OpenTelemetry + Event Sourcing)                     │
│   - Dialect 可插拔系统                                                  │
│   - RESTful API + LSP 一等公民                                          │
│   - WASM 插件沙箱                                                       │
└─────────────────────────────────────────────────────────────────────────┘
```

### BlockType 相比 rustc 的架构优势

| 维度 | rustc | BlockType Rust |
|------|-------|---------------|
| 通信 | 内部函数调用，紧耦合 | tower::Service，松耦合，统一中间件链 |
| 可观测性 | `-Z dump` + `tracing` 调试 | OpenTelemetry 全链路 + Event Sourcing + 实时仪表盘 |
| AI 集成 | 无 | 管线内一等公民，多 Provider + 流式 + 预算 |
| 扩展 | 修改源码或 `-Z` flag | REST API + 运行时 Dialect + WASM 插件热加载 |
| IDE | rust-analyzer（独立项目） | 内置 LSP + API 驱动 |
| 多语言 | 仅 Rust | Rust（原生）+ TypeScript（Deno 桥接）+ WASM 插件 |
| API | CLI only | RESTful API + CLI + LSP + WebSocket |

---

## 8.2 核心架构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     BlockType Rust — 架构全貌                            │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │           Layer 1: Cargo 集成                                     │  │
│  │  ┌─────────────┐                                                  │  │
│  │  │ bt-cargo    │  Cargo.toml 解析 + 依赖图 + feature gate         │  │
│  │  └──────┬──────┘                                                  │  │
│  └─────────┼─────────────────────────────────────────────────────────┘  │
│            │                                                           │
│  ┌─────────▼─────────────────────────────────────────────────────────┐  │
│  │           Layer 2: rustc 前端（复用，不修改）                       │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐         │  │
│  │  │ rustc_   │  │ rustc_   │  │ rustc_   │  │ rustc_   │         │  │
│  │  │ parse    │→│ typeck   │→│ borrowck │→│ mir      │         │  │
│  │  │ (语法)    │  │ (类型检查)│  │ (借用检查)│  │ (中间表示)│         │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └─────┬────┘         │  │
│  └──────────────────────────────────────────────────┼───────────────┘  │
│                                                      │                  │
│  ┌──────────────────────────────────────────────────▼───────────────┐  │
│  │           Layer 3: MIR → BTIR 桥接（bt-rustc-bridge）             │  │
│  │  ┌────────────────────────────────────────────────────────────┐  │  │
│  │  │  MirToBtirConverter                                        │  │  │
│  │  │  rustc_mir::Body → bt_ir::IRFunction                       │  │  │
│  │  │  rustc_ty::Ty    → bt_ir::IRType                           │  │  │
│  │  │  rustc_mir::Terminator → bt_ir::IRTerminator               │  │  │
│  │  └────────────────────────────────────────────────────────────┘  │  │
│  └──────────────────────────────┬───────────────────────────────────┘  │
│                                 │                                       │
│  ┌──────────────────────────────▼───────────────────────────────────┐  │
│  │           Layer 4: BlockType 核心（自有创新层）                     │  │
│  │                                                                  │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐   │  │
│  │  │  BTIR    │ │ Dialect  │ │ PassMgr  │ │ AIOrchestrator   │   │  │
│  │  │  (IR)    │ │ Registry │ │ (优化)    │ │ (AI 分析/建议)    │   │  │
│  │  └────┬─────┘ └──────────┘ └────┬─────┘ └──────────────────┘   │  │
│  │       │                          │                              │  │
│  │  ┌────▼──────────────────────────▼───────────────────────────┐  │  │
│  │  │     CompileService (tower::Service)                        │  │  │
│  │  │     限流 / 超时 / 重试 / 日志 / 事件记录                     │  │  │
│  │  └────┬───────────────────────────────────────────────────────┘  │  │
│  └───────┼──────────────────────────────────────────────────────────┘  │
│          │                                                             │
│  ┌───────▼──────────────────────────────────────────────────────────┐  │
│  │           Layer 5: 后端（可插拔）                                  │  │
│  │  ┌──────────────┐  ┌──────────────┐                              │  │
│  │  │ bt-backend-  │  │ bt-backend-  │                              │  │
│  │  │ llvm         │  │ cranelift    │                              │  │
│  │  │ (inkwell)    │  │ (WASM 目标)  │                              │  │
│  │  └──────────────┘  └──────────────┘                              │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │           Layer 6: API + 可观测性 + AI                             │  │
│  │  axum (REST) + LSP + WebSocket + OpenTelemetry + EventStore      │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 8.3 新增 Crate 设计

### 8.3.1 `bt-cargo` — Cargo 集成

```rust
/// bt-cargo: Cargo 工作空间解析与依赖管理
///
/// 职责：
/// 1. 解析 Cargo.toml → 依赖图
/// 2. 调用 cargo metadata 获取完整 crate 信息
/// 3. Feature gate 解析与组合
/// 4. 构建计划生成（编译顺序 + feature 组合）
/// 5. 增量构建脏标记
pub struct BtCargo {
    workspace: CargoWorkspace,
    resolver: DependencyResolver,
    build_planner: BuildPlanner,
}

pub struct CargoWorkspace {
    root: PathBuf,
    members: Vec<CargoCrate>,
    target_directory: PathBuf,
}

pub struct CargoCrate {
    name: String,
    version: Version,
    manifest_path: PathBuf,
    source_dir: PathBuf,
    edition: RustEdition,             // 2015/2018/2021/2024
    features: HashMap<String, Vec<String>>,
    default_features: Vec<String>,
    dependencies: Vec<Dependency>,
    is_proc_macro: bool,
    crate_type: CrateType,            // bin/lib/proc-macro/staticlib/cdylib
}

impl BtCargo {
    /// 从项目根目录加载工作空间
    pub fn load(root: &Path) -> Result<Self, CargoError>;

    /// 解析完整依赖图（含传递依赖）
    pub fn resolve_dependencies(&self) -> Result<DependencyGraph, CargoError>;

    /// 生成编译计划（拓扑排序 + feature 组合）
    pub fn build_plan(&self, target: &TargetTriple) -> Result<BuildPlan, CargoError>;

    /// 获取脏 crate 列表（基于 Salsa 增量追踪）
    pub fn dirty_crates(&self) -> Vec<CargoCrate>;

    /// 下载/缓存缺失的依赖
    pub async fn fetch_dependencies(&self) -> Result<(), CargoError>;
}
```

**实现策略**：内部调用 `cargo metadata --format-version=1` 获取 JSON，与 Cargo 100% 兼容。

### 8.3.2 `bt-rustc-bridge` — rustc 前端对接（核心适配层）

```rust
//! bt-rustc-bridge: BlockType 与 rustc 的唯一接口
//!
//! 依赖方式：sysroot + extern crate（详见 §8.4）
//! - rust-toolchain.toml 锁定 nightly 版本
//! - #![feature(rustc_private)] 启用 sysroot 访问
//! - extern crate rustc_* 从 sysroot 引入
//!
//! 这是整个架构最关键的适配层。
//! 职责：
//! 1. 通过 rustc_driver 调用完整 rustc 编译管线
//! 2. 在 codegen 阶段前拦截 MIR
//! 3. 将 rustc MIR 转换为 BlockType BTIR
//! 4. 透传 rustc 诊断（保持与 rustc 一致的错误消息）
//! 5. 执行 proc-macro 展开
//!
//! 技术路径：类似 rustc_codegen_cranelift 的做法——
//! 实现 rustc 的 ExtraBackendPointers，在 codegen 阶段接管

#![feature(rustc_private)]

extern crate rustc_driver;
extern crate rustc_interface;
extern crate rustc_middle;
extern crate rustc_mir;
extern crate rustc_hir;
extern crate rustc_session;
extern crate rustc_span;
extern crate rustc_errors;

pub struct RustcBridge {
    config: RustcBridgeConfig,
}

pub struct RustcBridgeConfig {
    /// rustc 版本（与链接的 rustc_driver 匹配）
    rustc_version: String,
    /// 是否保留 rustc 诊断格式
    native_diagnostics: bool,
}

impl RustcBridge {
    /// 编译单个 crate：rustc 前端 → MIR → BTIR
    ///
    /// 内部流程：
    /// 1. 调用 rustc_interface::run_compiler()
    /// 2. 注册 after_analysis callback
    /// 3. 在 callback 中遍历所有 MIR bodies
    /// 4. 将每个 MIR body 转换为 BTIR IRFunction
    /// 5. 组装为 IRModule → 交给 CompileService
    pub fn compile_crate(
        &self,
        crate_info: &CargoCrate,
        dependencies: &[Arc<IRModule>],
        target: &TargetTriple,
    ) -> Result<BridgeOutput, BridgeError>;

    /// 执行 proc-macro 展开
    pub fn expand_proc_macros(
        &self,
        source: &str,
        macros: &[ProcMacroDefinition],
    ) -> Result<ExpandedSource, BridgeError>;
}

/// rustc 编译产出（包含 BTIR + rustc 诊断 + 类型信息）
pub struct BridgeOutput {
    /// MIR → BTIR 转换后的 IR 模块
    pub ir_module: IRModule,
    /// rustc 产生的诊断信息（原样透传）
    pub diagnostics: Vec<RustcDiagnostic>,
    /// rustc 的类型信息摘要（用于 AI 分析上下文）
    pub type_info_summary: TypeInfoSummary,
    /// proc-mcrate 的依赖关系
    pub crate_metadata: CrateMetadata,
}

/// MIR → BTIR 转换器
pub struct MirToBtirConverter;

impl MirToBtirConverter {
    /// rustc_mir::Body → bt_ir::IRFunction
    fn convert_body(&self, mir: &mir::Body, tcx: TyCtxt<'_>) -> IRFunction;

    /// rustc_middle::ty::Ty → bt_ir::IRType
    fn convert_ty(&self, ty: Ty<'_>, tcx: TyCtxt<'_>) -> IRType {
        match ty.kind() {
            ty::Bool       => IRType::Integer { bits: 1 },
            ty::Int(i)     => IRType::Integer { bits: i.bit_width() },
            ty::Uint(i)    => IRType::Integer { bits: i.bit_width() },
            ty::Float(f)   => IRType::Float { bits: f.bit_width() },
            ty::Char       => IRType::Integer { bits: 32 },
            ty::Str        => IRType::Struct { name: "str".into(), fields: slice_fields(), packed: false },
            ty::Ref(_, t, _)  => IRType::Pointer { pointee: Box::new(self.convert_ty(t, tcx)) },
            ty::RawPtr(ty_m)  => IRType::Pointer { pointee: Box::new(self.convert_ty(ty_m.ty, tcx)) },
            ty::Adt(def, _)   => self.convert_adt(def, tcx),
            ty::Tuple(types)   => IRType::Tuple { elements: types.iter().map(|t| self.convert_ty(t, tcx)).collect() },
            ty::FnDef(_, _)    => IRType::FnPointer { params: vec![], ret: Box::new(IRType::Void), abi: None, unsafe_: false },
            ty::FnPtr(sig)     => self.convert_fn_sig(sig),
            ty::Closure(..)    => self.convert_closure(ty, tcx),
            ty::Coroutine(..)  => self.convert_coroutine(ty, tcx),
            ty::Dynamic(..)    => self.convert_trait_object(ty, tcx),
            _ => IRType::Opaque { name: format!("{ty:?}") },
        }
    }
}
```

### 8.3.3 `bt-proc-macro` — 过程宏支持

```rust
/// bt-proc-macro: 过程宏桥接
///
/// 过程宏在编译期执行任意 Rust 代码，必须通过 rustc 的 proc_macro 框架执行。
/// 策略：将 proc-macro crate 编译为 .so/.dll，在子进程中加载并执行。
/// 这正是 rustc 自己的做法，保证 100% 兼容。
pub struct ProcMacroHost {
    loaded_macros: HashMap<String, ProcMacroInstance>,
}

pub struct ProcMacroInstance {
    name: String,
    kind: ProcMacroKind,
    /// 加载的 .so 的句柄
    handle: LibHandle,
}

pub enum ProcMacroKind {
    Derive,          // #[derive(...)]
    Attribute,       // #[proc_macro_attribute]
    FunctionLike,    // #[proc_macro]
}

impl ProcMacroHost {
    /// 编译并加载 proc-macro crate
    pub fn load(&mut self, crate_info: &CargoCrate) -> Result<(), ProcMacroError>;

    /// 执行 proc-macro 展开
    pub fn expand(&self, name: &str, input: TokenStream) -> Result<TokenStream, ProcMacroError>;

    /// 卸载（dlclose）
    pub fn unload(&mut self, name: &str);
}
```

### 8.3.4 `bt-std-bridge` — 标准库兼容

```rust
/// bt-std-bridge: 直接链接 rustc 编译好的标准库
///
/// 不重写 std，直接复用 rustup 安装的 std 静态库。
/// 兼容性 100%，零额外工作。
pub struct StdBridge {
    sysroot: PathBuf,
}

impl StdBridge {
    /// 从 rustup 获取当前 sysroot
    pub fn from_rustup() -> Result<Self, StdError>;

    /// 获取标准库路径（用于链接）
    pub fn std_lib_paths(&self, target: &TargetTriple) -> Vec<PathBuf>;

    /// 检查 sysroot 是否可用
    pub fn check_sysroot(&self) -> Result<(), StdError>;
}
```

---

## 8.4 rustc 依赖方案：Sysroot + extern crate

> **核心决策**：使用 rustc nightly sysroot + `extern crate` 方式依赖 rustc 内部 crate。
> 这与 clippy、miri、rustc_codegen_cranelift 等官方项目使用完全相同的方式。

### 8.4.1 为什么选择 Sysroot 方案

| 维度 | 方案 A: Sysroot + extern crate ✅ | 方案 B: Cargo.toml 直接依赖 ❌ |
|------|-----------------------------------|-------------------------------|
| 版本一致性 | ✅ 与本地安装的 nightly 完全一致 | ❌ crates.io 版本可能与本地 rustc 不匹配 |
| 社区先例 | ✅ clippy / miri / cg_clif 全部使用此方案 | ❌ 无官方项目使用此方案 |
| 维护成本 | ✅ rust-toolchain.toml 锁定 nightly 即可 | ❌ 需要自己跟踪 rustc crate 版本发布 |
| ABI 安全 | ✅ sysroot crate 与 rustc 二进制 100% 匹配 | ❌ 版本不匹配可能导致静默 UB |
| 构建系统支持 | ✅ Cargo 原生支持 `rustc-private` | ❌ 需要自定义 build.rs |
| 缺点 | ❌ 必须使用 nightly（无 stable API） | ✅ 理论上不限制 channel |

**结论**：方案 A 是唯一可靠的路径。rustc 内部 crate 不发布到 crates.io，没有版本保证，必须通过 sysroot 获取。

### 8.4.2 配置文件

**`rust-toolchain.toml`**（项目根目录，锁定 nightly 版本）：

```toml
# 锁定到特定 nightly 版本，避免 rustc 内部 API 变更导致编译失败
# 更新策略：跟随 Rust 6 周发布周期，每个 stable 发布后升级一次
[toolchain]
channel = "nightly-2026-05-01"
components = ["rustc-dev", "llvm-tools"]
```

**`bt-rustc-bridge/Cargo.toml`**（注意：不依赖任何 rustc_* crate）：

```toml
[package]
name = "bt-rustc-bridge"
version = "0.1.0"
edition = "2024"

[dependencies]
bt-ir = { path = "../bt-ir" }
bt-core = { path = "../bt-core" }
bt-dialect-rust = { path = "../bt-dialect-rust" }

# rustc crate 通过 sysroot 引入，不在 Cargo.toml 中声明
# 见 src/lib.rs 中的 extern crate 声明
```

### 8.4.3 extern crate 声明

**`bt-rustc-bridge/src/lib.rs`**：

```rust
//! bt-rustc-bridge: 通过 sysroot 链接 rustc 内部 crate
//!
//! 依赖方式：
//! - `#![feature(rustc_private)]` 启用 sysroot crate 访问
//! - `extern crate rustc_*` 从 sysroot 引入 rustc 内部 crate
//! - 与 clippy/miri/cg_clif 使用完全相同的机制

#![feature(rustc_private)]

// ─── rustc 内部 crate（通过 sysroot 引入） ───
extern crate rustc_driver;      // 编译器驱动入口
extern crate rustc_interface;    // 编译器接口（run_compiler 等）
extern crate rustc_middle;       // TyCtxt, Ty, MirBody 等核心类型
extern crate rustc_mir;          // MIR 数据结构
extern crate rustc_hir;          // HIR（用于获取上下文信息）
extern crate rustc_session;      // Session, 编译选项
extern crate rustc_span;         // Span, 源码位置
extern crate rustc_target;       // 目标平台信息
extern crate rustc_errors;       // 诊断/错误处理
extern crate rustc_ast;          // AST（proc-macro 展开后）

// ─── BlockType 自有 crate ───
// （通过 Cargo.toml 正常依赖，无需 extern crate）

pub mod converter;   // MirToBtirConverter
pub mod bridge;      // RustcBridge 主入口
pub mod diagnostics; // rustc 诊断透传
pub mod types;       // 类型映射 (rustc_ty → bt_ir)
```

### 8.4.4 版本管理与 CI

**版本锁定策略**：

```
┌───────────────────────────────────────────────────────┐
│              rustc 版本管理流程                         │
│                                                       │
│  rust-toolchain.toml                                  │
│  channel = "nightly-2026-05-01"  ← 锁定日期版本        │
│                                                       │
│  更新周期：                                             │
│  1. 每 6 周（Rust stable 发布周期）检查一次              │
│  2. CI 每日使用 latest nightly 测试兼容性               │
│  3. 发现 API 变更 → 更新桥接层适配 → 更新锁定版本       │
│  4. 提交 PR: 更新 rust-toolchain.toml + 桥接层适配     │
│                                                       │
│  CI 配置：                                             │
│  - 主 CI: 使用锁定的 nightly（保证稳定）                │
│  - Nightly CI: 使用 latest nightly（提前发现 API 变更） │
│  - 两者都通过才合并                                    │
└───────────────────────────────────────────────────────┘
```

**GitHub Actions 配置示例**：

```yaml
# .github/workflows/ci.yml
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@master
        with:
          toolchain: nightly-2026-05-01
          components: rustc-dev, llvm-tools
      - run: cargo test --workspace

  nightly-rolling:
    # 每日运行，检测 rustc API 变更
    runs-on: ubuntu-latest
    if: github.event_name == 'schedule'
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@master
        with:
          toolchain: nightly
          components: rustc-dev, llvm-tools
      - run: cargo test --workspace
        continue-on-error: true  # 允许失败，仅用于预警
```

### 8.4.5 构建命令

```bash
# 开发构建（使用 rust-toolchain.toml 锁定的 nightly）
cargo build

# 运行测试
cargo test

# 更新锁定版本
# 编辑 rust-toolchain.toml 中的 channel = "nightly-YYYY-MM-DD"
rustup install nightly-YYYY-MM-DD
rustup component add rustc-dev --toolchain nightly-YYYY-MM-DD
cargo test  # 验证兼容性
```

### 8.4.6 最小化 API 接触面

```
bt-rustc-bridge 使用的 rustc API 表面（最小化原则）：

必须使用的 API（稳定度较高）：
├── rustc_driver::RunCompiler                    # 编译器入口
├── rustc_interface::run_compiler()              # 运行编译
├── rustc_interface::Queries::global_ctxt()      # 获取 TyCtxt
├── rustc_middle::ty::TyCtxt                     # 类型上下文
├── rustc_middle::ty::TyKind                     # 类型枚举
├── rustc_middle::mir::Body                      # MIR 函数体
├── rustc_middle::mir::Terminator                # 终结指令
├── rustc_middle::mir::Statement                 # 普通语句
├── rustc_session::Session                       # 编译会话
└── rustc_span::Span                             # 源码位置

隔离策略：
├── 所有 rustc 类型 → bt-ir 类型 在 converter/ 中完成
├── rustc 错误 → BridgeError 在 diagnostics/ 中转换
├── 其他 crate 绝不直接接触 rustc_* API
└── bt-rustc-bridge 是唯一 import rustc_* 的 crate
```

---

## 8.5 项目结构（精简后）

```
blocktype-next/
├── Cargo.toml                         # workspace 根
├── rust-toolchain.toml                # 锁定 nightly 版本（rustc_driver 需要）
│
├── crates/
│   │
│   │  ─── Layer 1: 基础设施 ───
│   ├── bt-core/                       # 核心类型、诊断、SourceManager、错误码
│   ├── bt-service/                    # tower::Service 编排、CompileService
│   ├── bt-api/                        # RESTful API (axum) + WebSocket + LSP
│   ├── bt-telemetry/                  # 可观测性 (OpenTelemetry)
│   ├── bt-event-store/                # Event Sourcing
│   ├── bt-query/                      # Salsa 增量查询引擎
│   │
│   │  ─── Layer 2: IR 层 ───
│   ├── bt-ir/                         # BTIR 核心
│   ├── bt-ir-verifier/                # IR 验证
│   ├── bt-passes/                     # IR 优化 Pass + PassManager
│   │
│   │  ─── Layer 2: Dialect 层 ───
│   ├── bt-dialect-core/               # bt_core Dialect (内建)
│   ├── bt-dialect-rust/               # bt_rust Dialect (运行时注册)
│   ├── bt-dialect-ts/                 # bt_ts Dialect (运行时注册)
│   │
│   │  ─── Layer 3: Rust 前端对接 ───
│   ├── bt-cargo/                      # Cargo 工作空间 + 依赖管理
│   ├── bt-rustc-bridge/               # rustc MIR → BTIR 桥接（核心适配层）
│   ├── bt-clippy-integration/         # Clippy 整合 + AI 增强（bt clippy）★ 新增
│   ├── bt-proc-macro/                 # 过程宏加载/执行
│   ├── bt-std-bridge/                 # 标准库链接
│   │
│   │  ─── Layer 2: TypeScript 前端（保留） ───
│   ├── bt-frontend-common/            # Frontend trait + Registry
│   ├── bt-frontend-ts/                # TS 前端桥接 (Deno)
│   │
│   │  ─── Layer 2: 后端层 ───
│   ├── bt-backend-common/             # Backend trait + Registry
│   ├── bt-backend-llvm/               # LLVM 后端 (inkwell)
│   ├── bt-backend-cranelift/          # Cranelift 后端
│   │
│   │  ─── Layer 2: AI 服务 ───
│   ├── bt-ai/                         # AI 编排器
│   │
│   │  ─── Layer 2: 插件系统 ───
│   ├── bt-plugin-host/                # WASM 插件宿主
│   │
│   │  ─── CLI 入口 ───
│   └── bt-cli/                        # CLI (clap)
│
├── ts-frontend/                       # TypeScript 前端 (Deno)
├── plugin-sdk/                        # WASM 插件 SDK (WIT)
├── runtime/
│   └── ts-runtime/                    # TypeScript 运行时
├── dashboard/                         # Web 监控仪表盘
├── tests/
└── docs/
```

**关键变化**：删除 `bt-frontend-rust`，新增 `bt-cargo` / `bt-rustc-bridge` / `bt-proc-macro` / `bt-std-bridge`。

---

## 8.6 依赖关系

> **关键说明**：`bt-rustc-bridge` 是唯一使用 sysroot 依赖的 crate。
> rustc 内部 crate（`rustc_driver` 等）通过 `#![feature(rustc_private)]` + `extern crate` 引入，
> **不在任何 Cargo.toml 中声明**。详见 §8.4。

```
bt-cli
 ├── bt-cargo                         # Cargo 集成
 │    └── cargo_metadata + toml
 ├── bt-rustc-bridge                  # rustc 前端对接（bt build 模式）
 │    ├── [sysroot] rustc_driver      # ← sysroot + extern crate（非 Cargo.toml）
 │    ├── [sysroot] rustc_interface
 │    ├── [sysroot] rustc_middle
 │    ├── [sysroot] rustc_mir
 │    ├── [sysroot] rustc_hir
 │    ├── [sysroot] rustc_session
 │    ├── [sysroot] rustc_span
 │    ├── [sysroot] rustc_errors
 │    ├── bt-ir                       # 输出 BTIR（Cargo.toml 正常依赖）
 │    ├── bt-dialect-rust             # bt_rust Dialect
 │    └── bt-core
 ├── bt-clippy-integration            # Clippy 整合（bt clippy 模式）★ 新增
 │    ├── clippy_lints                # Clippy 700+ lint 规则集（crates.io）
 │    ├── [sysroot] rustc_driver      # sysroot + extern crate
 │    ├── [sysroot] rustc_lint        # Lint 框架
 │    ├── [sysroot] rustc_interface
 │    ├── [sysroot] rustc_middle
 │    ├── bt-ai                       # AI 增强层
 │    ├── bt-event-store              # 事件记录
 │    ├── bt-core
 │    └── bt-rustc-bridge (optional)  # --build 模式时编译
 ├── bt-proc-macro                    # 过程宏
 │    └── bt-core
 ├── bt-std-bridge                    # 标准库链接
 │    └── bt-core
 ├── bt-api
 │    ├── bt-service
 │    │    ├── bt-ir
 │    │    ├── bt-frontend-common
 │    │    ├── bt-backend-common
 │    │    ├── bt-dialect-core
 │    │    ├── bt-event-store
 │    │    ├── bt-query
 │    │    └── bt-core
 │    ├── bt-telemetry
 │    │    └── bt-core
 │    └── axum + tower + tokio
 ├── bt-frontend-ts                   # TypeScript 前端
 │    ├── bt-frontend-common
 │    ├── bt-ir
 │    └── bt-dialect-ts
 ├── bt-backend-llvm
 │    ├── bt-backend-common
 │    ├── bt-ir
 │    └── inkwell
 ├── bt-backend-cranelift
 │    ├── bt-backend-common
 │    ├── bt-ir
 │    └── cranelift
 ├── bt-passes
 ├── bt-ai
 ├── bt-plugin-host
 └── clap
```

---

## 8.7 CLI 设计

```bash
# ─── 编译 Rust 项目（默认） ───

bt build                            # 编译当前 Cargo 项目
bt build src/main.rs                # 编译单文件
bt build --release                  # Release 模式
bt build --target aarch64-unknown-linux-gnu  # 交叉编译

# ─── 后端选择 ───

bt build --backend=llvm             # 默认，使用 inkwell (LLVM)
bt build --backend=cranelift        # Cranelift 后端（更快编译，可输出 WASM）

# ─── AI 增强 ───

bt build --ai=suggest               # AI 建议模式（默认）
bt build --ai=auto                  # AI 自动应用高置信度优化

# ─── 检查 / Lint ───

bt check                            # 类型检查（不生成代码）
bt clippy                           # Clippy 700+ 规则 + BlockType AI 增强
bt clippy --fix                     # 自动修复（Clippy MachineApplicable + AI 建议）
bt clippy --build                   # Lint 后继续编译（bt build + bt clippy 一步到位）
bt clippy --ai=off                  # 仅 Clippy 原始 lint，不叠加 AI 增强

# ─── 可观测性 ───

bt build --watch                    # 实时监控编译过程
bt dashboard                        # 启动 Web 仪表盘
bt trace <task_id>                  # 查看编译 Trace

# ─── 其他 ───

bt explain E0502                    # 解释错误码
bt --version                        # 显示版本 + 绑定的 rustc 版本
```

### CLI 参数

```rust
#[derive(Parser)]
#[command(name = "bt", about = "BlockType Rust — 下一代 Rust 编译器")]
pub struct BtCli {
    /// 子命令
    #[command(subcommand)]
    command: BtCommand,
}

#[derive(Subcommand)]
pub enum BtCommand {
    /// 编译项目
    Build {
        /// 输入文件（缺省则编译当前 Cargo 项目）
        input: Option<PathBuf>,
        /// 后端: llvm / cranelift
        #[arg(long, default_value = "llvm")]
        backend: String,
        /// 目标平台
        #[arg(long)]
        target: Option<String>,
        /// AI 模式: off / suggest / auto
        #[arg(long, default_value = "suggest")]
        ai: AiMode,
        /// Release 模式
        #[arg(short, long)]
        release: bool,
        /// 增量编译
        #[arg(long)]
        incremental: bool,
        /// 输出目录
        #[arg(short, long)]
        out_dir: Option<PathBuf>,
    },
    /// 类型检查
    Check { input: Option<PathBuf> },
    /// Clippy 700+ 规则 + BlockType AI 增强（整合 clippy_lints）
    Clippy {
        input: Option<PathBuf>,
        /// AI 模式: off / enhance / full
        #[arg(long, default_value = "enhance")]
        ai: AiMode,
        /// 自动修复可修复的 lint
        #[arg(long)]
        fix: bool,
        /// Lint 后继续编译
        #[arg(long)]
        build: bool,
    },
    /// 解释错误码
    Explain { code: String },
    /// 启动 Web 仪表盘
    Dashboard,
    /// 查看编译 Trace
    Trace { task_id: String },
}
```

---

## 8.8 编译管线（Rust 项目完整流程）

```
用户执行: bt build

1. bt-cargo: 解析 Cargo.toml
   ├── 解析依赖图（cargo metadata）
   ├── Feature gate 组合
   └── 生成编译计划（拓扑排序）

2. bt-proc-macro: 预处理 proc-macro crate
   ├── 编译 proc-macro crate 为 .so
   └── 加载到 ProcMacroHost

3. bt-rustc-bridge: 逐 crate 编译
   ├── 调用 rustc_driver::run_compiler()
   ├── rustc 执行: parse → HIR → typeck → borrowck → MIR
   ├── 拦截 MIR（after_analysis callback）
   ├── MirToBtirConverter: MIR → BTIR
   ├── 透传 rustc 诊断
   └── 输出 IRModule

4. bt-service: BlockType 核心管线
   ├── Dialect 降级（bt_rust → bt_core）
   ├── IR 验证
   ├── IR 优化 Pass（DCE/内联/常量折叠）
   ├── [AI_S2: 优化建议]
   └── 选择后端

5. bt-backend-llvm/cranelift: 代码生成
   ├── emit_object() → .o 文件
   └── [AI_S3: 代码质量评估]

6. bt-std-bridge + 系统链接器: 链接
   ├── 链接 std .rlib（来自 sysroot）
   ├── 链接依赖 crate 的 .o
   └── 输出可执行文件

7. bt-event-store: 全程事件记录
   ├── 每个 crate 的编译事件
   ├── AI 分析事件
   └── 可通过 WebSocket 实时推送 / 回放
```

---

## 8.9 渐进式路线图

### Phase 0: 基础设施 + rustc 桥接（8 周）

| 周 | 任务 | MVP |
|---|------|-----|
| 1-2 | `bt-cargo`: Cargo.toml 解析 + 依赖图 | `bt cargo-info` 显示工作空间结构 |
| 3-4 | `bt-rustc-bridge`: rustc_driver 集成 + MIR 拦截 | `fn main() {}` 通过 rustc 前端产出 MIR |
| 5-6 | `bt-rustc-bridge`: MIR → BTIR 基础转换 | 基础类型 + 函数 + 控制流转换通过 |
| 7-8 | `bt-std-bridge` + 链接 | `println!("hello")` 编译运行成功 |

**里程碑 M0**：`bt build hello.rs` 可编译运行使用 `std` 的程序

### Phase 1: IR 核心 + Dialect（5 周，不变）

| 周 | 任务 | MVP |
|---|------|-----|
| 1-2 | `bt-ir` 完善 + 序列化 | IRModule 可 JSON 序列化 |
| 3-4 | `bt-dialect-core` + `bt-dialect-rust` | bt_rust Dialect 注册 + 降级 |
| 5 | `bt-ir-verifier` + IR 操作 API | IR 验证通过 |

**里程碑 M1**：Dialect 可运行时注册/降级/验证

### Phase 2: Cargo 项目编译（6 周）

| 周 | 任务 | MVP |
|---|------|-----|
| 1-2 | `bt-cargo`: 完整依赖解析 + 拓扑排序 | 3 crate 工作空间编译 |
| 3-4 | `bt-cargo`: Feature gate + optional 依赖 | 有 feature 的 crate 正确编译 |
| 5-6 | 多 crate 增量编译 + 脏标记 | 改一个文件只重编译受影响 crate |

**里程碑 M2**：`bt build`（无参数）编译完整 Cargo 项目

### Phase 3: Proc-Macro + 生态兼容（6 周）

| 周 | 任务 | MVP |
|---|------|-----|
| 1-2 | `bt-proc-macro`: .so 加载 + 执行 | 自定义 derive macro 编译运行 |
| 3-4 | 常用 proc-macro 透传（serde 等） | `#[derive(Serialize)]` 编译通过 |
| 5-6 | 沙箱化 + top-100 crate 测试 | serde/tokio/axum 项目编译通过 |

**里程碑 M3**：主流 Rust 项目（使用 serde/tokio/axum/clap）编译通过

### Phase 4: AI 增强 + 可观测性 + LSP（8 周）

| 周 | 任务 | MVP |
|---|------|-----|
| 1-3 | AI Pass 集成到 Rust 编译管线 | `bt build --ai=auto` 给出优化建议 |
| 4-5 | OpenTelemetry 全链路追踪 | 每个编译阶段有 Span |
| 6-7 | LSP 适配器 | VS Code 中基本代码补全/诊断 |
| 8 | Dashboard + WebSocket 实时推送 | Web 仪表盘显示编译过程 |

**里程碑 M4**：AI 辅助优化 + IDE 集成 + 实时监控全部可用

### Phase 5: 高级特性（12 周）

| 周 | 任务 | MVP |
|---|------|-----|
| 1-2 | bt-backend-cranelift 完善 | Cranelift 后端可用于编译 Rust |
| 3-4 | WASM 目标输出 | Rust → WASM 编译 |
| 5-6 | bt-plugin-host: WASM 插件 | 自定义 Pass 插件加载执行 |
| 7-8 | Salsa 增量编译完善 | 文件修改后秒级重编译 |
| 9-10 | TypeScript 前端（Deno 桥接） | TS 基本程序编译 |
| 11-12 | `bt clippy` + AI lint | AI 增强的代码检查 |

**里程碑 M5**：BlockType Rust 全功能可用

### 总计

**Phase 0-4 = 33 周（~8 个月）** → 编译主流 Rust 项目 + AI 增强 + IDE 集成
**Phase 5 = 12 周** → Cranelift + 插件 + TS 前端
**全流程 = 45 周（~11 个月）**

---

## 8.10 风险评估

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| rustc_driver 是 nightly-only API | 高 | 高 | `rust-toolchain.toml` 锁定日期版本；CI 每日用 latest nightly 预警；最小化 API 接触面（见 §8.4.6） |
| sysroot nightly 版本锁定导致安全补丁延迟 | 中 | 中 | 每 6 周（Rust 发布周期）主动升级锁定版本；紧急安全修复可在 24h 内手动升级 |
| MIR → BTIR 转换信息丢失 | 中 | 中 | 复杂类型保留 bt_rust Dialect 标记，不强制完全转平 |
| proc-macro .so 跨平台问题 | 中 | 中 | 使用 rustc 自己的 proc_macro 加载器 |
| crates.io crate 编译兼容性 | 中 | 中 | Phase 3 后从 top-100 crate 开始逐步覆盖 |
| BlockType 优化 Pass 与 rustc 冲突 | 低 | 低 | 可选择性跳过 BlockType 优化，直接用 rustc 优化 |
| rustc 内部 API 变更导致桥接层崩溃 | 高 | 中 | 桥接层最小化 API 接触面（仅 9 个核心类型）；日期锁定 + CI nightly rolling 测试 |

---

## 8.11 与原路线图的关系

```
原路线图 (v2.1)                     BlockType Rust 路线图
────────────────                    ────────────────────
Phase 0 (基础设施, 4周)        →   Phase 0 (基础设施+rustc桥接, 8周)
Phase 1 (IR+Dialect, 5周)      →   Phase 1 (不变, 5周)
Phase 2 (自有Rust前端, 10周)   →   ❌ 删除，由 bt-rustc-bridge 替代
Phase 3 (TS前端, 8周)          →   Phase 5 后半段 (延后)
Phase 4 (后端+AI, 7周)         →   Phase 4 (AI+可观测, 8周)
Phase 5 (高级特性, 14周)       →   Phase 3+5 (Proc-Macro+高级, 18周)

总工期: 48周 → 45周（更短，且能编译真实 Rust 项目）
```

**核心变化**：删除自有 Rust 前端（省 10 周），新增 rustc 桥接（加 4 周），净省 6 周，且兼容性从 ~5% 跃升到 ~95%。

---

## 8.12 Clippy 整合方案

> **核心决策**：BlockType **整合 Clippy 而非替代**。`bt clippy` 底层直接复用 Clippy 的 700+ lint passes，
> 并在此基础上叠加 BlockType AI 增强（AI 解释、跨文件上下文、自动修复建议）。
>
> **理由**：Clippy 有 8 年社区积累和 700+ 规则，BlockType 不可能也不应该从零重建。
> BlockType 的差异化价值在编译器平台，不在 Lint 规则数量。

### 8.12.1 整合架构

```
bt clippy
  │
  │  ┌───────────────────────────────────────────────────────────┐
  │  │              bt-clippy-integration (桥接层)                 │
  │  │                                                           │
  │  │  1. 注册 Clippy lint passes                               │
  │  │     └── clippy_lints::register_plugins()                  │
  │  │     └── clippy_lints::register_pre_expansion_lints()      │
  │  │     └── clippy_lints::register_renamed()                  │
  │  │                                                           │
  │  │  2. 注册 BlockType AI 增强层                               │
  │  │     └── BtClippyEnhancer (LateLintPass)                   │
  │  │         ├── 为 Clippy warning 叠加 AI 解释                 │
  │  │         ├── 补充 Clippy 未覆盖的场景                        │
  │  │         └── 提供跨文件上下文建议                             │
  │  │                                                           │
  │  │  3. 替换 Diagnostic Emitter                               │
  │  │     └── BtClippyEmitter                                   │
  │  │         ├── 收集 Clippy lint → 结构化数据                   │
  │  │         ├── 叠加 AI 增强解释                               │
  │  │         ├── 记录到 EventStore                              │
  │  │         └── 推送到 Dashboard / LSP                         │
  │  └───────────────────────────────────────────────────────────┘
  │
  ├── rustc_driver::run_compiler(BtClippyCallbacks)
  │     ├── Callbacks::config() → 注入 BtClippyEmitter
  │     ├── Callbacks::after_expansion() → 注册 Clippy lint + AI 增强层
  │     └── Callbacks::after_analysis() → 可选 MIR→BTIR 转换
  │
  └── 输出
       ├── 终端：Clippy 原始格式 + AI 增强解释
       ├── JSON：结构化 lint 结果（供 LSP / API 使用）
       ├── EventStore：全量事件记录（可回放）
       └── Dashboard：实时 lint 可视化
```

### 8.12.2 BtClippyCallbacks 实现

```rust
//! bt-clippy-integration/src/callbacks.rs
//! BlockType 的 Clippy 整合回调 — 与 clippy-driver 几乎相同的 Callbacks 实现

#![feature(rustc_private)]
extern crate rustc_driver;
extern crate rustc_interface;
extern crate rustc_session;
extern crate rustc_lint;

use clippy_lints::register_plugins;
use clippy_lints::register_pre_expansion_lints;
use clippy_lints::register_renamed;

/// BlockType Clippy 回调
///
/// 实现逻辑与 clippy 的 ClippyCallbacks 几乎完全一致，
/// 额外注入 BlockType 的 AI 增强层和自定义 Emitter。
pub struct BtClippyCallbacks {
    /// AI 增强器（可选）
    ai_enhancer: Option<Arc<BtClippyEnhancer>>,
    /// 诊断收集器
    diagnostic_collector: Arc<DiagnosticCollector>,
    /// EventStore 引用
    event_store: Arc<EventStore>,
    /// 配置
    config: BtClippyConfig,
}

/// Clippy 整合配置
pub struct BtClippyConfig {
    /// 是否启用 AI 增强（叠加在 Clippy 结果之上）
    pub enable_ai_enhancement: bool,
    /// 是否启用 BlockType 补充 lint（Clippy 未覆盖的场景）
    pub enable_bt_supplementary_lints: bool,
    /// Clippy lint 级别覆盖（允许/警告/禁止）
    pub lint_overrides: HashMap<String, Level>,
    /// 是否同时执行编译（bt clippy --build）
    pub build_after_lint: bool,
}

impl Callbacks for BtClippyCallbacks {
    fn config(&mut self, config: &mut Config) {
        // 注入 BlockType 自定义 Emitter（收集 Clippy 诊断 + AI 增强）
        config.diagnostic = Some(Box::new(BtClippyEmitter::new(
            self.diagnostic_collector.clone(),
            self.ai_enhancer.clone(),
            self.event_store.clone(),
        )));
    }

    fn after_expansion(&mut self, compiler: &mut Compiler) -> Result<(), ErrorGuaranteed> {
        // ─── 注册 Clippy 的全部 lint passes（与 clippy-driver 完全一致）───
        let registry = compiler.query_mut(State::lint_store)?.unwrap();
        register_plugins(registry, self.config.lint_overrides.clone());
        register_pre_expansion_lints(registry);

        // ─── 注册 BlockType AI 增强层 ───
        if self.config.enable_bt_supplementary_lints {
            registry.register_late_pass(|tcx| Box::new(BtClippyEnhancer::new(tcx)));
        }

        Ok(())
    }

    fn after_analysis(&mut self, compiler: &mut Compiler) -> Result<(), ErrorGuaranteed> {
        // 如果配置了 --build，在 lint 后继续执行 MIR→BTIR 编译
        if self.config.build_after_lint {
            // 委托给 RustcBridge 的 after_analysis 逻辑
            // （MIR → BTIR 转换 + 后端代码生成）
        }
        Ok(())
    }
}
```

### 8.12.3 BtClippyEmitter — AI 增强的诊断输出

```rust
//! bt-clippy-integration/src/emitter.rs
//! 收集 Clippy 诊断 + AI 增强 + EventStore + Dashboard

/// BlockType Clippy 诊断 Emitter
///
/// 三层输出：
/// 1. 透传 Clippy 原始格式（终端输出与 `cargo clippy` 一致）
/// 2. 叠加 AI 增强解释（仅在 error/warning 级别）
/// 3. 结构化记录到 EventStore（可查询、可回放、可推送）
pub struct BtClippyEmitter {
    /// 原始 rustc emitter（保持终端输出一致）
    inner: Box<dyn Emitter>,
    /// 诊断收集器
    collector: Arc<DiagnosticCollector>,
    /// AI 增强器
    ai: Option<Arc<BtClippyEnhancer>>,
    /// EventStore
    event_store: Arc<EventStore>,
}

impl Emitter for BtClippyEmitter {
    fn emit_diagnostic(&mut self, diag: &Diagnostic) {
        // 1. 透传给原始 emitter（保持 Clippy 一致的终端输出）
        self.inner.emit_diagnostic(diag);

        // 2. 结构化收集
        let lint_name = diag.code.as_ref().map(|c| c.code.clone());
        let level = format!("{:?}", diag.level);
        let message = diag.message();
        let span = diag.span.clone();

        let lint_result = ClippyLintResult {
            lint_name: lint_name.clone(),
            level: level.clone(),
            message: message.clone(),
            file: span.primary().file.name.to_string(),
            line: span.primary().lo.line,
            column: span.primary().lo.col,
            suggestion: extract_suggestion(diag),
        };
        self.collector.push(lint_result.clone());

        // 3. AI 增强（仅 warning/error 且 AI 可用时）
        if matches!(diag.level, Level::Warning | Level::Error) {
            if let Some(ai) = &self.ai {
                let lint = lint_result.clone();
                // 异步 AI 增强（不阻塞编译）
                tokio::spawn(async move {
                    let enhancement = ai.enhance_clippy_lint(&lint).await;
                    // enhancement 通过 EventStore 推送到 Dashboard
                });
            }
        }

        // 4. 记录到 EventStore
        self.event_store.append(CompilerEvent::ClippyLintEmitted {
            lint_name: lint_result.lint_name,
            level: lint_result.level,
            file: lint_result.file,
            line: lint_result.line,
        });
    }
}
```

### 8.12.4 BtClippyEnhancer — AI 增强层

```rust
//! bt-clippy-integration/src/enhancer.rs
//! 在 Clippy 基础上叠加 BlockType AI 增强

/// BlockType AI Clippy 增强器
///
/// 职责：
/// 1. 为 Clippy 的 warning/error 叠加 AI 解释
/// 2. 补充 Clippy 未覆盖的场景（跨文件分析、架构级建议）
/// 3. 提供自动修复代码建议
///
/// 注意：这不是替代 Clippy，而是在 Clippy 结果上做增量增强。
pub struct BtClippyEnhancer {
    ai: Arc<AIOrchestrator>,
    rule_engine: BtLintRuleEngine,
}

/// BlockType 补充的 lint 规则（Clippy 未覆盖的场景）
///
/// 设计原则：不与 Clippy 重叠，只做 Clippy 做不到的事：
/// - 跨文件上下文分析
/// - 架构级模式建议
/// - IR 级性能洞察
pub struct BtLintRuleEngine {
    rules: Vec<Box<dyn BtLintRule>>,
}

/// BlockType 补充 Lint 规则 trait
pub trait BtLintRule: Send + Sync {
    fn name(&self) -> &str;
    fn category(&self) -> BtLintCategory;
    fn check(&self, ctx: &LintContext) -> Vec<BtLintSuggestion>;
}

pub enum BtLintCategory {
    /// 跨文件分析（Clippy 单文件无法做到）
    CrossFileAnalysis,
    /// 架构级建议（Clippy 不关注架构）
    ArchitectureSuggestion,
    /// IR 级性能洞察（需要 BlockType IR 层信息）
    IRPerformanceInsight,
    /// AI 驱动的模式识别
    AIPatternDetection,
}

impl BtClippyEnhancer {
    /// 为 Clippy 的 lint 结果叠加 AI 解释
    pub async fn enhance_clippy_lint(
        &self,
        lint: &ClippyLintResult,
    ) -> ClippyLintEnhancement {
        // 1. 规则引擎快速匹配（零延迟）
        if let Some(explanation) = self.rule_engine.explain(lint) {
            return ClippyLintEnhancement {
                ai_explanation: explanation,
                fix_suggestion: None,
                confidence: 1.0,
            };
        }

        // 2. AI 深度解释（异步）
        let ai_result = self.ai.explain_lint(lint).await;
        ClippyLintEnhancement {
            ai_explanation: ai_result.explanation,
            fix_suggestion: ai_result.fix_code,
            confidence: ai_result.confidence,
        }
    }
}
```

### 8.12.5 bt clippy 输出示例

```bash
$ bt clippy

# ─── Clippy 原始输出（与 cargo clippy 一致）───
warning: this loop could be written as a `for` loop
  --> src/main.rs:15:5
   |
15 |     let mut i = 0;
   |     ^^^^^^^^^^^^^ help: try: `for i in 0..10 { ... }`
   |
   = note: `#[warn(clippy::while_let_on_iterator)]` on by default

# ─── BlockType AI 增强（追加在 Clippy 输出之后）───
   [BlockType AI] 此模式在 Clippy 中标记为 while_let_on_iterator。
   在此项目中，类似的循环模式出现了 3 次（src/main.rs:15, src/lib.rs:42, src/utils.rs:7）。
   建议：批量重构为 iterator 模式可提升可读性。
   置信度: 0.96 | AI Provider: openai/gpt-4o

# ─── BlockType 补充 lint（Clippy 未覆盖的跨文件分析）───
warning: [BlockType] src/api/handler.rs:42 中的 `UserService` 直接依赖了 `DatabasePool`
  → 违反依赖倒置原则，建议抽象为 `UserRepository` trait
  → 影响范围：src/api/handler.rs:42, src/api/handler.rs:78, src/tests/handler_test.rs:15
  = note: [cross_file::dependency_inversion] AI 置信度 0.91

# ─── 汇总 ───
# Clippy: 3 warnings, 0 errors
# BlockType AI: 1 supplementary suggestion
# AI Budget: 340/5,000 tokens used
```

### 8.12.6 依赖关系变更

```
bt-cli
 ├── bt-cargo
 ├── bt-rustc-bridge                    # 编译模式（bt build）
 │    ├── [sysroot] rustc_*
 │    ├── bt-ir
 │    └── bt-core
 ├── bt-clippy-integration              # ← 新增：Clippy 整合层（bt clippy）★
 │    ├── clippy_lints                  # Cargo.toml 依赖 clippy（作为库）★
 │    ├── [sysroot] rustc_driver        # sysroot + extern crate
 │    ├── [sysroot] rustc_lint          # Lint 框架
 │    ├── [sysroot] rustc_interface
 │    ├── [sysroot] rustc_middle
 │    ├── bt-ai                         # AI 增强层
 │    ├── bt-event-store                # 事件记录
 │    ├── bt-core
 │    └── bt-rustc-bridge (optional)    # --build 模式时编译
 ├── bt-proc-macro
 ├── ...
```

**关键变更**：
- `bt-clippy-integration` 是新增 crate，是 Clippy 整合的唯一入口
- `clippy_lints` 作为 Cargo.toml 正常依赖引入（clippy 已发布到 crates.io）
- sysroot crate 仅用于 `rustc_driver` / `rustc_lint` / `rustc_interface` 等
- `bt-clippy-integration` 可选依赖 `bt-rustc-bridge`（`bt clippy --build` 模式）

### 8.12.7 整合 vs 自建的决策依据

| 维度 | 整合 Clippy（本方案 ✅） | 自建 AI Lint（❌） |
|------|-------------------------|-------------------|
| Lint 规则数 | Day 1: 700+（Clippy 全量） | Day 1: ~50（自建） |
| 社区信任 | Clippy 官方规则，经过数百万项目验证 | 自建规则需自行证明正确性 |
| 维护成本 | 跟随 clippy crate 升级即可 | 需要持续编写新规则 |
| AI 增强 | 在 Clippy 基础上叠加 AI 解释 | AI 从零生成所有建议 |
| 差异化 | BlockType 补充 Clippy 做不到的（跨文件/架构/IR 级） | 无差异化，与 Clippy 在同一维度竞争 |
| 开发时间 | ~1-2 周（桥接层） | ~5 周（规则引擎 + AI Lint） |
| 最终用户体验 | `bt clippy` = Clippy 700+ 规则 + AI 增强 | `bt clippy` = 50 规则（用户会失望） |

**结论**：整合是唯一正确的路径。BlockType 的差异化不在 Lint 规则数量，而在 AI 增强 + 编译器平台能力。

### 8.12.8 Clippy 版本管理

```toml
# bt-clippy-integration/Cargo.toml

[dependencies]
# clippy_lints 随 Rust 工具链发布，版本与 nightly 对齐
# clippy crate 在 crates.io 上与 Rust 版本同步发布
clippy_lints = "0.1"   # 与 rust-toolchain.toml 锁定的 nightly 版本对齐

# 当 nightly 版本更新时，clippy_lints 版本也需要同步更新
# CI nightly rolling 测试会自动检测不兼容
```

**版本同步策略**（与 §8.4.4 一致）：
1. `rust-toolchain.toml` 锁定 nightly 日期
2. CI 每日用 latest nightly 测试 clippy_lints 兼容性
3. 发现 API 变更 → 更新桥接层 + 更新 clippy_lints 版本
4. 每 6 周跟随 Rust 发布周期统一升级
