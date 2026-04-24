# BlockType 项目规则

## Checklist 执行规则

1. **禁止批量标记 checklist 项**。每个 checklist 项必须逐个标记，每标记一项前必须实际执行或验证该项的内容。
2. **禁止使用 shell 循环批量 check**。例如 `for i in $(seq 1 25); do refactor_workflow.sh check R.$i review; done` 这样的命令是被禁止的。
3. **审查 checklist（review）必须在代码完成后逐项对照文档审查**，不能仅因为代码编译通过就全部标记完成。
4. **约束验证类 checklist 必须有实际的验证动作**（如 grep 检查无 LLVM 依赖、确认虚析构存在等），不能仅凭记忆标记。

## 代码规则

5. **不依赖 LLVM**：IR 层代码（`include/blocktype/IR/` 和 `src/IR/`）不得直接 `#include` 任何 `llvm/` 头文件。使用标准 C++ 类型替代 LLVM 类型。
6. **严格按文档签名实现**：接口签名必须与任务流文档一致，类型映射关系：`StringRef → std::string`/`std::string_view`，`SmallVector → std::vector`，`DenseMap → std::unordered_map`。
7. **不在代码中添加注释**，除非用户明确要求。

## 任务执行规则

8. **每个 Task 必须先阅读文档**，确认依赖和产出文件，再开始编码。
9. **编译验证**：每个 Task 完成后必须执行编译，确保零错误。
10. **Git 提交**：每个 Task 完成后立即 git commit + push。
