# Task B.5 优化版：IREmitExpr（表达式发射器）

> 规格来源：`12-AI-Coder-任务流-PhaseB.md` 第 612~708 行
> 优化者：planner | 日期：2026-04-26

---

## 规格修正清单（dev-tester 必读）

| # | 原规格问题 | 修正方案 | 影响范围 |
|---|-----------|---------|---------|
| 1 | `CXXBoolLiteralExpr` 类名不存在 | 项目实际类名为 **`CXXBoolLiteral`**（`Expr.h:271`）。方法名 `EmitBoolLiteral` 参数类型改为 `const CXXBoolLiteral*` | 头文件/实现 |
| 2 | `BinaryOperator::BO_Add` 等 Opcode 名不存在 | 项目枚举是 **`BinaryOpKind`**（`Expr.h:35`），值名为 `Add`/`Sub`/`Mul`（无 `BO_` 前缀）。getter 为 `getOpcode()` 返回 `BinaryOpKind` | 实现文件 |
| 3 | `IRBuilder` 缺少 16 个 `create*` 方法 | `Opcode` 枚举已有 `SDiv/UDiv/SRem/URem/Shl/LShr/AShr/And/Or/Xor/SIToFP/UIToFP/FPToSI/FPToUI/PtrToInt/IntToPtr`，但 `IRBuilder` 缺少对应的 `create*` 包装方法。需添加 | `IRBuilder.h/.cpp` |
| 4 | 规格构造函数 `IREmitExpr(C, B)` 带 Builder 参数 | 桩文件只有 `Converter_`，Builder 可通过 `Converter_.getBuilder()` 获取。保持单参数构造函数 | 头文件 |
| 5 | `IRGlobalVariable` 不是 `IRValue` 子类 | `EmitDeclRefExpr` 引用全局变量时，需用 **`IRConstantGlobalRef`**（`IRConstant.h:117`）包装成 `IRValue*` | 实现 |
| 6 | `IRArgument` 不是 `IRValue` 子类 | `IRFunction::getArg()` 返回 `IRArgument*`，不能传给 `createStore(IRValue*, IRValue*)`。需将 `IRArgument` 改为继承 `IRValue`（`ValueKind::Argument` 已预留） | `IRFunction.h` + B.4 修复 |
| 7 | `IRFunction` 不是 `IRValue` 子类 | `EmitCallExpr` 的被调函数通过 `createCall(IRFunction*, ...)` 传递，**不**需要 `IRValue*` 包装。但若表达式引用函数名（如 `&foo`），需用 **`IRConstantFunctionRef`** 包装 | 实现 |
| 8 | 规格映射表用 `Opcode::Add` 等 | 项目 `Opcode` 在 `ir` 命名空间（`IRValue.h:31`）。`BinaryOpKind` 到 `Opcode` 的映射在 `EmitBinaryExpr` 中硬编码 switch | 实现 |
| 9 | 短路求值 `&&/\|\|` 生成 `CondBr` | 需要创建新 `IRBasicBlock`，但 `IRBasicBlock` 构造函数是 `IRFunction::addBasicBlock(StringRef)` 工厂方法。通过 `IRFunction*` 的引用获取 | 实现 |

---

## Part 1: 前置修改（dev-tester 必须先完成）

### 1.1 修改 `include/blocktype/IR/IRFunction.h` — IRArgument 继承 IRValue

**原因**：`ValueKind::Argument` 已在 `IRValue.h:28` 预留，`IRArgument` 不继承 `IRValue` 是设计遗漏。B.4 的 `createStore(Arg, Alloca)` 也依赖此修复。

```cpp
// 修改前（IRFunction.h:17-34）：
class IRArgument {
  IRType* ParamType;
  std::string Name;
  unsigned ArgNo;
  unsigned Attrs = 0;
public:
  IRArgument(IRType* T, unsigned No, StringRef N = "")
    : ParamType(T), Name(N.str()), ArgNo(No) {}
  IRType* getType() const { return ParamType; }
  StringRef getName() const { return Name; }
  unsigned getArgNo() const { return ArgNo; }
  // ...
  void print(raw_ostream& OS) const;
};

// 修改后：
#include "blocktype/IR/IRValue.h"  // 确保已包含

class IRArgument : public IRValue {
  unsigned ArgNo;
  unsigned Attrs = 0;

public:
  IRArgument(IRType* T, unsigned No, StringRef N = "")
    : IRValue(ValueKind::Argument, T, No, N), ArgNo(No) {}

  unsigned getArgNo() const { return ArgNo; }
  bool hasAttr(unsigned A) const { return (Attrs & A) != 0; }
  void addAttr(unsigned A) { Attrs |= A; }
  // getType() 和 getName() 由 IRValue 基类提供

  static bool classof(const IRValue* V) {
    return V->getValueKind() == ValueKind::Argument;
  }

  void print(raw_ostream& OS) const override;
};
```

**注意**：`IRFunction` 中 `SmallVector<std::unique_ptr<IRArgument>, 8> Args;` 不需要修改（智能指针管理不变）。但 `IRArgument` 的析构函数现在由 `IRValue` 基类 `virtual ~IRValue()` 处理。

### 1.2 修改 `include/blocktype/IR/IRBuilder.h` — 添加 16 个 create* 方法

在 `createNeg` 方法后、`createICmp` 方法前添加：

```cpp
  //===--- Integer Division / Remainder ---===//

  IRInstruction* createSDiv(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createUDiv(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createSRem(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createURem(IRValue* LHS, IRValue* RHS, StringRef Name = "");

  //===--- Bitwise Operations ---===//

  IRInstruction* createAnd(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createOr(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createXor(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createNot(IRValue* V, StringRef Name = "");

  //===--- Shift Operations ---===//

  IRInstruction* createShl(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createLShr(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createAShr(IRValue* LHS, IRValue* RHS, StringRef Name = "");

  //===--- Type Conversions ---===//

  IRInstruction* createSIToFP(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createUIToFP(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createFPToSI(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createFPToUI(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createPtrToInt(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createIntToPtr(IRValue* V, IRType* DestTy, StringRef Name = "");
```

### 1.3 修改 `src/IR/IRBuilder.cpp` — 实现 16 个新方法

每个方法的实现模式相同（以 `createSDiv` 为例）：

```cpp
IRInstruction* IRBuilder::createSDiv(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto* I = new IRInstruction(Opcode::SDiv, LHS->getType(),
                              IRCtx_.nextValueID(), dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::unique_ptr<IRInstruction>(I));
}

// 同理实现其他 15 个方法，替换 Opcode::SDiv 为对应值
// createUDiv → Opcode::UDiv
// createSRem → Opcode::SRem
// createURem → Opcode::URem
// createAnd  → Opcode::And
// createOr   → Opcode::Or
// createXor  → Opcode::Xor
// createNot  → Opcode::Xor  (Not = Xor with all-ones mask: V ^ -1)
// createShl  → Opcode::Shl
// createLShr → Opcode::LShr
// createAShr → Opcode::AShr
// createSIToFP → Opcode::SIToFP
// createUIToFP → Opcode::UIToFP
// createFPToSI → Opcode::FPToSI
// createFPToUI → Opcode::FPToUI
// createPtrToInt → Opcode::PtrToInt
// createIntToPtr → Opcode::IntToPtr
```

**`createNot` 特殊处理**：

```cpp
IRInstruction* IRBuilder::createNot(IRValue* V, StringRef Name) {
  // Not = XOR with all-ones constant
  auto* AllOnes = getInt64(static_cast<uint64_t>(-1));
  // 但 V 可能不是 64-bit，需要根据 V 的类型创建正确宽度的 -1
  // 简化方案：直接使用 Xor + 全 1 常量
  auto* NegOne = IRConstantInt(
    llvm::dyn_cast<IRIntegerType>(V->getType()),
    static_cast<uint64_t>(-1)  // APInt 会自动截断
  );
  return createXor(V, &NegOne, Name);
}
```

> **注意**：`createNot` 的上述实现需要使用 `IRContext` 分配 `IRConstantInt`。如果 `IRBuilder` 没有 `IRContext` 引用来创建常量，则 `createNot` 可降级为在 `IREmitExpr` 中直接使用 `createXor(V, AllOnes)`。当前 `IRBuilder` 有 `IRCtx_` 成员，可以创建常量。

### 1.4 添加 `IRContext::nextValueID()` 方法（如果不存在）

检查 `IRContext` 是否有 `nextValueID()` 方法。如果没有，需要添加：

