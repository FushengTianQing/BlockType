#!/bin/bash
# refactor_workflow.sh - AI coder 任务提醒、状态管理与 Checklist 强制执行脚本
#
# 用法:
#   ./refactor_workflow.sh              # 默认：显示当前任务提醒
#   ./refactor_workflow.sh start        # 标记当前任务开始，生成开发+审查 checklist
#   ./refactor_workflow.sh done         # 验证 checklist 完成情况，标记任务完成
#   ./refactor_workflow.sh check <id>   # 标记 checklist 某项为完成
#   ./refactor_workflow.sh uncheck <id> # 标记 checklist 某项为未完成
#   ./refactor_workflow.sh checklist    # 显示当前任务的 checklist
#   ./refactor_workflow.sh review       # 执行审查 checklist，输出审查问题清单
#   ./refactor_workflow.sh recover      # 断线恢复：检测状态，给出恢复指令
#   ./refactor_workflow.sh heartbeat    # 更新心跳时间戳（AI coder 定期调用）
#   ./refactor_workflow.sh status       # 查看当前详细状态
#   ./refactor_workflow.sh reset <task> # 重置到指定任务（慎用）

STATE_FILE="refactor_state.json"
HEARTBEAT_FILE="refactor_heartbeat"
LOG_DIR="refactor_logs"
CHECKLIST_DIR="refactor_checklists"
MAX_RETRIES=3
HEARTBEAT_TIMEOUT_SECONDS=600

mkdir -p "$LOG_DIR"
mkdir -p "$CHECKLIST_DIR"

log() {
    local level=$1
    local message=$2
    local timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    echo "[$timestamp] [$level] $message"
    echo "[$timestamp] [$level] $message" >> "$LOG_DIR/$(date +%Y-%m-%d).log"
}

print_workflow_rules() {
    log "INFO" "===================================="
    log "INFO" "🚨 任务流总体规则提醒 🚨"
    log "INFO" "1. 严格按照 11-15 AI coder 任务流文档执行"
    log "INFO" "2. 每个 Task 完成后必须执行 Git 提交并推送"
    log "INFO" "3. 遵循 Phase 文档中的总体要求和约束"
    log "INFO" "4. 确保接口签名与 01-04 文档一致"
    log "INFO" "5. 执行验收标准中的测试用例"
    log "INFO" "6. 注意接口间的关联关系（holding/calling）"
    log "INFO" "7. 遇到问题优先参考任务流文档中的解决方案"
    log "INFO" "8. ⚠️ 每个 Task 开始时必须生成 checklist，完成时必须验证 checklist"
    log "INFO" "9. ⚠️ 开发 checklist 全部完成 + 审查 checklist 通过后才能标记 done"
    log "INFO" "===================================="
}

get_next_task() {
    local task=$1
    case "$task" in
        "A.1")   echo "A.1.1" ;;
        "A.1.1") echo "A.1.2" ;;
        "A.1.2") echo "A.2"   ;;
        "A.2")   echo "A.3"   ;;
        "A.3")   echo "A.3.1" ;;
        "A.3.1") echo "A.4"   ;;
        "A.4")   echo "A.5"   ;;
        "A.5")   echo "A.6"   ;;
        "A.6")   echo "A.7"   ;;
        "A.7")   echo "A.8"   ;;
        "A.8")   echo "A.F1"  ;;
        "A.F1")  echo "A.F2"  ;;
        "A.F2")  echo "A.F3"  ;;
        "A.F3")  echo "A.F4"  ;;
        "A.F4")  echo "A.F5"  ;;
        "A.F5")  echo "A.F6"  ;;
        "A.F6")  echo "A.F7"  ;;
        "A.F7")  echo "A.F8"  ;;
        "A.F8")  echo "A.F9"  ;;
        "A.F9")  echo "A.F10" ;;
        "A.F10") echo "A.F11" ;;
        "A.F11") echo "A.F12" ;;
        "A.F12") echo "A.F13" ;;
        "A.F13") echo "B.1"   ;;
        "B.1")   echo "B.2"   ;;
        *)       echo "$task" ;;
    esac
}

get_task_description() {
    local task=$1
    case "$task" in
        "A.1")   echo "IRType 体系 + IRTypeContext" ;;
        "A.1.1") echo "IRContext + BumpPtrAllocator 内存管理" ;;
        "A.1.2") echo "IRThreadingMode 枚举 + seal 接口" ;;
        "A.2")   echo "TargetLayout（独立于 LLVM DataLayout）" ;;
        "A.3")   echo "IRValue + IRConstant + Use/User 体系" ;;
        "A.3.1") echo "IRFormatVersion + IRFileHeader" ;;
        "A.4")   echo "IRModule / IRFunction / IRBasicBlock / IRFunctionDecl" ;;
        "A.5")   echo "IRBuilder（含常量工厂）" ;;
        "A.6")   echo "IRVerifier" ;;
        "A.7")   echo "IRSerializer（文本 + 二进制格式）" ;;
        "A.8")   echo "CMake 集成 + 单元测试" ;;
        "A.F1")  echo "DialectCapability + DialectLoweringPass" ;;
        "A.F2")  echo "IRFeature + BackendCapability" ;;
        "A.F3")  echo "TelemetryCollector + PhaseGuard RAII" ;;
        "A.F4")  echo "IRInstruction 添加 Optional DbgInfo 字段" ;;
        "A.F5")  echo "IRDebugMetadata 基础类型定义 + 调试信息升级" ;;
        "A.F6")  echo "StructuredDiagnostic 基础结构定义" ;;
        "A.F7")  echo "CacheKey/CacheEntry + 编译器缓存架构" ;;
        "A.F8")  echo "IRIntegrityChecksum + IRSigner + 可重现构建" ;;
        "A.F9")  echo "IRConversionResult + IRVerificationResult" ;;
        "A.F10") echo "IRErrorCode 枚举" ;;
        "A.F11") echo "DeadFunctionEliminationPass + 其他优化 Pass" ;;
        "A.F12") echo "FFITypeMapper/FFIFunctionDecl/FFIModule" ;;
        "A.F13") echo "IR层头文件依赖图 + CMake子库拆分" ;;
        "B.1")   echo "FrontendBase + FrontendOptions + FrontendRegistry" ;;
        *)       echo "执行 Task $task" ;;
    esac
}

get_phase_doc() {
    local phase=$1
    case "$phase" in
        "A") echo "11-AI-Coder-任务流-PhaseA.md" ;;
        "B") echo "12-AI-Coder-任务流-PhaseB.md" ;;
        "C"|"D"|"E") echo "13-AI-Coder-任务流-PhaseCDE.md" ;;
        "F"|"G"|"H") echo "14-AI-Coder-任务流-PhaseFGH.md" ;;
        *) echo "未知 Phase" ;;
    esac
}