```cpp
// include/blocktype/IR/IRContext.h
class IRContext {
  unsigned NextValueID = 0;
public:
  unsigned nextValueID() { return NextValueID++; }
  // ...
};
```

> **注意**：如果 `IRBuilder::insertHelper` 已有内部 ID 分配机制，则不需要此修改。需检查现有 `IRBuilder.cpp` 实现中 `insertHelper` 的具体做法。

---

## Part 2: 头文件（完整代码）

### 2.1 替换 `include/blocktype/Frontend/IREmitExpr.h`

用以下完整版本替换现有桩文件：

```cpp
//===--- IREmitExpr.h - IR Expression Emitter ----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the IREmitExpr class, which converts AST expressions
// to IR instructions. Part of the Frontend → IR pipeline (Phase B, Task B.5).
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_IREMITEEXPR_H
#define BLOCKTYPE_FRONTEND_IREMITEEXPR_H

#include "blocktype/AST/Expr.h"
#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRValue.h"

namespace blocktype {
namespace frontend {

class ASTToIRConverter;

/// IREmitExpr - Converts AST expression nodes to IR instructions.
///
/// Each Emit*Expr method:
///   - Accepts a specific AST expression type
///   - Returns an IRValue* representing the expression result
///   - On failure, returns emitErrorPlaceholder(T) via the Converter
///
/// Value categories:
///   - LValue expressions return a pointer IRValue* (address of the value)
///   - RValue expressions return a non-pointer IRValue* (the value itself)
///   - Callers needing rvalues from lvalues must emit an additional Load
///
/// Thread safety: Not thread-safe. One instance per ASTToIRConverter.
class IREmitExpr {
public:
  explicit IREmitExpr(ASTToIRConverter& Converter);
  ~IREmitExpr() = default;

  // Non-copyable
  IREmitExpr(const IREmitExpr&) = delete;
  IREmitExpr& operator=(const IREmitExpr&) = delete;

  //===--- Binary/Unary Operators ---===//

  /// Emit IR for a binary operator expression.
  /// Handles arithmetic, comparison, logical, bitwise, and assignment ops.
  ir::IRValue* EmitBinaryExpr(const BinaryOperator* BO);

  /// Emit IR for a unary operator expression.
  /// Handles prefix/postfix increment/decrement, dereference, address-of, etc.
  ir::IRValue* EmitUnaryExpr(const UnaryOperator* UO);

  //===--- Call Expressions ---===//

  /// Emit IR for a function call expression.
  /// Ensures the callee function has been emitted first.
  ir::IRValue* EmitCallExpr(const CallExpr* CE);

  /// Emit IR for a member function call expression.
  /// Delegates to EmitCallExpr after handling object pointer ('this' arg).
  ir::IRValue* EmitCXXMemberCallExpr(const CXXMemberCallExpr* MCE);

  //===--- Access Expressions ---===//

  /// Emit IR for a member access expression (x.field or p->field).
  /// Returns the address of the member (lvalue).
  ir::IRValue* EmitMemberExpr(const MemberExpr* ME);

  /// Emit IR for a declaration reference expression.
  /// Looks up the IRValue from DeclValues/GlobalVars/Functions maps.
  ir::IRValue* EmitDeclRefExpr(const DeclRefExpr* DRE);

  //===--- Cast Expressions ---===//

  /// Emit IR for a cast expression.
  /// Dispatches to appropriate conversion based on CastKind.
  ir::IRValue* EmitCastExpr(const CastExpr* CE);

  //===--- C++ Specific Expressions ---===//

  /// Emit IR for a constructor call expression.
  ir::IRValue* EmitCXXConstructExpr(const CXXConstructExpr* CCE);

  /// Emit IR for a new expression.
  ir::IRValue* EmitCXXNewExpr(const CXXNewExpr* NE);

  /// Emit IR for a delete expression.
  ir::IRValue* EmitCXXDeleteExpr(const CXXDeleteExpr* DE);

  /// Emit IR for the 'this' pointer expression.
  ir::IRValue* EmitCXXThisExpr(const CXXThisExpr* TE);

  //===--- Conditional / Init ---===//

  /// Emit IR for a ternary conditional operator (cond ? true : false).
  /// Generates phi node with CondBr for branch control flow.
  ir::IRValue* EmitConditionalOperator(const ConditionalOperator* CO);

  /// Emit IR for a brace-enclosed initializer list.
  ir::IRValue* EmitInitListExpr(const InitListExpr* ILE);

  //===--- Literals ---===//

  /// Emit IR for a string literal.
  ir::IRValue* EmitStringLiteral(const StringLiteral* SL);

  /// Emit IR for an integer literal.
  ir::IRValue* EmitIntegerLiteral(const IntegerLiteral* IL);

  /// Emit IR for a floating-point literal.
  ir::IRValue* EmitFloatingLiteral(const FloatingLiteral* FL);

  /// Emit IR for a character literal.
  ir::IRValue* EmitCharacterLiteral(const CharacterLiteral* CL);

  /// Emit IR for a boolean literal (true/false).
  ir::IRValue* EmitBoolLiteral(const CXXBoolLiteral* BLE);

  //===--- General Dispatch ---===//

  /// Emit IR for any expression, dispatching to the correct Emit*Expr method.
  /// Returns nullptr on failure (should not happen if AST is well-formed).
  ir::IRValue* Emit(const Expr* E);

private:
  ASTToIRConverter& Converter_;

  //===--- Helper Methods ---===//

  /// Get the IRBuilder from the converter.
  ir::IRBuilder& getBuilder();

  /// Emit an error placeholder value for failed expression conversion.
  ir::IRValue* emitErrorPlaceholder(ir::IRType* T);

  /// Emit an error type for failed type mapping.
  ir::IRType* emitErrorType();

  /// Emit short-circuit evaluation for && or || operators.
  ir::IRValue* emitShortCircuitEval(const BinaryOperator* BO);

  /// Emit assignment operator (BO_Assign).
  ir::IRValue* emitAssignment(const BinaryOperator* BO);

  /// Emit compound assignment operator (e.g., +=, -=).
  ir::IRValue* emitCompoundAssignment(const BinaryOperator* BO);

  /// Map AST QualType to IR Type, with error recovery.
  ir::IRType* mapType(QualType T);

  /// Check if a QualType is a signed integer type.
  bool isSignedType(QualType T);

  /// Create a new basic block in the current function.
  ir::IRBasicBlock* createBasicBlock(ir::StringRef Name);
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_IREMITEEXPR_H
```

---

## Part 3: 实现文件（完整代码）

### 3.1 新增 `src/Frontend/IREmitExpr.cpp`

```cpp
//===--- IREmitExpr.cpp - IR Expression Emitter -----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/IREmitExpr.h"
#include "blocktype/Frontend/ASTToIRConverter.h"

#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRFunction.h"

namespace blocktype {
namespace frontend {

//===----------------------------------------------------------------------===//
// Construction
//===----------------------------------------------------------------------===//

IREmitExpr::IREmitExpr(ASTToIRConverter& Converter)
  : Converter_(Converter) {}

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

ir::IRBuilder& IREmitExpr::getBuilder() {
  return Converter_.getBuilder();
}

ir::IRValue* IREmitExpr::emitErrorPlaceholder(ir::IRType* T) {
  return Converter_.emitErrorPlaceholder(T);
}

ir::IRType* IREmitExpr::emitErrorType() {
  return Converter_.emitErrorType();
}

ir::IRType* IREmitExpr::mapType(QualType T) {
  return Converter_.getTypeMapper().mapType(T);
}

bool IREmitExpr::isSignedType(QualType T) {
  if (T.isNull()) return true; // 默认有符号
  // 通过 Type 系统判断：BuiltinType::isSignedInteger()
  if (auto* BT = T->getAsBuiltinType()) {
    return BT->isSignedInteger();
  }
  return true; // 非整数类型默认有符号
}

ir::IRBasicBlock* IREmitExpr::createBasicBlock(ir::StringRef Name) {
  // 需要当前 IRFunction 来创建 BB
  // 通过 Builder 的 insert block 获取 parent function
  ir::IRBasicBlock* CurBB = getBuilder().getInsertBlock();
  if (!CurBB) return nullptr;
  // IRBasicBlock 由 IRFunction::addBasicBlock 创建
  // 需要 IRFunction 指针。通过 Converter 的 Module 获取当前函数
  // 暂时使用 insertBlock 的 parent
  // IRBasicBlock 有 getParent() → IRFunction*
  // 但当前 IRBasicBlock.h 未暴露 getParent()...
  // 替代方案：通过 Converter 直接操作 Module
  // TODO: 确认 IRBasicBlock 是否有 getParent()
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Emit - General Dispatch
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::Emit(const Expr* E) {
  if (!E) return nullptr;

  switch (E->getKind()) {
    // Literals
    case NodeKind::IntegerLiteralKind:
      return EmitIntegerLiteral(static_cast<const IntegerLiteral*>(E));
    case NodeKind::FloatingLiteralKind:
      return EmitFloatingLiteral(static_cast<const FloatingLiteral*>(E));
    case NodeKind::StringLiteralKind:
      return EmitStringLiteral(static_cast<StringLiteral*>(E));
    case NodeKind::CharacterLiteralKind:
      return EmitCharacterLiteral(static_cast<const CharacterLiteral*>(E));
    case NodeKind::CXXBoolLiteralKind:
      return EmitBoolLiteral(static_cast<const CXXBoolLiteral*>(E));
    case NodeKind::CXXNullPtrLiteralKind:
      return getBuilder().getNull(mapType(E->getType()));

    // Operators
    case NodeKind::BinaryOperatorKind:
      return EmitBinaryExpr(static_cast<const BinaryOperator*>(E));
    case NodeKind::UnaryOperatorKind:
      return EmitUnaryExpr(static_cast<const UnaryOperator*>(E));

    // References
    case NodeKind::DeclRefExprKind:
      return EmitDeclRefExpr(static_cast<const DeclRefExpr*>(E));
    case NodeKind::MemberExprKind:
      return EmitMemberExpr(static_cast<const MemberExpr*>(E));

    // Calls
    case NodeKind::CallExprKind:
      return EmitCallExpr(static_cast<const CallExpr*>(E));
    case NodeKind::CXXMemberCallExprKind:
      return EmitCXXMemberCallExpr(
          static_cast<const CXXMemberCallExpr*>(E));

    // Casts
    case NodeKind::CastExprKind:
    case NodeKind::CXXStaticCastExprKind:
    case NodeKind::CXXDynamicCastExprKind:
    case NodeKind::CXXConstCastExprKind:
    case NodeKind::CXXReinterpretCastExprKind:
    case NodeKind::CStyleCastExprKind:
      return EmitCastExpr(static_cast<const CastExpr*>(E));

    // C++ Specific
    case NodeKind::CXXConstructExprKind:
    case NodeKind::CXXTemporaryObjectExprKind:
      return EmitCXXConstructExpr(static_cast<const CXXConstructExpr*>(E));
    case NodeKind::CXXNewExprKind:
      return EmitCXXNewExpr(static_cast<const CXXNewExpr*>(E));
    case NodeKind::CXXDeleteExprKind:
      return EmitCXXDeleteExpr(static_cast<const CXXDeleteExpr*>(E));
    case NodeKind::CXXThisExprKind:
      return EmitCXXThisExpr(static_cast<const CXXThisExpr*>(E));

    // Conditional
    case NodeKind::ConditionalOperatorKind:
      return EmitConditionalOperator(
          static_cast<const ConditionalOperator*>(E));

    // Init list
    case NodeKind::InitListExprKind:
      return EmitInitListExpr(static_cast<const InitListExpr*>(E));

    default:
      // Unhandled expression type — error recovery
      return emitErrorPlaceholder(mapType(E->getType()));
  }
}

//===----------------------------------------------------------------------===//
// EmitBinaryExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitBinaryExpr(const BinaryOperator* BO) {
  if (!BO) return nullptr;

  BinaryOpKind Op = BO->getOpcode();

  // Short-circuit logical operators
  if (Op == BinaryOpKind::LAnd || Op == BinaryOpKind::LOr)
    return emitShortCircuitEval(BO);

  // Assignment operators
  if (Op == BinaryOpKind::Assign)
    return emitAssignment(BO);

  // Compound assignment operators
  if (Op == BinaryOpKind::AddAssign || Op == BinaryOpKind::SubAssign ||
      Op == BinaryOpKind::MulAssign || Op == BinaryOpKind::DivAssign ||
      Op == BinaryOpKind::RemAssign || Op == BinaryOpKind::ShlAssign ||
      Op == BinaryOpKind::ShrAssign || Op == BinaryOpKind::AndAssign ||
      Op == BinaryOpKind::OrAssign  || Op == BinaryOpKind::XorAssign)
    return emitCompoundAssignment(BO);

  // Comma operator
  if (Op == BinaryOpKind::Comma) {
    Emit(BO->getLHS()); // Evaluate LHS (discard result)
    return Emit(BO->getRHS()); // Return RHS result
  }

  // Standard binary operators: evaluate both operands
  ir::IRValue* LHS = Emit(BO->getLHS());
  ir::IRValue* RHS = Emit(BO->getRHS());
  if (!LHS || !RHS)
    return emitErrorPlaceholder(mapType(BO->getType()));

  ir::IRType* LHSTy = LHS->getType();

  switch (Op) {
    // Arithmetic
    case BinaryOpKind::Add:
      return getBuilder().createAdd(LHS, RHS);
    case BinaryOpKind::Sub:
      return getBuilder().createSub(LHS, RHS);
    case BinaryOpKind::Mul:
      return getBuilder().createMul(LHS, RHS);
    case BinaryOpKind::Div:
      if (LHSTy->isFloat())
        return emitErrorPlaceholder(LHSTy); // TODO: createFDiv
      return isSignedType(BO->getLHS()->getType())
                 ? getBuilder().createSDiv(LHS, RHS)
                 : getBuilder().createUDiv(LHS, RHS);
    case BinaryOpKind::Rem:
      if (LHSTy->isFloat())
        return emitErrorPlaceholder(LHSTy); // TODO: createFRem
      return isSignedType(BO->getLHS()->getType())
                 ? getBuilder().createSRem(LHS, RHS)
                 : getBuilder().createURem(LHS, RHS);

    // Shift
    case BinaryOpKind::Shl:
      return getBuilder().createShl(LHS, RHS);
    case BinaryOpKind::Shr:
      return isSignedType(BO->getLHS()->getType())
                 ? getBuilder().createAShr(LHS, RHS)
                 : getBuilder().createLShr(LHS, RHS);

    // Bitwise
    case BinaryOpKind::And:
      return getBuilder().createAnd(LHS, RHS);
    case BinaryOpKind::Or:
      return getBuilder().createOr(LHS, RHS);
    case BinaryOpKind::Xor:
      return getBuilder().createXor(LHS, RHS);

    // Comparison — use ICmpPred
    case BinaryOpKind::LT:
      return getBuilder().createICmp(
          isSignedType(BO->getLHS()->getType())
              ? ir::ICmpPred::SLT : ir::ICmpPred::ULT,
          LHS, RHS);
    case BinaryOpKind::GT:
      return getBuilder().createICmp(
          isSignedType(BO->getLHS()->getType())
              ? ir::ICmpPred::SGT : ir::ICmpPred::UGT,
          LHS, RHS);
    case BinaryOpKind::LE:
      return getBuilder().createICmp(
          isSignedType(BO->getLHS()->getType())
              ? ir::ICmpPred::SLE : ir::ICmpPred::ULE,
          LHS, RHS);
    case BinaryOpKind::GE:
      return getBuilder().createICmp(
          isSignedType(BO->getLHS()->getType())
              ? ir::ICmpPred::SGE : ir::ICmpPred::UGE,
          LHS, RHS);
    case BinaryOpKind::EQ:
      return getBuilder().createICmp(ir::ICmpPred::EQ, LHS, RHS);
    case BinaryOpKind::NE:
      return getBuilder().createICmp(ir::ICmpPred::NE, LHS, RHS);

    // Spaceship (C++20)
    case BinaryOpKind::Spaceship:
      // TODO: 三路比较，暂返回错误占位
      return emitErrorPlaceholder(mapType(BO->getType()));

    default:
      return emitErrorPlaceholder(mapType(BO->getType()));
  }
}

//===----------------------------------------------------------------------===//
// EmitUnaryExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitUnaryExpr(const UnaryOperator* UO) {
  if (!UO) return nullptr;

  ir::IRValue* SubVal = Emit(UO->getSubExpr());
  if (!SubVal)
    return emitErrorPlaceholder(mapType(UO->getType()));

  switch (UO->getOpcode()) {
    case UnaryOpKind::Plus:
      return SubVal; // Unary plus is a no-op

    case UnaryOpKind::Minus:
      return getBuilder().createNeg(SubVal);

    case UnaryOpKind::Not:
      return getBuilder().createNot(SubVal);

    case UnaryOpKind::LNot: {
      // Logical NOT: compare with zero
      ir::IRValue* Zero = getBuilder().getInt32(0);
      ir::IRType* SubTy = SubVal->getType();
      if (SubTy->isInteger()) {
        return getBuilder().createICmp(ir::ICmpPred::EQ, SubVal, Zero);
      }
      // For pointer/float: use different comparisons
      return emitErrorPlaceholder(
          Converter_.getTypeContext().getInt1Ty());
    }

    case UnaryOpKind::Deref:
      // Dereference returns an lvalue (pointer itself)
      return SubVal;

    case UnaryOpKind::AddrOf:
      // &expr — need the address. If expr is already an alloca/lvalue,
      // return it directly. Otherwise error.
      return SubVal; // SubVal should be the alloca address

    case UnaryOpKind::PreInc: {
      // ++x → x = x + 1
      ir::IRValue* One = getBuilder().getInt32(1);
      ir::IRValue* Result = getBuilder().createAdd(SubVal, One);
      getBuilder().createStore(Result, SubVal); // Store back
      return Result;
    }

    case UnaryOpKind::PreDec: {
      ir::IRValue* One = getBuilder().getInt32(1);
      ir::IRValue* Result = getBuilder().createSub(SubVal, One);
      getBuilder().createStore(Result, SubVal);
      return Result;
    }

    case UnaryOpKind::PostInc: {
      // x++ → save old, increment, return old
      ir::IRValue* OldVal = SubVal;
      ir::IRValue* One = getBuilder().getInt32(1);
      ir::IRValue* NewVal = getBuilder().createAdd(SubVal, One);
      getBuilder().createStore(NewVal, SubVal);
      return OldVal;
    }

    case UnaryOpKind::PostDec: {
      ir::IRValue* OldVal = SubVal;
      ir::IRValue* One = getBuilder().getInt32(1);
      ir::IRValue* NewVal = getBuilder().createSub(SubVal, One);
      getBuilder().createStore(NewVal, SubVal);
      return OldVal;
    }

    default:
      return emitErrorPlaceholder(mapType(UO->getType()));
  }
}

//===----------------------------------------------------------------------===//
// EmitCallExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCallExpr(const CallExpr* CE) {
  if (!CE) return nullptr;

  // Get callee expression — should be a DeclRefExpr referencing a FunctionDecl
  Expr* CalleeExpr = CE->getCallee();
  if (!CalleeExpr) return emitErrorPlaceholder(emitErrorType());

  // Resolve callee to FunctionDecl
  auto* DRE = llvm::dyn_cast<DeclRefExpr>(CalleeExpr);
  if (!DRE) {
    // Could be a MemberExpr (method call) or other
    // Try emitting callee as expression (for function pointers)
    ir::IRValue* CalleeVal = Emit(CalleeExpr);
    if (!CalleeVal) return emitErrorPlaceholder(emitErrorType());
    // TODO: Support indirect calls via function pointer
    return emitErrorPlaceholder(emitErrorType());
  }

  ValueDecl* VD = DRE->getDecl();
  auto* FD = llvm::dyn_cast<FunctionDecl>(VD);
  if (!FD) return emitErrorPlaceholder(emitErrorType());

  // Ensure the function has been emitted
  ir::IRFunction* IRFn = Converter_.getFunction(FD);
  if (!IRFn) {
    // Try to emit the function first
    IRFn = Converter_.emitFunction(FD);
  }
  if (!IRFn) return emitErrorPlaceholder(emitErrorType());

  // Emit arguments
  ir::SmallVector<ir::IRValue*, 8> IRArgs;
  for (Expr* Arg : CE->getArgs()) {
    ir::IRValue* ArgVal = Emit(Arg);
    if (!ArgVal) {
      ArgVal = emitErrorPlaceholder(emitErrorType());
    }
    IRArgs.push_back(ArgVal);
  }

  // Create call instruction
  // createCall returns IRInstruction* which IS IRValue*
  return getBuilder().createCall(IRFn, IRArgs);
}

//===----------------------------------------------------------------------===//
// EmitCXXMemberCallExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCXXMemberCallExpr(const CXXMemberCallExpr* MCE) {
  if (!MCE) return nullptr;

  // CXXMemberCallExpr inherits from CallExpr
  // The callee is a MemberExpr for the method
  // TODO: Full member call support requires 'this' pointer + vtable dispatch
  // For now, delegate to EmitCallExpr as a simplification
  return EmitCallExpr(static_cast<const CallExpr*>(MCE));
}

//===----------------------------------------------------------------------===//
// EmitMemberExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitMemberExpr(const MemberExpr* ME) {
  if (!ME) return nullptr;

  // Emit base expression (the object)
  ir::IRValue* BaseVal = Emit(ME->getBase());
  if (!BaseVal) return emitErrorPlaceholder(mapType(ME->getType()));

  // If arrow access (p->field), BaseVal is already a pointer to the object
  // If dot access (obj.field), BaseVal is the object's alloca address

  ValueDecl* Member = ME->getMemberDecl();
  if (!Member) return emitErrorPlaceholder(mapType(ME->getType()));

  // Look up member in DeclValues (if it's a field index mapping)
  ir::IRValue* MemberVal = Converter_.getDeclValue(Member);
  if (MemberVal) return MemberVal;

  // If not found, compute GEP offset
  // Need field index from the struct layout
  // For now: return GEP with index [0, fieldIndex]
  ir::IRType* BaseType = BaseVal->getType();
  if (BaseType->isPointer()) {
    auto* PtrTy = static_cast<ir::IRPointerType*>(BaseType);
    ir::IRType* PointeeTy = PtrTy->getPointeeType();
    if (PointeeTy->isStruct()) {
      // Compute field index (simplified: use decl index)
      // TODO: proper field offset computation
      ir::SmallVector<ir::IRValue*, 2> Indices;
      Indices.push_back(getBuilder().getInt32(0)); // dereference pointer
      Indices.push_back(getBuilder().getInt32(0)); // first field (TODO: actual index)
      return getBuilder().createGEP(PointeeTy, BaseVal, Indices);
    }
  }

  return emitErrorPlaceholder(mapType(ME->getType()));
}

//===----------------------------------------------------------------------===//
// EmitDeclRefExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitDeclRefExpr(const DeclRefExpr* DRE) {
  if (!DRE) return nullptr;

  ValueDecl* VD = DRE->getDecl();
  if (!VD) return emitErrorPlaceholder(emitErrorType());

  // 1. Check DeclValues map (local variables, parameters via alloca)
  ir::IRValue* Val = Converter_.getDeclValue(VD);
  if (Val) {
    // Val is the alloca address (lvalue). For rvalue use, caller must load.
    // But DeclRefExpr for a variable is an lvalue, so return the address.
    return Val;
  }

  // 2. Check if it's a VarDecl — could be a global variable
  if (auto* VarD = llvm::dyn_cast<VarDecl>(VD)) {
    ir::IRGlobalVariable* GV = Converter_.getGlobalVar(VarD);
    if (GV) {
      // IRGlobalVariable is NOT an IRValue subclass!
      // Use IRConstantGlobalRef wrapper
      ir::IRConstantGlobalRef* Ref =
          new ir::IRConstantGlobalRef(GV);
      // Note: IRConstantGlobalRef extends IRConstant extends IRValue
      // Return the address of the global (lvalue)
      return Ref;
    }
  }

  // 3. Check if it's a FunctionDecl — return function reference
  if (auto* FnD = llvm::dyn_cast<FunctionDecl>(VD)) {
    ir::IRFunction* IRFn = Converter_.getFunction(FnD);
    if (!IRFn) {
      IRFn = Converter_.emitFunction(FnD);
    }
    if (IRFn) {
      // Use IRConstantFunctionRef wrapper
      ir::IRConstantFunctionRef* Ref =
          new ir::IRConstantFunctionRef(IRFn);
      return Ref;
    }
  }

  return emitErrorPlaceholder(mapType(DRE->getType()));
}

//===----------------------------------------------------------------------===//
// EmitCastExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCastExpr(const CastExpr* CE) {
  if (!CE) return nullptr;

  ir::IRValue* SubVal = Emit(CE->getSubExpr());
  if (!SubVal) return emitErrorPlaceholder(mapType(CE->getType()));

  ir::IRType* DestTy = mapType(CE->getType());
  if (!DestTy) DestTy = emitErrorType();

  switch (CE->getCastKind()) {
    case CastKind::NoOp:
      return SubVal;

    case CastKind::LValueToRValue:
      // Load from the lvalue address
      return getBuilder().createLoad(SubVal->getType(), SubVal);

    case CastKind::IntegralCast: {
      ir::IRType* SrcTy = SubVal->getType();
      auto* SrcInt = static_cast<ir::IRIntegerType*>(SrcTy);
      auto* DstInt = static_cast<ir::IRIntegerType*>(DestTy);
      if (SrcInt->getBitWidth() < DstInt->getBitWidth()) {
        return getBuilder().createSExt(SubVal, DestTy); // 或 ZExt
      } else if (SrcInt->getBitWidth() > DstInt->getBitWidth()) {
        return getBuilder().createTrunc(SubVal, DestTy);
      }
      return SubVal;
    }

    case CastKind::FloatingCast:
      // TODO: FP trunc/extend
      return SubVal;

    case CastKind::IntegralToFloating:
      return isSignedType(CE->getSubExpr()->getType())
                 ? getBuilder().createSIToFP(SubVal, DestTy)
                 : getBuilder().createUIToFP(SubVal, DestTy);

    case CastKind::FloatingToIntegral:
      return isSignedType(CE->getType())
                 ? getBuilder().createFPToSI(SubVal, DestTy)
                 : getBuilder().createFPToUI(SubVal, DestTy);

    case CastKind::PointerToIntegral:
      return getBuilder().createPtrToInt(SubVal, DestTy);

    case CastKind::IntegralToPointer:
      return getBuilder().createIntToPtr(SubVal, DestTy);

    case CastKind::BitCast:
      return getBuilder().createBitCast(SubVal, DestTy);

    case CastKind::DerivedToBase:
    case CastKind::BaseToDerived:
      // Pointer adjustment — TODO: proper offset calculation
      return getBuilder().createBitCast(SubVal, DestTy);

    case CastKind::CStyle:
    case CastKind::CXXStatic:
    case CastKind::CXXDynamic:
    case CastKind::CXXConst:
    case CastKind::CXXReinterpret:
      // C++ casts — dispatch based on source/dest type
      if (SubVal->getType()->isPointer() && DestTy->isPointer())
        return getBuilder().createBitCast(SubVal, DestTy);
      if (SubVal->getType()->isInteger() && DestTy->isPointer())
        return getBuilder().createIntToPtr(SubVal, DestTy);
      if (SubVal->getType()->isPointer() && DestTy->isInteger())
        return getBuilder().createPtrToInt(SubVal, DestTy);
      return SubVal;

    default:
      return emitErrorPlaceholder(DestTy);
  }
}

//===----------------------------------------------------------------------===//
// EmitCXXConstructExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCXXConstructExpr(const CXXConstructExpr* CCE) {
  if (!CCE) return nullptr;

  // Allocate space for the object
  ir::IRType* ObjTy = mapType(CCE->getType());
  if (!ObjTy) ObjTy = emitErrorType();

  ir::IRValue* Alloca = getBuilder().createAlloca(ObjTy, "ctor.tmp");

  // Emit constructor arguments
  ir::SmallVector<ir::IRValue*, 4> Args;
  Args.push_back(Alloca); // 'this' pointer
  for (Expr* Arg : CCE->getArgs()) {
    ir::IRValue* ArgVal = Emit(Arg);
    if (!ArgVal) ArgVal = emitErrorPlaceholder(emitErrorType());
    Args.push_back(ArgVal);
  }

  // Call constructor if available
  CXXConstructorDecl* Ctor = CCE->getConstructor();
  if (Ctor) {
    ir::IRFunction* CtorFn = Converter_.getFunction(Ctor);
    if (CtorFn) {
      getBuilder().createCall(CtorFn, Args);
    }
  }

  return Alloca;
}

//===----------------------------------------------------------------------===//
// EmitCXXNewExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCXXNewExpr(const CXXNewExpr* NE) {
  if (!NE) return nullptr;

  ir::IRType* AllocTy = mapType(NE->getAllocatedType());
  if (!AllocTy) AllocTy = emitErrorType();

  if (NE->isArray()) {
    // new T[n] → allocate array
    ir::IRValue* Count = Emit(NE->getArraySize());
    // TODO: runtime allocation (malloc/call allocator)
    return getBuilder().createAlloca(AllocTy, "new.array");
  }

  // new T → allocate single object
  ir::IRValue* Mem = getBuilder().createAlloca(AllocTy, "new.obj");

  // Emit initializer if present
  if (Expr* Init = NE->getInitializer()) {
    Emit(Init); // Should store into Mem
  }

  return Mem;
}

//===----------------------------------------------------------------------===//
// EmitCXXDeleteExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCXXDeleteExpr(const CXXDeleteExpr* DE) {
  if (!DE) return nullptr;

  // Emit destructor call if applicable
  ir::IRValue* Arg = Emit(DE->getArgument());
  // TODO: call destructor, call deallocator

  // delete returns void
  return nullptr;
}

//===----------------------------------------------------------------------===//
// EmitCXXThisExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCXXThisExpr(const CXXThisExpr* TE) {
  // 'this' pointer is stored as a special DeclValue
  // Look it up from the converter
  return Converter_.getDeclValue(nullptr); // TODO: special 'this' handling
}

//===----------------------------------------------------------------------===//
// EmitConditionalOperator
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitConditionalOperator(const ConditionalOperator* CO) {
  if (!CO) return nullptr;

  // Emit condition
  ir::IRValue* Cond = Emit(CO->getCond());
  if (!Cond) return emitErrorPlaceholder(mapType(CO->getType()));

  // Create basic blocks for true/false/merge
  // NOTE: 需要 IRFunction::addBasicBlock() 来创建新 BB
  // 当前 createBasicBlock() 需要 IRFunction 指针
  // 暂用 select 指令作为简化实现（无短路）
  ir::IRValue* TrueVal = Emit(CO->getTrueExpr());
  ir::IRValue* FalseVal = Emit(CO->getFalseExpr());
  if (!TrueVal || !FalseVal)
    return emitErrorPlaceholder(mapType(CO->getType()));

  // 使用 select 指令（简化版，不生成分支）
  return getBuilder().createSelect(Cond, TrueVal, FalseVal, "ternary");

  // TODO: 完整实现应生成 CondBr + Phi:
  // 1. createCondBr(Cond, TrueBB, FalseBB)
  // 2. In TrueBB: emit TrueExpr, createBr(MergeBB)
  // 3. In FalseBB: emit FalseExpr, createBr(MergeBB)
  // 4. In MergeBB: createPhi with TrueVal/FalseVal
}

//===----------------------------------------------------------------------===//
// EmitInitListExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitInitListExpr(const InitListExpr* ILE) {
  if (!ILE) return nullptr;

  ir::IRType* AggTy = mapType(ILE->getType());
  if (!AggTy) AggTy = emitErrorType();

  // Create alloca for the aggregate
  ir::IRValue* AggAddr = getBuilder().createAlloca(AggTy, "initlist");

  // Emit each initializer and store into the aggregate
  unsigned Idx = 0;
  for (Expr* Init : ILE->getInits()) {
    ir::IRValue* InitVal = Emit(Init);
    if (!InitVal) InitVal = emitErrorPlaceholder(emitErrorType());

    // Compute GEP to get field address
    ir::SmallVector<ir::IRValue*, 2> Indices;
    Indices.push_back(getBuilder().getInt32(0));
    Indices.push_back(getBuilder().getInt32(Idx));
    ir::IRValue* FieldAddr = getBuilder().createGEP(
        AggTy, AggAddr, Indices, "init.field");

    // Store value
    getBuilder().createStore(InitVal, FieldAddr);
    ++Idx;
  }

  return AggAddr;
}

//===----------------------------------------------------------------------===//
// EmitStringLiteral
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitStringLiteral(const StringLiteral* SL) {
  if (!SL) return nullptr;

  // Create a global constant for the string
  // TODO: 通过 Module 创建全局字符串常量
  // 简化方案：返回 null placeholder
  return emitErrorPlaceholder(mapType(SL->getType()));
}

//===----------------------------------------------------------------------===//
// EmitIntegerLiteral
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitIntegerLiteral(const IntegerLiteral* IL) {
  if (!IL) return nullptr;

  ir::IRType* Ty = mapType(IL->getType());
  if (!Ty) Ty = Converter_.getTypeContext().getInt32Ty();

  // Create IRConstantInt from APInt
  auto* IntTy = static_cast<ir::IRIntegerType*>(Ty);
  auto* C = new ir::IRConstantInt(IntTy, IL->getValue());
  return C;
}

//===----------------------------------------------------------------------===//
// EmitFloatingLiteral
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitFloatingLiteral(const FloatingLiteral* FL) {
  if (!FL) return nullptr;

  ir::IRType* Ty = mapType(FL->getType());
  if (!Ty) Ty = Converter_.getTypeContext().getDoubleTy();

  auto* FloatTy = static_cast<ir::IRFloatType*>(Ty);
  auto* C = new ir::IRConstantFP(FloatTy, FL->getValue());
  return C;
}

//===----------------------------------------------------------------------===//
// EmitCharacterLiteral
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCharacterLiteral(const CharacterLiteral* CL) {
  if (!CL) return nullptr;

  ir::IRType* Ty = mapType(CL->getType());
  if (!Ty) Ty = Converter_.getTypeContext().getInt32Ty();

  auto* IntTy = static_cast<ir::IRIntegerType*>(Ty);
  auto* C = new ir::IRConstantInt(IntTy, CL->getValue());
  return C;
}

//===----------------------------------------------------------------------===//
// EmitBoolLiteral
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitBoolLiteral(const CXXBoolLiteral* BLE) {
  if (!BLE) return nullptr;

  return getBuilder().getInt1(BLE->getValue());
}

//===----------------------------------------------------------------------===//
// Short-Circuit Evaluation (&& / ||)
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::emitShortCircuitEval(const BinaryOperator* BO) {
  if (!BO) return nullptr;

  // Simplified implementation using Select instruction
  // TODO: Full implementation with CondBr for proper short-circuit
  ir::IRValue* LHS = Emit(BO->getLHS());
  ir::IRValue* RHS = Emit(BO->getRHS());
  if (!LHS || !RHS)
    return emitErrorPlaceholder(Converter_.getTypeContext().getInt1Ty());

  if (BO->getOpcode() == BinaryOpKind::LAnd) {
    // LHS && RHS → select(LHS, RHS, false)
    ir::IRValue* FalseVal = getBuilder().getInt1(false);
    return getBuilder().createSelect(LHS, RHS, FalseVal, "and");
  } else {
    // LHS || RHS → select(LHS, true, RHS)
    ir::IRValue* TrueVal = getBuilder().getInt1(true);
    return getBuilder().createSelect(LHS, TrueVal, RHS, "or");
  }
}

//===----------------------------------------------------------------------===//
// emitAssignment
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::emitAssignment(const BinaryOperator* BO) {
  if (!BO) return nullptr;

  // LHS is an lvalue (DeclRefExpr/MemberExpr → alloca address)
  ir::IRValue* LHSPtr = Emit(BO->getLHS());
  ir::IRValue* RHSVal = Emit(BO->getRHS());
  if (!LHSPtr || !RHSVal)
    return emitErrorPlaceholder(mapType(BO->getType()));

  // Store RHS into LHS address
  getBuilder().createStore(RHSVal, LHSPtr);

  // Assignment returns the assigned value (rvalue)
  return RHSVal;
}

//===----------------------------------------------------------------------===//
// emitCompoundAssignment
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::emitCompoundAssignment(const BinaryOperator* BO) {
  if (!BO) return nullptr;

  // Get LHS address (lvalue)
  ir::IRValue* LHSPtr = Emit(BO->getLHS());

  // Load current LHS value
  ir::IRValue* LHSVal = getBuilder().createLoad(
      LHSPtr->getType()->isPointer()
          ? static_cast<ir::IRPointerType*>(LHSPtr->getType())->getPointeeType()
          : LHSPtr->getType(),
      LHSPtr, "lhs.load");

  // Compute RHS
  ir::IRValue* RHSVal = Emit(BO->getRHS());
  if (!LHSVal || !RHSVal)
    return emitErrorPlaceholder(mapType(BO->getType()));

  // Compute result based on compound op
  ir::IRValue* Result = nullptr;
  switch (BO->getOpcode()) {
    case BinaryOpKind::AddAssign:  Result = getBuilder().createAdd(LHSVal, RHSVal); break;
    case BinaryOpKind::SubAssign:  Result = getBuilder().createSub(LHSVal, RHSVal); break;
    case BinaryOpKind::MulAssign:  Result = getBuilder().createMul(LHSVal, RHSVal); break;
    case BinaryOpKind::DivAssign:
      Result = isSignedType(BO->getLHS()->getType())
                   ? getBuilder().createSDiv(LHSVal, RHSVal)
                   : getBuilder().createUDiv(LHSVal, RHSVal);
      break;
    case BinaryOpKind::RemAssign:
      Result = isSignedType(BO->getLHS()->getType())
                   ? getBuilder().createSRem(LHSVal, RHSVal)
                   : getBuilder().createURem(LHSVal, RHSVal);
      break;
    case BinaryOpKind::ShlAssign:  Result = getBuilder().createShl(LHSVal, RHSVal); break;
    case BinaryOpKind::ShrAssign:
      Result = isSignedType(BO->getLHS()->getType())
                   ? getBuilder().createAShr(LHSVal, RHSVal)
                   : getBuilder().createLShr(LHSVal, RHSVal);
      break;
    case BinaryOpKind::AndAssign:  Result = getBuilder().createAnd(LHSVal, RHSVal); break;
    case BinaryOpKind::OrAssign:   Result = getBuilder().createOr(LHSVal, RHSVal); break;
    case BinaryOpKind::XorAssign:  Result = getBuilder().createXor(LHSVal, RHSVal); break;
    default: return emitErrorPlaceholder(mapType(BO->getType()));
  }

  // Store result back
  getBuilder().createStore(Result, LHSPtr);

  return Result;
}

} // namespace frontend
} // namespace blocktype
```