#===----------------------------------------------------------------------===//
# Checklist 生成：开发任务 checklist
#===----------------------------------------------------------------------===//
generate_dev_checklist() {
    local task=$1
    local checklist_file="$CHECKLIST_DIR/dev_${task}.json"

    if [ -f "$checklist_file" ]; then
        log "INFO" "开发 checklist 已存在: $checklist_file"
        return 0
    fi

    log "INFO" "生成开发 checklist: Task $task"

    case "$task" in
        "A.1")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.1",
  "type": "dev",
  "description": "IRType 体系 + IRTypeContext",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.1.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.1 全部内容，包括接口签名、实现约束、验收标准"},
    {"id": "A.1.D.02", "status": "pending", "category": "前提", "item": "阅读执行规则（文档开头7条规则）"},
    {"id": "A.1.D.03", "status": "pending", "category": "前提", "item": "阅读接口关联关系（持有关系、调用关系、生命周期约束、内存管理规则）"},
    {"id": "A.1.D.04", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRType.h"},
    {"id": "A.1.D.05", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRTypeContext.h"},
    {"id": "A.1.D.06", "status": "pending", "category": "产出", "item": "新增 src/IR/IRType.cpp"},
    {"id": "A.1.D.07", "status": "pending", "category": "产出", "item": "新增 src/IR/IRTypeContext.cpp"},
    {"id": "A.1.D.08", "status": "pending", "category": "实现", "item": "实现 DialectID 枚举（Core=0,Cpp=1,Target=2,Debug=3,Metadata=4）"},
    {"id": "A.1.D.09", "status": "pending", "category": "实现", "item": "实现 IRType 基类（Kind枚举10种、getDialect、equals、toString、getSizeInBits、getAlignInBits、isXxx判断方法、classof）"},
    {"id": "A.1.D.10", "status": "pending", "category": "实现", "item": "实现 IRVoidType（equals/toString/getSizeInBits/getAlignInBits/classof）"},
    {"id": "A.1.D.11", "status": "pending", "category": "实现", "item": "实现 IRIntegerType（BitWidth断言1/8/16/32/64/128、equals/toString/getSizeInBits/getAlignInBits/classof）"},
    {"id": "A.1.D.12", "status": "pending", "category": "实现", "item": "实现 IRFloatType（BitWidth断言16/32/64/80/128、equals/toString/getSizeInBits/getAlignInBits/classof）"},
    {"id": "A.1.D.13", "status": "pending", "category": "实现", "item": "实现 IRPointerType（PointeeType+AddressSpace、equals/toString/getSizeInBits/getAlignInBits/classof）"},
    {"id": "A.1.D.14", "status": "pending", "category": "实现", "item": "实现 IRArrayType（NumElements+ElementType、equals/toString/getSizeInBits/getAlignInBits/classof）"},
    {"id": "A.1.D.15", "status": "pending", "category": "实现", "item": "实现 IRStructType（Name+FieldTypes+IsPacked+FieldOffsets缓存、getFieldOffset/equals/toString/getSizeInBits/getAlignInBits/classof）"},
    {"id": "A.1.D.16", "status": "pending", "category": "实现", "item": "实现 IRFunctionType（ReturnType+ParamTypes+IsVarArg、equals/toString/getSizeInBits/getAlignInBits/classof）"},
    {"id": "A.1.D.17", "status": "pending", "category": "实现", "item": "实现 IRVectorType（NumElements+ElementType、断言2/4/8/16/32/64、equals/toString/getSizeInBits/getAlignInBits/classof）"},
    {"id": "A.1.D.18", "status": "pending", "category": "实现", "item": "实现 IROpaqueType（Name、equals/toString/getSizeInBits触发assert/getAlignInBits触发assert/classof）"},
    {"id": "A.1.D.19", "status": "pending", "category": "实现", "item": "实现 IRStructType 布局计算算法（非packed按ABI对齐+尾部padding，packed紧密排列，结果缓存）"},
    {"id": "A.1.D.20", "status": "pending", "category": "实现", "item": "实现 IRTypeContext（DenseMap/FoldingSet/StringMap缓存、所有get*Type方法、唯一化语义、getNumTypesCreated/getMemoryUsage）"},
    {"id": "A.1.D.21", "status": "pending", "category": "约束", "item": "验证：不依赖 LLVM（不 #include 任何 llvm/ 头文件）"},
    {"id": "A.1.D.22", "status": "pending", "category": "约束", "item": "验证：IRType 必须虚析构"},
    {"id": "A.1.D.23", "status": "pending", "category": "约束", "item": "验证：getSizeInBits 必须 const"},
    {"id": "A.1.D.24", "status": "pending", "category": "约束", "item": "验证：所有子类不可拷贝"},
    {"id": "A.1.D.25", "status": "pending", "category": "约束", "item": "验证：IRTypeContext 唯一化语义（相同参数返回相同指针）"},
    {"id": "A.1.D.26", "status": "pending", "category": "验收", "item": "V1: 整数类型大小 assert(IRIntegerType(32).getSizeInBits(Layout)==32)"},
    {"id": "A.1.D.27", "status": "pending", "category": "验收", "item": "V2: IRTypeContext 唯一化 assert(Int32_1==Int32_2)"},
    {"id": "A.1.D.28", "status": "pending", "category": "验收", "item": "V3: 指针类型唯一化 assert(PtrI8_1==PtrI8_2)"},
    {"id": "A.1.D.29", "status": "pending", "category": "动作", "item": "编译通过：cmake --build build --target blocktype-ir 退出码==0"},
    {"id": "A.1.D.30", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行 tests/IR/test_ir_type.cpp，所有 assert 通过"},
    {"id": "A.1.D.31", "status": "pending", "category": "动作", "item": "执行审查 checklist：./refactor_workflow.sh review"},
    {"id": "A.1.D.32", "status": "pending", "category": "动作", "item": "Git 提交并推送：git add -A && git commit -m 'feat(A): 完成 A.1 — IRType 体系 + IRTypeContext' && git push origin HEAD"}
  ]
}
CHECKLISTEOF
            ;;
        "A.1.1")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.1.1",
  "type": "dev",
  "description": "IRContext + BumpPtrAllocator 内存管理",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.1.1.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.1.1 全部内容"},
    {"id": "A.1.1.D.02", "status": "pending", "category": "前提", "item": "确认 A.1 已完成（依赖）"},
    {"id": "A.1.1.D.03", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRContext.h"},
    {"id": "A.1.1.D.04", "status": "pending", "category": "产出", "item": "新增 src/IR/IRContext.cpp"},
    {"id": "A.1.1.D.05", "status": "pending", "category": "实现", "item": "实现 IRThreadingMode 枚举（SingleThread/MultiInstance/SharedReadOnly）"},
    {"id": "A.1.1.D.06", "status": "pending", "category": "实现", "item": "实现 IRContext（BumpPtrAllocator+Cleanups+IRTypeContext+create<T>+saveString+getTypeContext+getMemoryUsage+sealModule）"},
    {"id": "A.1.1.D.07", "status": "pending", "category": "实现", "item": "实现 BumpPtrAllocator（从 blocktype-basic 引入，Allocate方法签名 void* Allocate(size_t,size_t)）"},
    {"id": "A.1.1.D.08", "status": "pending", "category": "约束", "item": "验证：不依赖 LLVM（使用自实现 BumpPtrAllocator）"},
    {"id": "A.1.1.D.09", "status": "pending", "category": "约束", "item": "验证：IRContext 拥有所有 IR 节点内存"},
    {"id": "A.1.1.D.10", "status": "pending", "category": "约束", "item": "验证：IRModule 仅拥有逻辑结构"},
    {"id": "A.1.1.D.11", "status": "pending", "category": "验收", "item": "V1: create 分配成功 assert(IntTy!=nullptr && IntTy->getBitWidth()==32)"},
    {"id": "A.1.1.D.12", "status": "pending", "category": "验收", "item": "V2: 析构自动释放（无内存泄漏）"},
    {"id": "A.1.1.D.13", "status": "pending", "category": "验收", "item": "V3: addCleanup 回调正确执行 assert(CleanupCalled==true)"},
    {"id": "A.1.1.D.14", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "A.1.1.D.15", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行 tests/IR/test_ir_context.cpp，所有 assert 通过"},
    {"id": "A.1.1.D.16", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "A.1.1.D.17", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
        "A.1.2")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.1.2",
  "type": "dev",
  "description": "IRThreadingMode 枚举 + seal 接口",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.1.2.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.1.2 全部内容"},
    {"id": "A.1.2.D.02", "status": "pending", "category": "前提", "item": "确认 A.1.1 已完成（依赖）"},
    {"id": "A.1.2.D.03", "status": "pending", "category": "实现", "item": "确保 IRThreadingMode 枚举已定义（SingleThread/MultiInstance/SharedReadOnly）"},
    {"id": "A.1.2.D.04", "status": "pending", "category": "实现", "item": "实现 sealModule(IRModule& M)：标记 IRModule 不可变，之后修改触发 assert"},
    {"id": "A.1.2.D.05", "status": "pending", "category": "约束", "item": "验证：SingleThread 为默认值"},
    {"id": "A.1.2.D.06", "status": "pending", "category": "约束", "item": "验证：sealModule 后 IRModule 不可修改（触发 assert 或返回错误）"},
    {"id": "A.1.2.D.07", "status": "pending", "category": "验收", "item": "V1: 默认线程模式 assert(Ctx.getThreadingMode()==SingleThread)"},
    {"id": "A.1.2.D.08", "status": "pending", "category": "验收", "item": "V2: 设置线程模式 assert(Ctx.getThreadingMode()==MultiInstance)"},
    {"id": "A.1.2.D.09", "status": "pending", "category": "验收", "item": "V3: sealModule 后不可修改"},
    {"id": "A.1.2.D.10", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "A.1.2.D.11", "status": "pending", "category": "动作", "item": "验收断言测试通过：线程模式断言已在 test_ir_context.cpp 中覆盖"},
    {"id": "A.1.2.D.12", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "A.1.2.D.13", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
        "A.2")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.2",
  "type": "dev",
  "description": "TargetLayout（独立于 LLVM DataLayout）",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.2.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.2 全部内容"},
    {"id": "A.2.D.02", "status": "pending", "category": "前提", "item": "确认 A.1 已完成（依赖）"},
    {"id": "A.2.D.03", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/TargetLayout.h"},
    {"id": "A.2.D.04", "status": "pending", "category": "产出", "item": "新增 src/IR/TargetLayout.cpp"},
    {"id": "A.2.D.05", "status": "pending", "category": "实现", "item": "实现 TargetLayout 类（TripleStr/PointerSize/IntSize/LongSize等布局参数、从Triple推断布局）"},
    {"id": "A.2.D.06", "status": "pending", "category": "实现", "item": "实现 getTypeSizeInBits(IRType*) 对各子类的计算规则"},
    {"id": "A.2.D.07", "status": "pending", "category": "实现", "item": "实现 getTypeAlignInBits(IRType*)"},
    {"id": "A.2.D.08", "status": "pending", "category": "实现", "item": "实现 getPointerSizeInBits()"},
    {"id": "A.2.D.09", "status": "pending", "category": "实现", "item": "实现 isLittleEndian()"},
    {"id": "A.2.D.10", "status": "pending", "category": "实现", "item": "实现 static Create(StringRef Triple) 工厂方法"},
    {"id": "A.2.D.11", "status": "pending", "category": "实现", "item": "支持 x86_64-unknown-linux-gnu 平台布局参数"},
    {"id": "A.2.D.12", "status": "pending", "category": "实现", "item": "支持 aarch64-unknown-linux-gnu 平台布局参数"},
    {"id": "A.2.D.13", "status": "pending", "category": "实现", "item": "支持 x86_64-apple-macosx 平台布局参数"},
    {"id": "A.2.D.14", "status": "pending", "category": "约束", "item": "验证：不依赖 LLVM DataLayout"},
    {"id": "A.2.D.15", "status": "pending", "category": "约束", "item": "验证：从 Triple 字符串推断布局"},
    {"id": "A.2.D.16", "status": "pending", "category": "验收", "item": "V1: x86_64 指针大小 assert(Layout->getPointerSizeInBits()==64)"},
    {"id": "A.2.D.17", "status": "pending", "category": "验收", "item": "V2: x86_64 小端 assert(Layout->isLittleEndian()==true)"},
    {"id": "A.2.D.18", "status": "pending", "category": "验收", "item": "V3: aarch64 小端 assert(LayoutARM->isLittleEndian()==true)"},
    {"id": "A.2.D.19", "status": "pending", "category": "验收", "item": "V4: 类型大小 assert(Layout->getTypeSizeInBits(Ctx.getInt32Ty())==32)"},
    {"id": "A.2.D.20", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "A.2.D.21", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行 tests/IR/test_target_layout.cpp，所有 assert 通过"},
    {"id": "A.2.D.22", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "A.2.D.23", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
        "A.3")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.3",
  "type": "dev",
  "description": "IRValue + IRConstant + Use/User 体系",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.3.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.3 全部内容"},
    {"id": "A.3.D.02", "status": "pending", "category": "前提", "item": "确认 A.1 + A.2 已完成（依赖）"},
    {"id": "A.3.D.03", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRValue.h"},
    {"id": "A.3.D.04", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRInstruction.h"},
    {"id": "A.3.D.05", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRConstant.h"},
    {"id": "A.3.D.06", "status": "pending", "category": "产出", "item": "新增 src/IR/IRValue.cpp"},
    {"id": "A.3.D.07", "status": "pending", "category": "产出", "item": "新增 src/IR/IRInstruction.cpp"},
    {"id": "A.3.D.08", "status": "pending", "category": "产出", "item": "新增 src/IR/IRConstant.cpp"},
    {"id": "A.3.D.09", "status": "pending", "category": "实现", "item": "实现 ValueKind 枚举（12种值类型）"},
    {"id": "A.3.D.10", "status": "pending", "category": "实现", "item": "实现 Opcode 枚举（全部操作码）"},
    {"id": "A.3.D.11", "status": "pending", "category": "实现", "item": "实现 ICmpPred/FCmpPred 枚举"},
    {"id": "A.3.D.12", "status": "pending", "category": "实现", "item": "实现 LinkageKind/CallingConvention/FunctionAttr 枚举"},
    {"id": "A.3.D.13", "status": "pending", "category": "实现", "item": "实现 IRValue（ValueKind+IRType*+ValueID+Name+replaceAllUsesWith+getNumUses+print）"},
    {"id": "A.3.D.14", "status": "pending", "category": "实现", "item": "实现 Use（Val+Owner双向引用+set维护def-use链）"},
    {"id": "A.3.D.15", "status": "pending", "category": "实现", "item": "实现 User（Operands列表+getOperand/setOperand/addOperand）"},
    {"id": "A.3.D.16", "status": "pending", "category": "实现", "item": "实现 IRInstruction（Opcode+DialectID+Parent+isTerminator/isBinaryOp/isCast/isMemoryOp/isComparison+eraseFromParent+print）"},
    {"id": "A.3.D.17", "status": "pending", "category": "实现", "item": "实现 IRConstant 系列（IRConstantInt/IRConstantFP/IRConstantNull/IRConstantUndef/IRConstantAggregateZero/IRConstantStruct/IRConstantArray/IRConstantFunctionRef/IRConstantGlobalRef）"},
    {"id": "A.3.D.18", "status": "pending", "category": "约束", "item": "验证：APInt 完整实现（位运算/算术/比较/截断扩展/toString），不得简化"},
    {"id": "A.3.D.19", "status": "pending", "category": "约束", "item": "验证：APFloat 完整实现（多精度 Semantics/算术/比较/bitcast），不得简化"},
    {"id": "A.3.D.20", "status": "pending", "category": "约束", "item": "验证：Use-Def chain 双向正确（Use::set 更新 def-use 链）"},
    {"id": "A.3.D.21", "status": "pending", "category": "约束", "item": "验证：IRConstant 子类不可变（immutable）"},
    {"id": "A.3.D.22", "status": "pending", "category": "约束", "item": "验证：IRConstantUndef 缓存（同一类型返回同一指针）"},
    {"id": "A.3.D.23", "status": "pending", "category": "约束", "item": "验证：所有文档中定义的方法签名均已完整实现，无遗漏无简化"},
    {"id": "A.3.D.24", "status": "pending", "category": "验收", "item": "V1: 常量整数值 assert(CI->getZExtValue()==42)"},
    {"id": "A.3.D.25", "status": "pending", "category": "验收", "item": "V2: Use/User 双向链接 getUser()->getOperand(0)==V1"},
    {"id": "A.3.D.26", "status": "pending", "category": "验收", "item": "V3: IRConstantUndef 缓存（同类型返回同一指针）"},
    {"id": "A.3.D.27", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "A.3.D.28", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行 tests/IR/test_ir_value.cpp，所有 assert 通过"},
    {"id": "A.3.D.29", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "A.3.D.30", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
        "A.3.1")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.3.1",
  "type": "dev",
  "description": "IRFormatVersion + IRFileHeader",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.3.1.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.3.1 全部内容"},
    {"id": "A.3.1.D.02", "status": "pending", "category": "前提", "item": "确认 A.1 已完成（依赖）"},
    {"id": "A.3.1.D.03", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRFormatVersion.h"},
    {"id": "A.3.1.D.04", "status": "pending", "category": "产出", "item": "新增 src/IR/IRFormatVersion.cpp"},
    {"id": "A.3.1.D.05", "status": "pending", "category": "实现", "item": "实现 IRFormatVersion（Major/Minor/Patch + Current()+isCompatibleWith()+toString()）"},
    {"id": "A.3.1.D.06", "status": "pending", "category": "实现", "item": "实现 IRFileHeader（Magic=BTIR + Version + Flags + ModuleOffset + StringTableOffset + StringTableSize）"},
    {"id": "A.3.1.D.07", "status": "pending", "category": "约束", "item": "验证：版本号独立于编译器版本"},
    {"id": "A.3.1.D.08", "status": "pending", "category": "约束", "item": "验证：魔数 BTIR"},
    {"id": "A.3.1.D.09", "status": "pending", "category": "约束", "item": "验证：isCompatibleWith 语义（Major相等，Reader Minor>=File Minor）"},
    {"id": "A.3.1.D.10", "status": "pending", "category": "约束", "item": "验证：sizeof(IRFileHeader) 为常量"},
    {"id": "A.3.1.D.11", "status": "pending", "category": "验收", "item": "V1: 当前版本 assert(Current().Major==1)"},
    {"id": "A.3.1.D.12", "status": "pending", "category": "验收", "item": "V2: 兼容性判断"},
    {"id": "A.3.1.D.13", "status": "pending", "category": "验收", "item": "V3: IRFileHeader 大小固定 static_assert"},
    {"id": "A.3.1.D.14", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "A.3.1.D.15", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行 tests/IR/test_ir_format.cpp，所有 assert 通过"},
    {"id": "A.3.1.D.16", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "A.3.1.D.17", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
        "A.4")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.4",
  "type": "dev",
  "description": "IRModule / IRFunction / IRBasicBlock / IRFunctionDecl",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.4.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.4 全部内容"},
    {"id": "A.4.D.02", "status": "pending", "category": "前提", "item": "确认 A.1 + A.3 已完成（依赖）"},
    {"id": "A.4.D.03", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRModule.h"},
    {"id": "A.4.D.04", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRFunction.h"},
    {"id": "A.4.D.05", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRBasicBlock.h"},
    {"id": "A.4.D.06", "status": "pending", "category": "产出", "item": "新增 src/IR/IRModule.cpp"},
    {"id": "A.4.D.07", "status": "pending", "category": "产出", "item": "新增 src/IR/IRFunction.cpp"},
    {"id": "A.4.D.08", "status": "pending", "category": "产出", "item": "新增 src/IR/IRBasicBlock.cpp"},
    {"id": "A.4.D.09", "status": "pending", "category": "实现", "item": "实现 IRArgument（ParamType+Name+ArgNo+Attrs+hasAttr/addAttr+print）"},
    {"id": "A.4.D.10", "status": "pending", "category": "实现", "item": "实现 IRBasicBlock（Parent+Name+InstList+getTerminator/getFirstNonPHI/getFirstInsertionPt+push_back/push_front/insert/erase+getPredecessors/getSuccessors+print）"},
    {"id": "A.4.D.11", "status": "pending", "category": "实现", "item": "实现 IRFunction（Parent+Name+Ty+Linkage+CallConv+Args+BasicBlocks+Attrs+addBasicBlock+getEntryBlock+isDeclaration/isDefinition+print）"},
    {"id": "A.4.D.12", "status": "pending", "category": "实现", "item": "实现 IRFunctionDecl（Name+Ty+Linkage+CallConv）"},
    {"id": "A.4.D.13", "status": "pending", "category": "实现", "item": "实现 IRGlobalVariable（Name+Ty+Linkage+Initializer+Alignment+IsConstant+Section+AddressSpace）"},
    {"id": "A.4.D.14", "status": "pending", "category": "实现", "item": "实现 IRGlobalAlias（Name+Ty+Aliasee）"},
    {"id": "A.4.D.15", "status": "pending", "category": "实现", "item": "实现 IRModule（TypeCtx+Name+TargetTriple+DataLayoutStr+Functions+FunctionDecls+Globals+Aliases+Metadata+IsReproducible+RequiredFeatures+getOrInsertFunction+getOrInsertGlobal+print）"},
    {"id": "A.4.D.16", "status": "pending", "category": "实现", "item": "实现 getOrInsertFunction 精确语义（同名同类型→返回已有；同名不同类型→nullptr；不存在→创建）"},
    {"id": "A.4.D.17", "status": "pending", "category": "约束", "item": "验证：IRFunction 持有 IRBasicBlock 列表"},
    {"id": "A.4.D.18", "status": "pending", "category": "约束", "item": "验证：IRModule 持有所有函数/全局变量"},
    {"id": "A.4.D.19", "status": "pending", "category": "约束", "item": "验证：getOrInsertFunction 语义与 LLVM 一致"},
    {"id": "A.4.D.20", "status": "pending", "category": "验收", "item": "V1: getOrInsertFunction 创建新函数 assert(F!=nullptr && F->getName()==\"foo\")"},
    {"id": "A.4.D.21", "status": "pending", "category": "验收", "item": "V2: addBasicBlock assert(Entry!=nullptr && Entry->getName()==\"entry\")"},
    {"id": "A.4.D.22", "status": "pending", "category": "验收", "item": "V3: getTerminator 无终结指令时返回 nullptr"},
    {"id": "A.4.D.23", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "A.4.D.24", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行对应测试文件，所有 assert 通过"},
    {"id": "A.4.D.25", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "A.4.D.26", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
        "A.5")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.5",
  "type": "dev",
  "description": "IRBuilder（含常量工厂）",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.5.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.5 全部内容"},
    {"id": "A.5.D.02", "status": "pending", "category": "前提", "item": "确认 A.3 + A.4 已完成（依赖）"},
    {"id": "A.5.D.03", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRBuilder.h"},
    {"id": "A.5.D.04", "status": "pending", "category": "产出", "item": "新增 src/IR/IRBuilder.cpp"},
    {"id": "A.5.D.05", "status": "pending", "category": "实现", "item": "实现 IRBuilder（InsertBB+InsertPt+TypeCtx+IRCtx+setInsertPoint+getInsertBlock）"},
    {"id": "A.5.D.06", "status": "pending", "category": "实现", "item": "实现常量工厂（getInt1/getInt32/getInt64/getNull/getUndef）"},
    {"id": "A.5.D.07", "status": "pending", "category": "实现", "item": "实现终结指令（createRet/createRetVoid/createBr/createCondBr/createInvoke）"},
    {"id": "A.5.D.08", "status": "pending", "category": "实现", "item": "实现算术指令（createAdd/createSub/createMul/createNeg）"},
    {"id": "A.5.D.09", "status": "pending", "category": "实现", "item": "实现比较指令（createICmp/createFCmp）"},
    {"id": "A.5.D.10", "status": "pending", "category": "实现", "item": "实现内存指令（createAlloca/createLoad/createStore/createGEP）"},
    {"id": "A.5.D.11", "status": "pending", "category": "实现", "item": "实现转换指令（createBitCast/createZExt/createSExt/createTrunc）"},
    {"id": "A.5.D.12", "status": "pending", "category": "实现", "item": "实现调用指令（createCall）"},
    {"id": "A.5.D.13", "status": "pending", "category": "实现", "item": "实现聚合操作（createExtractValue/createInsertValue）"},
    {"id": "A.5.D.14", "status": "pending", "category": "实现", "item": "实现 Phi/Select（createPhi/createSelect）"},
    {"id": "A.5.D.15", "status": "pending", "category": "约束", "item": "验证：所有 create* 方法返回非空指针"},
    {"id": "A.5.D.16", "status": "pending", "category": "约束", "item": "验证：InsertPoint 正确维护"},
    {"id": "A.5.D.17", "status": "pending", "category": "约束", "item": "验证：常量工厂与 IRTypeContext 缓存一致"},
    {"id": "A.5.D.18", "status": "pending", "category": "验收", "item": "V1: createAdd assert(Add!=nullptr)"},
    {"id": "A.5.D.19", "status": "pending", "category": "验收", "item": "V2: createRetVoid assert(Ret!=nullptr)"},
    {"id": "A.5.D.20", "status": "pending", "category": "验收", "item": "V3: createCall assert(Call!=nullptr)"},
    {"id": "A.5.D.21", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "A.5.D.22", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行对应测试文件，所有 assert 通过"},
    {"id": "A.5.D.23", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "A.5.D.24", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
        "A.6")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.6",
  "type": "dev",
  "description": "IRVerifier",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.6.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.6 全部内容"},
    {"id": "A.6.D.02", "status": "pending", "category": "前提", "item": "确认 A.4 + A.5 已完成（依赖）"},
    {"id": "A.6.D.03", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRVerifier.h"},
    {"id": "A.6.D.04", "status": "pending", "category": "产出", "item": "新增 src/IR/IRVerifier.cpp"},
    {"id": "A.6.D.05", "status": "pending", "category": "实现", "item": "实现 VerifierPass（Pass基类+getName+run+verifyType+verifyFunction+verifyBasicBlock+verifyInstruction）"},
    {"id": "A.6.D.06", "status": "pending", "category": "实现", "item": "实现验证检查项1：类型完整性（无 OpaqueType 残留）"},
    {"id": "A.6.D.07", "status": "pending", "category": "实现", "item": "实现验证检查项2：SSA 性质（Value 唯一定义）"},
    {"id": "A.6.D.08", "status": "pending", "category": "实现", "item": "实现验证检查项3：终结指令（每个 BB 恰好一个终结指令）"},
    {"id": "A.6.D.09", "status": "pending", "category": "实现", "item": "实现验证检查项4：类型匹配（操作数类型与指令要求一致）"},
    {"id": "A.6.D.10", "status": "pending", "category": "实现", "item": "实现验证检查项5：函数调用参数数量和类型匹配"},
    {"id": "A.6.D.11", "status": "pending", "category": "实现", "item": "实现验证检查项6：引用的 Function/GlobalVariable 存在"},
    {"id": "A.6.D.12", "status": "pending", "category": "验收", "item": "V1: 合法 IRModule 通过验证 assert(VerifierPass().run(LegalModule)==true)"},
    {"id": "A.6.D.13", "status": "pending", "category": "验收", "item": "V2: 含 OpaqueType 的 IRModule 不通过 assert(VerifierPass().run(ModuleWithOpaque)==false)"},
    {"id": "A.6.D.14", "status": "pending", "category": "验收", "item": "V3: 无终结指令的 BB 不通过 assert(VerifierPass().run(ModuleNoTerminator)==false)"},
    {"id": "A.6.D.15", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "A.6.D.16", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行对应测试文件，所有 assert 通过"},
    {"id": "A.6.D.17", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "A.6.D.18", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
        "A.7")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.7",
  "type": "dev",
  "description": "IRSerializer（文本 + 二进制格式）",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.7.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.7 全部内容，包括文本格式规范"},
    {"id": "A.7.D.02", "status": "pending", "category": "前提", "item": "确认 A.4 + A.3.1 已完成（依赖）"},
    {"id": "A.7.D.03", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRSerializer.h"},
    {"id": "A.7.D.04", "status": "pending", "category": "产出", "item": "新增 src/IR/IRSerializer.cpp"},
    {"id": "A.7.D.05", "status": "pending", "category": "实现", "item": "实现 IRWriter::writeText（按文本格式规范输出 module/global/function/basic_block/instruction）"},
    {"id": "A.7.D.06", "status": "pending", "category": "实现", "item": "实现 IRWriter::writeBitcode（IRFileHeader + 模块数据 + 字符串表，版本号写入文件头）"},
    {"id": "A.7.D.07", "status": "pending", "category": "实现", "item": "实现 IRReader::parseText（解析文本格式，调用 IRTypeContext/IRModule/IRBuilder 创建对象）"},
    {"id": "A.7.D.08", "status": "pending", "category": "实现", "item": "实现 IRReader::parseBitcode（验证魔数 BTIR + 解析二进制数据）"},
    {"id": "A.7.D.09", "status": "pending", "category": "实现", "item": "实现 IRReader::readFile（自动检测文本/二进制格式并调用对应解析方法）"},
    {"id": "A.7.D.10", "status": "pending", "category": "实现", "item": "实现字符串表的写入和读取"},
    {"id": "A.7.D.11", "status": "pending", "category": "约束", "item": "验证：文本格式人类可读"},
    {"id": "A.7.D.12", "status": "pending", "category": "约束", "item": "验证：二进制格式紧凑（IRFileHeader+模块数据+字符串表）"},
    {"id": "A.7.D.13", "status": "pending", "category": "约束", "item": "验证：版本号写入文件头"},
    {"id": "A.7.D.14", "status": "pending", "category": "验收", "item": "V1: 文本格式往返（writeText→parseText→比较结构等价性）"},
    {"id": "A.7.D.15", "status": "pending", "category": "验收", "item": "V2: 二进制格式往返（writeBitcode→parseBitcode→比较结构等价性）"},
    {"id": "A.7.D.16", "status": "pending", "category": "验收", "item": "V3: IRFileHeader 魔数（二进制数据前4字节==BTIR）"},
    {"id": "A.7.D.17", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "A.7.D.18", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行对应测试文件，所有 assert 通过"},
    {"id": "A.7.D.19", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "A.7.D.20", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
        "A.8")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.8",
  "type": "dev",
  "description": "CMake 集成 + 单元测试",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.8.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.8 全部内容"},
    {"id": "A.8.D.02", "status": "pending", "category": "前提", "item": "确认 A.1~A.7 全部完成（依赖）"},
    {"id": "A.8.D.03", "status": "pending", "category": "产出", "item": "新增/修改 src/IR/CMakeLists.txt"},
    {"id": "A.8.D.04", "status": "pending", "category": "产出", "item": "修改 CMakeLists.txt（添加 IR 子目录）"},
    {"id": "A.8.D.05", "status": "pending", "category": "产出", "item": "新增 tests/unit/IR/ 下所有测试文件"},
    {"id": "A.8.D.06", "status": "pending", "category": "实现", "item": "CMake 目标定义：add_library(blocktype-ir ...) 包含所有 .cpp 文件"},
    {"id": "A.8.D.07", "status": "pending", "category": "实现", "item": "target_link_libraries(blocktype-ir PUBLIC blocktype-basic) — 不链接 LLVM"},
    {"id": "A.8.D.08", "status": "pending", "category": "实现", "item": "target_include_directories(blocktype-ir PUBLIC ${CMAKE_SOURCE_DIR}/include)"},
    {"id": "A.8.D.09", "status": "pending", "category": "实现", "item": "创建 IRType 单元测试"},
    {"id": "A.8.D.10", "status": "pending", "category": "实现", "item": "创建 IRTypeContext 单元测试"},
    {"id": "A.8.D.11", "status": "pending", "category": "实现", "item": "创建 IRValue/IRInstruction 单元测试"},
    {"id": "A.8.D.12", "status": "pending", "category": "实现", "item": "创建 IRModule/IRFunction/IRBasicBlock 单元测试"},
    {"id": "A.8.D.13", "status": "pending", "category": "实现", "item": "创建 IRBuilder 单元测试"},
    {"id": "A.8.D.14", "status": "pending", "category": "实现", "item": "创建 IRVerifier 单元测试"},
    {"id": "A.8.D.15", "status": "pending", "category": "实现", "item": "创建 IRSerializer 单元测试"},
    {"id": "A.8.D.16", "status": "pending", "category": "实现", "item": "创建 TargetLayout 单元测试"},
    {"id": "A.8.D.17", "status": "pending", "category": "约束", "item": "验证：libblocktype-ir 不链接 LLVM"},
    {"id": "A.8.D.18", "status": "pending", "category": "约束", "item": "验证：所有单元测试通过"},
    {"id": "A.8.D.19", "status": "pending", "category": "约束", "item": "验证：lit 测试通过"},
    {"id": "A.8.D.20", "status": "pending", "category": "验收", "item": "V1: 构建成功 cmake --build build --target blocktype-ir 退出码==0"},
    {"id": "A.8.D.21", "status": "pending", "category": "验收", "item": "V2: 单元测试全部通过 cd build && ctest -R 'IR' --output-on-failure"},
    {"id": "A.8.D.22", "status": "pending", "category": "验收", "item": "V3: libblocktype-ir 无 LLVM 符号 nm libblocktype-ir.a | grep 'llvm::' | wc -l ==0"},
    {"id": "A.8.D.23", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "A.8.D.24", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行对应测试文件，所有 assert 通过"},
    {"id": "A.8.D.25", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "A.8.D.26", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
        "A.F1")
            cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "A.F1",
  "type": "dev",
  "description": "DialectCapability + DialectLoweringPass",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "A.F1.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task A.F1 全部内容，包括降级规则表和后端能力声明"},
    {"id": "A.F1.D.02", "status": "pending", "category": "前提", "item": "确认 A.1 + A.3 已完成（依赖）"},
    {"id": "A.F1.D.03", "status": "pending", "category": "产出", "item": "修改 include/blocktype/IR/IRType.h（确认 DialectID 字段已集成）"},
    {"id": "A.F1.D.04", "status": "pending", "category": "产出", "item": "修改 include/blocktype/IR/IRInstruction.h（确认 DialectID 字段已集成）"},
    {"id": "A.F1.D.05", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/IRDialect.h"},
    {"id": "A.F1.D.06", "status": "pending", "category": "产出", "item": "新增 src/IR/IRDialect.cpp"},
    {"id": "A.F1.D.07", "status": "pending", "category": "产出", "item": "新增 include/blocktype/IR/DialectLoweringPass.h"},
    {"id": "A.F1.D.08", "status": "pending", "category": "产出", "item": "新增 src/IR/DialectLoweringPass.cpp"},
    {"id": "A.F1.D.09", "status": "pending", "category": "实现", "item": "实现 DialectCapability（位掩码 declareDialect/hasDialect/supportsAll/getUnsupported/getSupportedMask）"},
    {"id": "A.F1.D.10", "status": "pending", "category": "实现", "item": "实现 DialectLoweringPass（Pass基类+getName+run+LoweringRule+LoweringRules静态表）"},
    {"id": "A.F1.D.11", "status": "pending", "category": "实现", "item": "实现降级规则1：bt_cpp Invoke → bt_core call+landingpad布局"},
    {"id": "A.F1.D.12", "status": "pending", "category": "实现", "item": "实现降级规则2：bt_cpp Resume → bt_core unreachable"},
    {"id": "A.F1.D.13", "status": "pending", "category": "实现", "item": "实现降级规则3：bt_cpp dynamic_cast → bt_core call @__dynamic_cast"},
    {"id": "A.F1.D.14", "status": "pending", "category": "实现", "item": "实现降级规则4：bt_cpp vtable.dispatch → bt_core load vptr+gep idx+indirect_call"},
    {"id": "A.F1.D.15", "status": "pending", "category": "实现", "item": "实现降级规则5：bt_cpp RTTI.typeid → bt_core call @__typeid"},
    {"id": "A.F1.D.16", "status": "pending", "category": "实现", "item": "实现降级规则6：bt_target target.intrinsic → bt_core 目标特定函数调用"},
    {"id": "A.F1.D.17", "status": "pending", "category": "实现", "item": "实现降级规则7：bt_meta meta.inline.always → bt_core 附加AlwaysInline属性"},
    {"id": "A.F1.D.18", "status": "pending", "category": "实现", "item": "实现降级规则8：bt_meta meta.inline.never → bt_core 附加NoInline属性"},
    {"id": "A.F1.D.19", "status": "pending", "category": "实现", "item": "实现降级规则9：bt_meta meta.hot → bt_core 附加Hot属性"},
    {"id": "A.F1.D.20", "status": "pending", "category": "实现", "item": "实现降级规则10：bt_meta meta.cold → bt_core 附加Cold属性"},
    {"id": "A.F1.D.21", "status": "pending", "category": "实现", "item": "实现各后端 Dialect 能力声明（LLVM:0x1F, Cranelift:0x09）"},
    {"id": "A.F1.D.22", "status": "pending", "category": "约束", "item": "验证：DialectID 作为 IRType/IRInstruction 的附加字段，不改变现有接口"},
    {"id": "A.F1.D.23", "status": "pending", "category": "约束", "item": "验证：现有指令默认属于 bt_core"},
    {"id": "A.F1.D.24", "status": "pending", "category": "约束", "item": "验证：DialectCapability 使用位掩码，与 BackendCapability 正交"},
    {"id": "A.F1.D.25", "status": "pending", "category": "验收", "item": "V1: DialectCapability 位掩码 assert(Cap.hasDialect(Core) && Cap.hasDialect(Cpp) && !Cap.hasDialect(Debug))"},
    {"id": "A.F1.D.26", "status": "pending", "category": "验收", "item": "V2: DialectLoweringPass 可运行（构建含 bt_cpp Invoke 的 IRModule，Pass.run 返回 true）"},
    {"id": "A.F1.D.27", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "A.F1.D.28", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行对应测试文件，所有 assert 通过"},
    {"id": "A.F1.D.29", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "A.F1.D.30", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
        *)
            local desc=$(get_task_description "$task")
            cat > "$checklist_file" << CHECKLISTEOF
{
  "task": "$task",
  "type": "dev",
  "description": "$desc",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "${task}.D.01", "status": "pending", "category": "前提", "item": "阅读任务流文档 Task $task 全部内容"},
    {"id": "${task}.D.02", "status": "pending", "category": "前提", "item": "确认依赖任务已完成"},
    {"id": "${task}.D.03", "status": "pending", "category": "实现", "item": "按任务流文档实现所有接口签名和功能"},
    {"id": "${task}.D.04", "status": "pending", "category": "约束", "item": "验证所有实现约束"},
    {"id": "${task}.D.05", "status": "pending", "category": "验收", "item": "执行所有验收标准"},
    {"id": "${task}.D.06", "status": "pending", "category": "动作", "item": "编译通过"},
    {"id": "${task}.D.07", "status": "pending", "category": "动作", "item": "验收断言测试通过：编译运行对应测试文件，所有 assert 通过"},
    {"id": "${task}.D.08", "status": "pending", "category": "动作", "item": "执行审查 checklist"},
    {"id": "${task}.D.09", "status": "pending", "category": "动作", "item": "Git 提交并推送"}
  ]
}
CHECKLISTEOF
            ;;
    esac

    log "INFO" "开发 checklist 已生成: $checklist_file ($(jq '.items | length' "$checklist_file") 项)"
}

#===----------------------------------------------------------------------===//
# Checklist 生成：审查代码 checklist
#===----------------------------------------------------------------------===//
generate_review_checklist() {
    local task=$1
    local checklist_file="$CHECKLIST_DIR/review_${task}.json"

    if [ -f "$checklist_file" ]; then
        log "INFO" "审查 checklist 已存在: $checklist_file"
        return 0
    fi

    log "INFO" "生成审查 checklist: Task $task"

    cat > "$checklist_file" << 'CHECKLISTEOF'
{
  "task": "TEMPLATE",
  "type": "review",
  "description": "审查代码 checklist",
  "warning": "严格按照文档要点，完全实现。禁止简化参数、函数、算法。",
  "items": [
    {"id": "R.01", "status": "pending", "category": "接口签名", "item": "逐项对标任务流文档中的 class/struct/enum/函数签名，确认完全一致"},
    {"id": "R.02", "status": "pending", "category": "接口签名", "item": "核对构造函数参数（含默认参数），确认与文档一致"},
    {"id": "R.03", "status": "pending", "category": "接口签名", "item": "核对返回值类型，确认与文档一致"},
    {"id": "R.04", "status": "pending", "category": "接口签名", "item": "核对 const 修饰，确认与文档一致"},
    {"id": "R.05", "status": "pending", "category": "命名空间", "item": "确认所有代码在 namespace blocktype::ir 中"},
    {"id": "R.06", "status": "pending", "category": "命名空间", "item": "确认 DialectID 在 namespace blocktype::ir::dialect 中"},
    {"id": "R.07", "status": "pending", "category": "文件路径", "item": "确认头文件在 include/blocktype/IR/ 下"},
    {"id": "R.08", "status": "pending", "category": "文件路径", "item": "确认源文件在 src/IR/ 下"},
    {"id": "R.09", "status": "pending", "category": "依赖限制", "item": "确认 libblocktype-ir 不链接 LLVM（检查 CMakeLists.txt）"},
    {"id": "R.10", "status": "pending", "category": "依赖限制", "item": "确认不 #include 任何 llvm/ 头文件（如文档要求不依赖LLVM）"},
    {"id": "R.11", "status": "pending", "category": "持有关系", "item": "核对接口关联关系图中的持有关系（owns/ref）是否正确实现"},
    {"id": "R.12", "status": "pending", "category": "调用关系", "item": "核对接口关联关系图中的调用关系是否正确实现"},
    {"id": "R.13", "status": "pending", "category": "生命周期", "item": "核对生命周期约束（IRContext>=IRModule>=IRFunction>=IRBasicBlock>=IRInstruction）"},
    {"id": "R.14", "status": "pending", "category": "内存管理", "item": "核对内存管理规则（分配方式、拥有者、释放方式）"},
    {"id": "R.15", "status": "pending", "category": "实现约束", "item": "逐项核对任务流文档中的实现约束"},
    {"id": "R.16", "status": "pending", "category": "验收标准", "item": "逐项执行验收标准中的断言"},
    {"id": "R.17", "status": "pending", "category": "编译", "item": "确认编译通过，无错误无警告"},
    {"id": "R.18", "status": "pending", "category": "代码质量", "item": "检查是否有占位符实现（如仅返回 true/nullptr 的空函数体）"},
    {"id": "R.19", "status": "pending", "category": "代码质量", "item": "检查是否有 TODO/FIXME/HACK 等标记"},
    {"id": "R.20", "status": "pending", "category": "代码质量", "item": "检查是否有硬编码的魔法数字"},
    {"id": "R.21", "status": "pending", "category": "完整性", "item": "确认所有产出文件已创建（对照文档中的产出文件表）"},
    {"id": "R.22", "status": "pending", "category": "完整性", "item": "确认所有枚举值已定义（对照文档中的枚举定义）"},
    {"id": "R.23", "status": "pending", "category": "完整性", "item": "确认所有方法已实现（对照文档中的接口签名）"},
    {"id": "R.24", "status": "pending", "category": "严禁简化", "item": "对照文档逐项核查：APInt/APFloat/Use-Def chain/布局算法等是否完整实现所有参数和算法，严禁简化"},
    {"id": "R.25", "status": "pending", "category": "Git", "item": "确认 Git 提交信息格式正确 feat(<phase>): 完成 <Task编号> — <Task标题>"},
    {"id": "R.26", "status": "pending", "category": "Git", "item": "确认已推送到远端 git push origin HEAD"}
  ]
}
CHECKLISTEOF

    local tmp=$(mktemp)
    jq --arg t "$task" '.task = $t' "$checklist_file" > "$tmp" && mv "$tmp" "$checklist_file"

    log "INFO" "审查 checklist 已生成: $checklist_file ($(jq '.items | length' "$checklist_file") 项)"
}

#===----------------------------------------------------------------------===//
# Checklist 操作
#===----------------------------------------------------------------------===//
display_checklist() {
    local task=$1
    local checklist_type=${2:-"dev"}
    local checklist_file="$CHECKLIST_DIR/${checklist_type}_${task}.json"

    if [ ! -f "$checklist_file" ]; then
        log "WARNING" "Checklist 文件不存在: $checklist_file"
        log "INFO" "请先执行: $0 start 生成 checklist"
        return 1
    fi

    local total=$(jq '.items | length' "$checklist_file")
    local done=$(jq '[.items[] | select(.status == "done")] | length' "$checklist_file")
    local pending=$(jq '[.items[] | select(.status == "pending")] | length' "$checklist_file")

    echo ""
    echo "╔══════════════════════════════════════════════════════════════╗"
    local upper_type=$(echo "$checklist_type" | tr '[:lower:]' '[:upper:]')
    echo "║  📋 ${upper_type} Checklist: Task $task"
    echo "║  进度: $done/$total 完成, $pending 待完成"
    echo "╠══════════════════════════════════════════════════════════════╣"

    local categories=$(jq -r '.items[].category' "$checklist_file" | sort -u)
    for cat in $categories; do
        echo "║"
        echo "║  【$cat】"
        local cat_items=$(jq -c --arg c "$cat" '.items[] | select(.category == $c)' "$checklist_file")
        echo "$cat_items" | jq -r '. | if .status == "done" then "║  [✅] \(.id) \(.item)" else "║  [⬜] \(.id) \(.item)" end'
    done
    echo "╚══════════════════════════════════════════════════════════════╝"
    echo ""
}

checklist_item_check() {
    local item_id=$1
    local task=$(jq -r '.current_task' "$STATE_FILE")
    local checklist_type=${2:-"dev"}
    local checklist_file="$CHECKLIST_DIR/${checklist_type}_${task}.json"

    if [ ! -f "$checklist_file" ]; then
        log "ERROR" "Checklist 文件不存在: $checklist_file"
        return 1
    fi

    local tmp=$(mktemp)
    jq --arg id "$item_id" '(.items[] | select(.id == $id)).status = "done"' "$checklist_file" > "$tmp" && mv "$tmp" "$checklist_file"
    log "INFO" "✅ Checklist 项 $item_id 已标记为完成"
}

checklist_item_uncheck() {
    local item_id=$1
    local task=$(jq -r '.current_task' "$STATE_FILE")
    local checklist_type=${2:-"dev"}
    local checklist_file="$CHECKLIST_DIR/${checklist_type}_${task}.json"

    if [ ! -f "$checklist_file" ]; then
        log "ERROR" "Checklist 文件不存在: $checklist_file"
        return 1
    fi

    local tmp=$(mktemp)
    jq --arg id "$item_id" '(.items[] | select(.id == $id)).status = "pending"' "$checklist_file" > "$tmp" && mv "$tmp" "$checklist_file"
    log "INFO" "⬜ Checklist 项 $item_id 已标记为未完成"
}

check_checklist_completion() {
    local task=$1
    local checklist_type=${2:-"dev"}
    local checklist_file="$CHECKLIST_DIR/${checklist_type}_${task}.json"

    if [ ! -f "$checklist_file" ]; then
        log "ERROR" "Checklist 文件不存在: $checklist_file"
        return 1
    fi

    local total=$(jq '.items | length' "$checklist_file")
    local done=$(jq '[.items[] | select(.status == "done")] | length' "$checklist_file")
    local pending=$(jq '[.items[] | select(.status == "pending")] | length' "$checklist_file")

    local upper_type=$(echo "$checklist_type" | tr '[:lower:]' '[:upper:]')
    if [ "$pending" -gt 0 ]; then
        log "WARNING" "⚠️  ${upper_type} Checklist 尚未全部完成: $done/$total, $pending 项未完成"
        echo ""
        echo "未完成项:"
        jq -r '.items[] | select(.status == "pending") | "  ⬜ \(.id): \(.item)"' "$checklist_file"
        echo ""
        return 1
    fi

    log "INFO" "✅ ${upper_type} Checklist 全部完成: $done/$total"
    return 0
}

#===----------------------------------------------------------------------===//
# 状态管理
#===----------------------------------------------------------------------===//
init_state() {
    if [ ! -f "$STATE_FILE" ]; then
        log "INFO" "初始化状态文件"
        local now=$(date -u +%Y-%m-%dT%H:%M:%SZ)
        cat > "$STATE_FILE" << STATEEOF
{
  "current_phase": "A",
  "current_task": "A.1",
  "task_status": "pending",
  "completed_tasks": [],
  "task_started_at": null,
  "task_completed_at": null,
  "last_heartbeat": null,
  "retry_count": 0,
  "last_executed": "$now"
}
STATEEOF
    fi
}

read_state() {
    if [ ! -f "$STATE_FILE" ]; then
        init_state
    fi

    CURRENT_PHASE=$(jq -r '.current_phase' "$STATE_FILE")
    CURRENT_TASK=$(jq -r '.current_task' "$STATE_FILE")
    TASK_STATUS=$(jq -r '.task_status // "pending"' "$STATE_FILE")
    TASK_STARTED_AT=$(jq -r '.task_started_at // "null"' "$STATE_FILE")
    RETRY_COUNT=$(jq -r '.retry_count' "$STATE_FILE")
    COMPLETED_COUNT=$(jq -r '.completed_tasks | length' "$STATE_FILE")
    COMPLETED_TASKS=$(jq -r '.completed_tasks | join(", ")' "$STATE_FILE")

    log "INFO" "当前状态: Phase=$CURRENT_PHASE Task=$CURRENT_TASK Status=$TASK_STATUS 已完成=$COMPLETED_COUNT"
}

write_state() {
    local phase="$1"
    local task="$2"
    local status="$3"
    local completed_json="$4"
    local started_at="$5"
    local retry="$6"

    local now=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    local completed_at="null"
    if [ "$status" = "done" ]; then
        completed_at="\"$now\""
    fi

    local started_json="null"
    if [ "$started_at" != "" ] && [ "$started_at" != "null" ]; then
        started_json="\"$started_at\""
    fi

    cat > "$STATE_FILE.tmp" << STATEEOF
{
  "current_phase": "$phase",
  "current_task": "$task",
  "task_status": "$status",
  "completed_tasks": $completed_json,
  "task_started_at": $started_json,
  "task_completed_at": $completed_at,
  "last_heartbeat": null,
  "retry_count": $retry,
  "last_executed": "$now"
}
STATEEOF

    mv "$STATE_FILE.tmp" "$STATE_FILE"
}

check_environment() {
    log "INFO" "检测环境状态..."
    if git rev-parse --git-dir > /dev/null 2>&1; then
        log "INFO" "Git仓库状态正常"
    else
        log "WARNING" "Git仓库状态检查失败"
    fi
    log "INFO" "环境检查完成"
    return 0
}

generate_task_reminder() {
    local task=$1
    local status=$2
    local description=$(get_task_description "$task")
    local phase=$3
    local doc=$(get_phase_doc "$phase")

    cat << EOF

================================================================================
📢 AI CODER 任务提醒 📢

当前任务: Task $task [$status]
任务描述: $description

🔍 执行要求:
1. 参考文档: docs/plan/多前端后端重构方案/$doc
2. 遵循 Phase $phase 文档中的总体要求
3. 严格按照任务流中的接口签名和实现约束
4. 完成后执行: ./refactor_workflow.sh done
5. 执行验收标准中的测试用例
6. 完成后 Git 提交并推送

⚠️  Checklist 强制要求:
- 开发 checklist 全部 ✅ 后才能执行 done
- 审查 checklist 必须执行并全部 ✅
- 使用 ./refactor_workflow.sh check <id> 标记完成
- 使用 ./refactor_workflow.sh checklist 查看进度

⚠️  注意事项:
- 保持接口签名与 01-04 文档一致
- 注意接口间的关联关系（holding/calling）
- 遇到问题参考任务流文档中的解决方案
- 确保代码符合项目的编码规范
- 工作期间定期执行: ./refactor_workflow.sh heartbeat
================================================================================

EOF
}

#===----------------------------------------------------------------------===//
# 命令实现
#===----------------------------------------------------------------------===//
cmd_default() {
    log "INFO" "=== 显示当前任务 ==="
    print_workflow_rules
    read_state
    check_environment
    generate_task_reminder "$CURRENT_TASK" "$TASK_STATUS" "$CURRENT_PHASE"
}

cmd_start() {
    log "INFO" "=== 标记任务开始: $(jq -r '.current_task' "$STATE_FILE") ==="
    read_state

    if [ "$TASK_STATUS" = "done" ]; then
        log "WARNING" "Task $CURRENT_TASK 已完成，不应重新开始"
        log "INFO" "如需重做，请先执行: $0 reset $CURRENT_TASK"
        return 1
    fi

    local completed_json=$(jq -c '.completed_tasks' "$STATE_FILE")
    local now=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    local retry=$(jq -r '.retry_count' "$STATE_FILE")

    write_state "$CURRENT_PHASE" "$CURRENT_TASK" "in_progress" "$completed_json" "$now" "$retry"

    touch "$HEARTBEAT_FILE"

    generate_dev_checklist "$CURRENT_TASK"
    generate_review_checklist "$CURRENT_TASK"

    log "INFO" "任务 $CURRENT_TASK 已标记为 in_progress"
    log "INFO" "⚠️  请按照开发 checklist 逐项执行，每完成一项使用 check <id> 标记"
    generate_task_reminder "$CURRENT_TASK" "in_progress" "$CURRENT_PHASE"
    display_checklist "$CURRENT_TASK" "dev"
}

cmd_done() {
    log "INFO" "=== 标记任务完成: $(jq -r '.current_task' "$STATE_FILE") ==="
    read_state

    if [ "$TASK_STATUS" = "done" ]; then
        log "WARNING" "Task $CURRENT_TASK 已经是完成状态"
        return 0
    fi

    log "INFO" "验证开发 checklist 完成情况..."
    if ! check_checklist_completion "$CURRENT_TASK" "dev"; then
        log "ERROR" "❌ 开发 checklist 未全部完成，不能标记任务为 done"
        log "INFO" "请完成未完成的 checklist 项，或使用 check <id> 标记完成"
        return 1
    fi

    log "INFO" "验证审查 checklist 完成情况..."
    if ! check_checklist_completion "$CURRENT_TASK" "review"; then
        log "ERROR" "❌ 审查 checklist 未全部完成，不能标记任务为 done"
        log "INFO" "请执行审查并使用 check <id> 标记完成"
        return 1
    fi

    local completed_json=$(jq -c ".completed_tasks + [\"$CURRENT_TASK\"]" "$STATE_FILE")
    local next_task=$(get_next_task "$CURRENT_TASK")
    local next_phase="$CURRENT_PHASE"

    if [[ "$next_task" == B.* ]] && [ "$CURRENT_PHASE" = "A" ]; then
        next_phase="B"
    fi

    write_state "$next_phase" "$next_task" "pending" "$completed_json" "null" "0"

    rm -f "$HEARTBEAT_FILE"

    log "INFO" "✅ Task $CURRENT_TASK 完成！推进到 Task $next_task"
    log "INFO" "📋 下一个任务: $next_task - $(get_task_description "$next_task")"
    log "INFO" "📖 参考文档: docs/plan/多前端后端重构方案/$(get_phase_doc "$next_phase")"
}

cmd_check() {
    local item_id="${2:-}"
    if [ -z "$item_id" ]; then
        log "ERROR" "用法: $0 check <item_id> [dev|review]"
        return 1
    fi
    local checklist_type="${3:-dev}"
    checklist_item_check "$item_id" "$checklist_type"
    read_state
    display_checklist "$CURRENT_TASK" "$checklist_type"
}

cmd_uncheck() {
    local item_id="${2:-}"
    if [ -z "$item_id" ]; then
        log "ERROR" "用法: $0 uncheck <item_id> [dev|review]"
        return 1
    fi
    local checklist_type="${3:-dev}"
    checklist_item_uncheck "$item_id" "$checklist_type"
    read_state
    display_checklist "$CURRENT_TASK" "$checklist_type"
}

cmd_checklist() {
    read_state
    local checklist_type="${2:-dev}"
    display_checklist "$CURRENT_TASK" "$checklist_type"
}

cmd_review() {
    read_state
    log "INFO" "=== 执行审查 checklist: Task $CURRENT_TASK ==="

    generate_review_checklist "$CURRENT_TASK"

    echo ""
    echo "╔══════════════════════════════════════════════════════════════╗"
    echo "║  🔍 审查 Checklist: Task $CURRENT_TASK"
    echo "║  请逐项核查代码，发现问题后记录到审查问题清单"
    echo "╠══════════════════════════════════════════════════════════════╣"
    echo ""

    local review_file="$CHECKLIST_DIR/review_${CURRENT_TASK}.json"
    if [ ! -f "$review_file" ]; then
        log "ERROR" "审查 checklist 文件不存在"
        return 1
    fi

    local issues_file="$CHECKLIST_DIR/review_issues_${CURRENT_TASK}.md"
    echo "# 审查问题清单 - Task $CURRENT_TASK" > "$issues_file"
    echo "" >> "$issues_file"
    echo "生成时间: $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$issues_file"
    echo "" >> "$issues_file"

    local total=$(jq '.items | length' "$review_file")
    local idx=0
    local issue_count=0

    while [ "$idx" -lt "$total" ]; do
        local item_id=$(jq -r ".items[$idx].id" "$review_file")
        local item_cat=$(jq -r ".items[$idx].category" "$review_file")
        local item_desc=$(jq -r ".items[$idx].item" "$review_file")

        echo "📋 [$item_id] [$item_cat] $item_desc"
        echo "   请核查并输入结果 (pass/fail/skip):"

        read -r result
        case "$result" in
            pass)
                checklist_item_check "$item_id" "review"
                echo "   ✅ 通过"
                ;;
            fail)
                echo "   请输入问题描述:"
                read -r issue_desc
                issue_count=$((issue_count + 1))
                echo "- [$item_id] $item_desc — ❌ 问题: $issue_desc" >> "$issues_file"
                echo "   ❌ 问题已记录"
                ;;
            skip)
                echo "   ⏭️  跳过"
                echo "- [$item_id] $item_desc — ⏭️ 跳过" >> "$issues_file"
                ;;
            *)
                echo "   未知输入，默认为 fail"
                echo "- [$item_id] $item_desc — ❌ 未验证" >> "$issues_file"
                issue_count=$((issue_count + 1))
                ;;
        esac

        idx=$((idx + 1))
    done

    echo "" >> "$issues_file"
    echo "## 审查统计" >> "$issues_file"
    echo "- 问题数量: $issue_count" >> "$issues_file"

    echo ""
    echo "╚══════════════════════════════════════════════════════════════╝"
    echo ""
    log "INFO" "审查完成，问题数量: $issue_count"
    log "INFO" "审查问题清单已保存: $issues_file"

    if [ "$issue_count" -gt 0 ]; then
        log "WARNING" "⚠️  存在 $issue_count 个审查问题，请修复后再执行 done"
        echo ""
        echo "审查问题清单:"
        cat "$issues_file"
    fi
}

cmd_heartbeat() {
    local now=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    touch "$HEARTBEAT_FILE"
    local tmp=$(mktemp)
    jq --arg ts "$now" '.last_heartbeat = $ts' "$STATE_FILE" > "$tmp" && mv "$tmp" "$STATE_FILE"
    log "INFO" "💓 心跳更新: $now"
}

cmd_recover() {
    log "INFO" "=== 🔧 断线恢复检测 ==="
    read_state

    local now_epoch=$(date +%s)
    local hb_status="无心跳"

    if [ -f "$HEARTBEAT_FILE" ]; then
        local hb_epoch=$(stat -f %m "$HEARTBEAT_FILE" 2>/dev/null || echo 0)
        local diff=$((now_epoch - hb_epoch))
        if [ "$diff" -lt "$HEARTBEAT_TIMEOUT_SECONDS" ]; then
            hb_status="存活 (最后心跳 ${diff}s 前)"
        else
            hb_status="超时 (最后心跳 ${diff}s 前，超时阈值 ${HEARTBEAT_TIMEOUT_SECONDS}s)"
        fi
    fi

    local started_info="未开始"
    if [ "$TASK_STARTED_AT" != "null" ] && [ "$TASK_STARTED_AT" != "" ]; then
        started_info="开始于 $TASK_STARTED_AT"
    fi

    echo ""
    echo "╔══════════════════════════════════════════════════════════════╗"
    echo "║              🔧 断线恢复报告                                ║"
    echo "╠══════════════════════════════════════════════════════════════╣"
    echo "║  当前 Phase:  $CURRENT_PHASE"
    echo "║  当前 Task:   $CURRENT_TASK"
    echo "║  任务状态:    $TASK_STATUS"
    echo "║  开始时间:    $started_info"
    echo "║  心跳状态:    $hb_status"
    echo "║  已完成任务:  $COMPLETED_TASKS"
    echo "║  任务描述:    $(get_task_description "$CURRENT_TASK")"
    echo "╚══════════════════════════════════════════════════════════════╝"
    echo ""

    case "$TASK_STATUS" in
        "pending")
            log "INFO" "📌 状态=pending: 任务尚未开始，应执行: $0 start"
            log "INFO" "然后开始实现 $CURRENT_TASK"
            ;;
        "in_progress")
            log "WARNING" "⚠️  状态=in_progress: 任务执行中，可能因中断未完成"
            log "INFO" "恢复步骤:"
            log "INFO" "  1. 检查 Git 工作区是否有未提交的变更: git status"
            log "INFO" "  2. 检查 checklist 进度: $0 checklist"
            log "INFO" "  3a. 如果 checklist 全部完成 → 执行: $0 done"
            log "INFO" "  3b. 如果 checklist 未完成 → 继续完成，用 check <id> 标记"
            log "INFO" "  3c. 如果需要重做 → 执行: $0 reset $CURRENT_TASK"
            ;;
        "done")
            log "INFO" "✅ 状态=done: 任务已完成"
            local next_task=$(get_next_task "$CURRENT_TASK")
            log "INFO" "下一个任务: $next_task - $(get_task_description "$next_task")"
            log "INFO" "执行: $0 start 开始下一个任务"
            ;;
        *)
            log "ERROR" "未知状态: $TASK_STATUS，建议执行: $0 reset $CURRENT_TASK"
            ;;
    esac

    log "INFO" "参考文档: docs/plan/多前端后端重构方案/$(get_phase_doc "$CURRENT_PHASE")"

    echo ""
    echo "📋 Checklist 进度:"
    local dev_file="$CHECKLIST_DIR/dev_${CURRENT_TASK}.json"
    local review_file="$CHECKLIST_DIR/review_${CURRENT_TASK}.json"
    if [ -f "$dev_file" ]; then
        local dev_total=$(jq '.items | length' "$dev_file")
        local dev_done=$(jq '[.items[] | select(.status == "done")] | length' "$dev_file")
        echo "  开发 checklist: $dev_done/$dev_total 完成"
    else
        echo "  开发 checklist: 未生成（执行 start 生成）"
    fi
    if [ -f "$review_file" ]; then
        local rev_total=$(jq '.items | length' "$review_file")
        local rev_done=$(jq '[.items[] | select(.status == "done")] | length' "$review_file")
        echo "  审查 checklist: $rev_done/$rev_total 完成"
    else
        echo "  审查 checklist: 未生成（执行 start 生成）"
    fi
}

cmd_status() {
    read_state
    echo ""
    echo "当前状态:"
    echo "  Phase:         $CURRENT_PHASE"
    echo "  Task:          $CURRENT_TASK"
    echo "  Status:        $TASK_STATUS"
    echo "  开始时间:      $TASK_STARTED_AT"
    echo "  描述:          $(get_task_description "$CURRENT_TASK")"
    echo "  已完成任务数:  $(jq '.completed_tasks | length' "$STATE_FILE")"
    echo "  已完成列表:    $COMPLETED_TASKS"

    if [ -f "$HEARTBEAT_FILE" ]; then
        local hb_epoch=$(stat -f %m "$HEARTBEAT_FILE" 2>/dev/null || echo 0)
        local now_epoch=$(date +%s)
        local diff=$((now_epoch - hb_epoch))
        echo "  心跳:          ${diff}s 前"
    else
        echo "  心跳:          无"
    fi
    echo "  参考文档:      docs/plan/多前端后端重构方案/$(get_phase_doc "$CURRENT_PHASE")"

    echo ""
    echo "📋 Checklist 进度:"
    local dev_file="$CHECKLIST_DIR/dev_${CURRENT_TASK}.json"
    local review_file="$CHECKLIST_DIR/review_${CURRENT_TASK}.json"
    if [ -f "$dev_file" ]; then
        local dev_total=$(jq '.items | length' "$dev_file")
        local dev_done=$(jq '[.items[] | select(.status == "done")] | length' "$dev_file")
        echo "  开发 checklist: $dev_done/$dev_total 完成"
    else
        echo "  开发 checklist: 未生成"
    fi
    if [ -f "$review_file" ]; then
        local rev_total=$(jq '.items | length' "$review_file")
        local rev_done=$(jq '[.items[] | select(.status == "done")] | length' "$review_file")
        echo "  审查 checklist: $rev_done/$rev_total 完成"
    else
        echo "  审查 checklist: 未生成"
    fi
    echo ""
}

cmd_reset() {
    local target_task="${2:-A.1}"
    local target_phase="${target_task%%.*}"

    log "WARNING" "⚠️  重置状态到 Task $target_task (Phase $target_phase)"

    local completed_json="[]"

    cat > "$STATE_FILE" << STATEEOF
{
  "current_phase": "$target_phase",
  "current_task": "$target_task",
  "task_status": "pending",
  "completed_tasks": $completed_json,
  "task_started_at": null,
  "task_completed_at": null,
  "last_heartbeat": null,
  "retry_count": 0,
  "last_executed": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
STATEEOF

    rm -f "$HEARTBEAT_FILE"

    local dev_file="$CHECKLIST_DIR/dev_${target_task}.json"
    local review_file="$CHECKLIST_DIR/review_${target_task}.json"
    if [ -f "$dev_file" ]; then
        mv "$dev_file" "${dev_file}.bak.$(date +%Y%m%d)"
        log "INFO" "旧开发 checklist 已备份"
    fi
    if [ -f "$review_file" ]; then
        mv "$review_file" "${review_file}.bak.$(date +%Y%m%d)"
        log "INFO" "旧审查 checklist 已备份"
    fi

    log "INFO" "状态已重置。执行: $0 start 开始任务"
}

case "${1:-}" in
    start)
        cmd_start
        ;;
    done)
        cmd_done
        ;;
    check)
        cmd_check "$@"
        ;;
    uncheck)
        cmd_uncheck "$@"
        ;;
    checklist)
        cmd_checklist "$@"
        ;;
    review)
        cmd_review
        ;;
    heartbeat)
        cmd_heartbeat
        ;;
    recover)
        cmd_recover
        ;;
    status)
        cmd_status
        ;;
    reset)
        cmd_reset "$@"
        ;;
    *)
        cmd_default
        ;;
esac