---

## Part 4: 测试文件

### `tests/unit/Frontend/IREmitExprTest.cpp`（GTest 格式）

```cpp
//===--- IREmitExprTest.cpp - IREmitExpr Unit Tests -----------------------===//

#include <gtest/gtest.h>

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/Frontend/IREmitExpr.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRConversionResult.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype;
using namespace blocktype::frontend;

namespace {

/// Test fixture providing a reusable test environment
class IREmitExprTest : public ::testing::Test {
protected:
  ir::IRContext IRCtx;
  ir::IRTypeContext& TypeCtx;
  std::unique_ptr<ir::TargetLayout> Layout;
  DiagnosticsEngine Diags;

  IREmitExprTest()
    : TypeCtx(IRCtx.getTypeContext()),
      Layout(ir::TargetLayout::Create("x86_64-unknown-linux-gnu")),
      Diags() {}
};

} // anonymous namespace

// === Test 1: Integer literal emission ===
TEST_F(IREmitExprTest, EmitIntegerLiteral) {
  SourceLocation Loc;

  // Create: 42
  llvm::APInt Val(32, 42);
  IntegerLiteral IL(Loc, Val, QualType());

  // Create converter and expression emitter
  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);

  // Create a TU and convert to set up the converter state
  TranslationUnitDecl TU(Loc);
  auto Result = Converter.convert(&TU);
  ASSERT_TRUE(Result.isUsable());

  IREmitExpr ExprEmitter(Converter);

  auto* Result2 = ExprEmitter.Emit(&IL);
  ASSERT_NE(Result2, nullptr);

  auto* CI = dyn_cast<ir::IRConstantInt>(Result2);
  ASSERT_NE(CI, nullptr);
  EXPECT_EQ(CI->getZExtValue(), 42u);
}

// === Test 2: Boolean literal emission ===
TEST_F(IREmitExprTest, EmitBoolLiteral) {
  SourceLocation Loc;

  CXXBoolLiteral TrueLit(Loc, true, QualType());
  CXXBoolLiteral FalseLit(Loc, false, QualType());

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);

  auto* TrueResult = ExprEmitter.Emit(&TrueLit);
  ASSERT_NE(TrueResult, nullptr);

  auto* FalseResult = ExprEmitter.Emit(&FalseLit);
  ASSERT_NE(FalseResult, nullptr);
}

// === Test 3: Binary operator — integer addition ===
TEST_F(IREmitExprTest, EmitBinaryAdd) {
  SourceLocation Loc;

  // Create: a + b
  QualType IntTy; // Simplified
  llvm::APInt AVal(32, 10);
  llvm::APInt BVal(32, 20);
  IntegerLiteral A(Loc, AVal, IntTy);
  IntegerLiteral B(Loc, BVal, IntTy);
  BinaryOperator AddBO(Loc, &A, &B, BinaryOpKind::Add);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);

  auto* Result = ExprEmitter.EmitBinaryExpr(&AddBO);
  ASSERT_NE(Result, nullptr);
  // Result should be an IRInstruction with Opcode::Add
  EXPECT_NE(Result, nullptr);
}

// === Test 4: Comparison operators ===
TEST_F(IREmitExprTest, EmitComparison) {
  SourceLocation Loc;

  QualType IntTy;
  llvm::APInt AVal(32, 5);
  llvm::APInt BVal(32, 10);
  IntegerLiteral A(Loc, AVal, IntTy);
  IntegerLiteral B(Loc, BVal, IntTy);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);

  // Test LT
  BinaryOperator LTOp(Loc, &A, &B, BinaryOpKind::LT);
  auto* LTResult = ExprEmitter.EmitBinaryExpr(&LTOp);
  ASSERT_NE(LTResult, nullptr);

  // Test EQ
  BinaryOperator EQOp(Loc, &A, &B, BinaryOpKind::EQ);
  auto* EQResult = ExprEmitter.EmitBinaryExpr(&EQOp);
  ASSERT_NE(EQResult, nullptr);
}

// === Test 5: Unary negation ===
TEST_F(IREmitExprTest, EmitUnaryNeg) {
  SourceLocation Loc;

  QualType IntTy;
  llvm::APInt Val(32, 42);
  IntegerLiteral IL(Loc, Val, IntTy);
  UnaryOperator NegOp(Loc, &IL, UnaryOpKind::Minus);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);

  auto* Result = ExprEmitter.EmitUnaryExpr(&NegOp);
  ASSERT_NE(Result, nullptr);
}

// === Test 6: Character literal ===
TEST_F(IREmitExprTest, EmitCharacterLiteral) {
  SourceLocation Loc;

  CharacterLiteral CL(Loc, 'A', QualType());

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);

  auto* Result = ExprEmitter.EmitCharacterLiteral(&CL);
  ASSERT_NE(Result, nullptr);

  auto* CI = dyn_cast<ir::IRConstantInt>(Result);
  ASSERT_NE(CI, nullptr);
  EXPECT_EQ(CI->getZExtValue(), 65u); // 'A' = 65
}

// === Test 7: Comma operator ===
TEST_F(IREmitExprTest, EmitCommaOperator) {
  SourceLocation Loc;

  QualType IntTy;
  llvm::APInt AVal(32, 1);
  llvm::APInt BVal(32, 2);
  IntegerLiteral A(Loc, AVal, IntTy);
  IntegerLiteral B(Loc, BVal, IntTy);
  BinaryOperator CommaOp(Loc, &A, &B, BinaryOpKind::Comma);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);

  auto* Result = ExprEmitter.EmitBinaryExpr(&CommaOp);
  ASSERT_NE(Result, nullptr);
  // Comma should return RHS value (2)
  auto* CI = dyn_cast<ir::IRConstantInt>(Result);
  ASSERT_NE(CI, nullptr);
  EXPECT_EQ(CI->getZExtValue(), 2u);
}

// === Test 8: General Emit dispatch ===
TEST_F(IREmitExprTest, EmitDispatch) {
  SourceLocation Loc;

  llvm::APInt Val(32, 100);
  IntegerLiteral IL(Loc, Val, QualType());

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);

  auto* Result = ExprEmitter.Emit(&IL);
  ASSERT_NE(Result, nullptr);

  auto* CI = dyn_cast<ir::IRConstantInt>(Result);
  ASSERT_NE(CI, nullptr);
  EXPECT_EQ(CI->getZExtValue(), 100u);
}

// === Test 9: Null expression → returns nullptr ===
TEST_F(IREmitExprTest, EmitNullExpr) {
  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(SourceLocation());
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);

  auto* Result = ExprEmitter.Emit(nullptr);
  EXPECT_EQ(Result, nullptr);
}

// === Test 10: IRBuilder extended API ===
TEST_F(IREmitExprTest, IRBuilderExtendedAPI) {
  // Verify new IRBuilder methods exist and work
  ir::IRContext C;
  ir::IRTypeContext& TC = C.getTypeContext();
  ir::IRBuilder Builder(C);

  auto* Int32Ty = TC.getInt32Ty();

  // We need a basic block to insert into
  ir::IRModule Mod("test", TC);
  ir::IRFunctionType* FnTy = TC.getFunctionType(Int32Ty, {});
  ir::IRFunction* Fn = Mod.getOrInsertFunction("test_fn", FnTy);
  ir::IRBasicBlock* BB = Fn->addBasicBlock("entry");
  Builder.setInsertPoint(BB);

  auto* V1 = Builder.getInt32(10);
  auto* V2 = Builder.getInt32(3);

  auto* SDivResult = Builder.createSDiv(V1, V2);
  ASSERT_NE(SDivResult, nullptr);

  auto* UDivResult = Builder.createUDiv(V1, V2);
  ASSERT_NE(UDivResult, nullptr);

  auto* SRemResult = Builder.createSRem(V1, V2);
  ASSERT_NE(SRemResult, nullptr);

  auto* AndResult = Builder.createAnd(V1, V2);
  ASSERT_NE(AndResult, nullptr);

  auto* OrResult = Builder.createOr(V1, V2);
  ASSERT_NE(OrResult, nullptr);

  auto* XorResult = Builder.createXor(V1, V2);
  ASSERT_NE(XorResult, nullptr);

  auto* ShlResult = Builder.createShl(V1, V2);
  ASSERT_NE(ShlResult, nullptr);

  auto* AShrResult = Builder.createAShr(V1, V2);
  ASSERT_NE(AShrResult, nullptr);

  auto* LShrResult = Builder.createLShr(V1, V2);
  ASSERT_NE(LShrResult, nullptr);
}
```

---

## Part 5: BinaryOpKind → IR 指令映射表（修正版）

| BinaryOpKind | IR 操作 | 签名处理 | 备注 |
|-------------|---------|---------|------|
| `Add` | `createAdd` | — | 整数加法 |
| `Sub` | `createSub` | — | 整数减法 |
| `Mul` | `createMul` | — | 整数乘法 |
| `Div` | `createSDiv` / `createUDiv` | `isSignedType()` 决定 | 有/无符号除法 |
| `Rem` | `createSRem` / `createURem` | `isSignedType()` 决定 | 有/无符号取余 |
| `Shl` | `createShl` | — | 左移 |
| `Shr` | `createAShr` / `createLShr` | `isSignedType()` 决定 | 算术/逻辑右移 |
| `And` | `createAnd` | — | 按位与 |
| `Or` | `createOr` | — | 按位或 |
| `Xor` | `createXor` | — | 按位异或 |
| `LT` | `createICmp(SLT/ULT)` | `isSignedType()` 决定 | 小于 |
| `GT` | `createICmp(SGT/UGT)` | `isSignedType()` 决定 | 大于 |
| `LE` | `createICmp(SLE/ULE)` | `isSignedType()` 决定 | 小于等于 |
| `GE` | `createICmp(SGE/UGE)` | `isSignedType()` 决定 | 大于等于 |
| `EQ` | `createICmp(EQ)` | — | 等于 |
| `NE` | `createICmp(NE)` | — | 不等于 |
| `LAnd` | Select 指令（简化版） | — | 逻辑与，TODO: CondBr |
| `LOr` | Select 指令（简化版） | — | 逻辑或，TODO: CondBr |
| `Assign` | `createStore` | — | 赋值 = Store |
| `Comma` | 评估 LHS，返回 RHS | — | 逗号表达式 |
| `Spaceship` | TODO（错误占位） | — | C++20 三路比较 |

---

## Part 6: 验收标准映射

| 验收标准 | 对应测试 | 状态 |
|---------|---------|------|
| V1: 整数加法 `int x = a + b;` | `EmitBinaryAdd` | 覆盖 |
| V2: 函数调用 `foo(1, 2)` | `EmitCallExpr` (需 FunctionDecl stub) | 部分（需完整函数调用链） |
| V3: 成员访问 `obj.field` | `EmitMemberExpr` (需 StructType stub) | 部分（简化 GEP） |
| V4: 整数字面量 `42` | `EmitIntegerLiteral` | 完整覆盖 |
| V5: 布尔字面量 `true/false` | `EmitBoolLiteral` | 完整覆盖 |
| V6: 字符字面量 `'A'` | `EmitCharacterLiteral` | 完整覆盖 |
| V7: 比较运算 `a < b` | `EmitComparison` | 完整覆盖 |
| V8: 一元取负 `-x` | `EmitUnaryNeg` | 完整覆盖 |
| V9: 逗号表达式 `(a, b)` → 返回 RHS | `EmitCommaOperator` | 完整覆盖 |
| V10: IRBuilder 扩展 API | `IRBuilderExtendedAPI` | 完整覆盖 |

---

## Part 7: dev-tester 执行步骤

### 步骤 1: 前置修改（IR 层）

1. **修改 `include/blocktype/IR/IRFunction.h`**
   - 将 `IRArgument` 改为继承 `IRValue`（使用 `ValueKind::Argument`）
   - 移除重复的 `ParamType`/`Name` 字段（由 `IRValue` 基类提供）

2. **修改 `include/blocktype/IR/IRBuilder.h`**
   - 添加 16 个 `create*` 方法声明

3. **修改 `src/IR/IRBuilder.cpp`**
   - 实现 16 个 `create*` 方法

4. **修改 `include/blocktype/IR/IRConstant.h`**
   - 确认 `IRConstantGlobalRef` 和 `IRConstantFunctionRef` 构造函数为 public（已确认 ✅）

5. **修复 B.4 遗留的 `createStore(Arg, Alloca)` 编译问题**
   - `src/Frontend/ASTToIRConverter.cpp` 中 `emitFunction` 方法的 `Arg` 现在是 `IRValue*` 子类，`createStore` 调用可以编译

### 步骤 2: 替换 IREmitExpr 桩

6. **替换 `include/blocktype/Frontend/IREmitExpr.h`**
   - 用 Part 2 的完整版本替换桩文件

7. **创建 `src/Frontend/IREmitExpr.cpp`**
   - 用 Part 3 的完整实现

### 步骤 3: 修改构建

8. **修改 `src/Frontend/CMakeLists.txt`**
   - 添加 `IREmitExpr.cpp`

### 步骤 4: 创建测试

9. **创建 `tests/unit/Frontend/IREmitExprTest.cpp`**

### 步骤 5: 编译验证

```bash
cd /Users/yuan/Documents/BlockType && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target blocktype
```

### 步骤 6: 运行测试

```bash
# 如果使用 GTest：
./build/tests/unit/Frontend/IREmitExprTest
```

### 步骤 7: 红线 Checklist 自检

1. ✅ **架构优先** — `IREmitExpr` 通过 `ASTToIRConverter` 抽象访问 IR 层，不直接依赖具体 IR 实现细节
2. ✅ **多前端多后端自由组合** — `IREmitExpr` 是 `frontend` 命名空间的一部分，输出标准 IR，后端无关
3. ✅ **渐进式改造** — B.5 在 B.4 框架上构建，不修改现有 IR 层行为（仅扩展 IRBuilder + IRArgument 继承）
4. ✅ **现有功能不退化** — `IRBuilder` 新方法不影响现有 `createAdd/createSub` 等；`IRArgument` 改为继承 `IRValue` 是向后兼容的
5. ✅ **接口抽象优先** — `IREmitExpr` 通过 `ASTToIRConverter` 的 accessor 方法获取 Builder/Module/Context
6. ✅ **IR 中间层解耦** — `IREmitExpr` 只产出 `IRValue*`/`IRInstruction*`，不涉及后端

---

## 附录 A: AST Expr 类型验证结果

| 规格引用类型 | 实际类名 | 文件位置 | 状态 |
|------------|---------|---------|------|
| `BinaryOperator` | `BinaryOperator` | `Expr.h:516` | ✅ 匹配 |
| `UnaryOperator` | `UnaryOperator` | `Expr.h:558` | ✅ 匹配 |
| `CallExpr` | `CallExpr` | `Expr.h:712` | ✅ 匹配 |
| `MemberExpr` | `MemberExpr` | `Expr.h:440` | ✅ 匹配 |
| `DeclRefExpr` | `DeclRefExpr` | `Expr.h:318` | ✅ 匹配 |
| `CastExpr` | `CastExpr` | `Expr.h:1040` | ✅ 匹配 |
| `CXXConstructExpr` | `CXXConstructExpr` | `Expr.h:769` | ✅ 匹配 |
| `CXXMemberCallExpr` | `CXXMemberCallExpr` | `Expr.h:751` | ✅ 匹配 |
| `CXXNewExpr` | `CXXNewExpr` | `Expr.h:950` | ✅ 匹配 |
| `CXXDeleteExpr` | `CXXDeleteExpr` | `Expr.h:982` | ✅ 匹配 |
| `CXXThisExpr` | `CXXThisExpr` | `Expr.h:930` | ✅ 匹配 |
| `ConditionalOperator` | `ConditionalOperator` | `Expr.h:680` | ✅ 匹配 |
| `InitListExpr` | `InitListExpr` | `Expr.h:829` | ✅ 匹配 |
| `StringLiteral` | `StringLiteral` | `Expr.h:220` | ✅ 匹配 |
| `IntegerLiteral` | `IntegerLiteral` | `Expr.h:168` | ✅ 匹配 |
| `FloatingLiteral` | `FloatingLiteral` | `Expr.h:193` | ✅ 匹配 |
| `CharacterLiteral` | `CharacterLiteral` | `Expr.h:245` | ✅ 匹配 |
| `CXXBoolLiteralExpr` | **`CXXBoolLiteral`** | `Expr.h:271` | ⚠️ 名称不同 |

## 附录 B: BinaryOpKind 枚举值对照

| 规格 `BO_*` | 实际 `BinaryOpKind::*` | 值 | 状态 |
|------------|----------------------|---|------|
| `BO_Add` | `Add` | ✅ | 无 `BO_` 前缀 |
| `BO_Sub` | `Sub` | ✅ | 无 `BO_` 前缀 |
| `BO_Mul` | `Mul` | ✅ | 无 `BO_` 前缀 |
| `BO_Div` | `Div` | ✅ | 无 `BO_` 前缀 |
| `BO_Rem` | `Rem` | ✅ | 无 `BO_` 前缀 |
| `BO_Shl` | `Shl` | ✅ | 无 `BO_` 前缀 |
| `BO_Shr` | `Shr` | ✅ | 无 `BO_` 前缀 |
| `BO_And` | `And` | ✅ | 无 `BO_` 前缀 |
| `BO_Or` | `Or` | ✅ | 无 `BO_` 前缀 |
| `BO_Xor` | `Xor` | ✅ | 无 `BO_` 前缀 |
| `BO_LT` | `LT` | ✅ | 无 `BO_` 前缀 |
| `BO_GT` | `GT` | ✅ | 无 `BO_` 前缀 |
| `BO_LE` | `LE` | ✅ | 无 `BO_` 前缀 |
| `BO_GE` | `GE` | ✅ | 无 `BO_` 前缀 |
| `BO_EQ` | `EQ` | ✅ | 无 `BO_` 前缀 |
| `BO_NE` | `NE` | ✅ | 无 `BO_` 前缀 |
| `BO_LAnd` | `LAnd` | ✅ | 无 `BO_` 前缀 |
| `BO_LOr` | `LOr` | ✅ | 无 `BO_` 前缀 |
| `BO_Assign` | `Assign` | ✅ | 无 `BO_` 前缀 |
| `BO_Comma` | `Comma` | ✅ | 无 `BO_` 前缀 |

## 附录 C: IRValue 继承关系图（B.5 关键路径）

```
IRValue (基类)
├── IRConstant
│   ├── IRConstantInt        ← EmitIntegerLiteral 返回此类型
│   ├── IRConstantFP         ← EmitFloatingLiteral 返回此类型
│   ├── IRConstantNull
│   ├── IRConstantUndef      ← emitErrorPlaceholder 返回此类型
│   ├── IRConstantFunctionRef ← EmitDeclRefExpr(function) 用此包装
│   └── IRConstantGlobalRef   ← EmitDeclRefExpr(global var) 用此包装
├── User
│   └── IRInstruction        ← createAdd/createCall 等返回此类型
│       (是 IRValue 子类！)
└── IRArgument               ← 修改后继承 IRValue（B.5 前置修改）

非 IRValue 子类（需要包装）：
├── IRGlobalVariable         ← 用 IRConstantGlobalRef 包装
├── IRFunction               ← createCall 直接接受，函数引用用 IRConstantFunctionRef 包装
└── IRBasicBlock             ← createCondBr 直接接受
```
