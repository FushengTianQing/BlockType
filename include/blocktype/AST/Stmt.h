//===--- Stmt.h - Statement AST Nodes -----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Stmt class and all statement AST nodes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/ASTNode.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {

class Expr;
class Decl;
class VarDecl;
class LabelDecl;

//===----------------------------------------------------------------------===//
// Stmt - Base class for all statements
//===----------------------------------------------------------------------===//

/// Stmt - Base class for all statement nodes.
class Stmt : public ASTNode {
protected:
  Stmt(SourceLocation Loc) : ASTNode(Loc) {}

public:
  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::NullStmtKind &&
           N->getKind() < NodeKind::NamedDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// Basic Statements
//===----------------------------------------------------------------------===//

/// NullStmt - Null statement (semicolon by itself).
class NullStmt : public Stmt {
public:
  NullStmt(SourceLocation Loc) : Stmt(Loc) {}

  NodeKind getKind() const override { return NodeKind::NullStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "NullStmt: ;\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::NullStmtKind;
  }
};

/// CompoundStmt - Compound statement (block).
class CompoundStmt : public Stmt {
  llvm::SmallVector<Stmt *, 8> Body;

public:
  CompoundStmt(SourceLocation Loc, llvm::ArrayRef<Stmt *> Body)
      : Stmt(Loc), Body(Body.begin(), Body.end()) {}

  llvm::ArrayRef<Stmt *> getBody() const { return Body; }

  NodeKind getKind() const override { return NodeKind::CompoundStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CompoundStmtKind;
  }
};

/// ReturnStmt - Return statement.
class ReturnStmt : public Stmt {
  Expr *RetValue;

public:
  ReturnStmt(SourceLocation Loc, Expr *RetValue)
      : Stmt(Loc), RetValue(RetValue) {}

  Expr *getRetValue() const { return RetValue; }

  NodeKind getKind() const override { return NodeKind::ReturnStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ReturnStmtKind;
  }
};

/// ExprStmt - Expression statement.
class ExprStmt : public Stmt {
  Expr *Expression;

public:
  ExprStmt(SourceLocation Loc, Expr *Expression)
      : Stmt(Loc), Expression(Expression) {}

  Expr *getExpr() const { return Expression; }

  NodeKind getKind() const override { return NodeKind::ExprStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ExprStmtKind;
  }
};

//===----------------------------------------------------------------------===//
// Declaration Statements
//===----------------------------------------------------------------------===//

/// DeclStmt - Declaration statement.
class DeclStmt : public Stmt {
  llvm::SmallVector<Decl *, 2> Decls;

public:
  DeclStmt(SourceLocation Loc, llvm::ArrayRef<Decl *> Decls)
      : Stmt(Loc), Decls(Decls.begin(), Decls.end()) {}

  llvm::ArrayRef<Decl *> getDecls() const { return Decls; }

  NodeKind getKind() const override { return NodeKind::DeclStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::DeclStmtKind;
  }
};

//===----------------------------------------------------------------------===//
// Control Flow Statements
//===----------------------------------------------------------------------===//

/// IfStmt - If statement.
class IfStmt : public Stmt {
  Expr *Cond;
  Stmt *Then;
  Stmt *Else;
  VarDecl *CondVar; // Optional condition variable
  bool IsConsteval : 1 = false;
  bool IsNegated : 1 = false; // for `if !consteval`

public:
  IfStmt(SourceLocation Loc, Expr *Cond, Stmt *Then, Stmt *Else,
         VarDecl *CondVar = nullptr, bool IsConsteval = false,
         bool IsNegated = false)
      : Stmt(Loc), Cond(Cond), Then(Then), Else(Else), CondVar(CondVar),
        IsConsteval(IsConsteval), IsNegated(IsNegated) {}

  Expr *getCond() const { return Cond; }
  Stmt *getThen() const { return Then; }
  Stmt *getElse() const { return Else; }
  VarDecl *getConditionVariable() const { return CondVar; }
  bool isConsteval() const { return IsConsteval; }
  bool isNegated() const { return IsNegated; }

  NodeKind getKind() const override { return NodeKind::IfStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::IfStmtKind;
  }
};

/// SwitchStmt - Switch statement.
class SwitchStmt : public Stmt {
  Expr *Cond;
  Stmt *Body;
  VarDecl *CondVar;

public:
  SwitchStmt(SourceLocation Loc, Expr *Cond, Stmt *Body,
             VarDecl *CondVar = nullptr)
      : Stmt(Loc), Cond(Cond), Body(Body), CondVar(CondVar) {}

  Expr *getCond() const { return Cond; }
  Stmt *getBody() const { return Body; }
  VarDecl *getConditionVariable() const { return CondVar; }

  NodeKind getKind() const override { return NodeKind::SwitchStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::SwitchStmtKind;
  }
};

/// CaseStmt - Case statement.
class CaseStmt : public Stmt {
  Expr *LHS;
  Expr *RHS; // For GNU case range extension
  Stmt *SubStmt;

public:
  CaseStmt(SourceLocation Loc, Expr *LHS, Expr *RHS, Stmt *SubStmt)
      : Stmt(Loc), LHS(LHS), RHS(RHS), SubStmt(SubStmt) {}

  Expr *getLHS() const { return LHS; }
  Expr *getRHS() const { return RHS; }
  Stmt *getSubStmt() const { return SubStmt; }

  NodeKind getKind() const override { return NodeKind::CaseStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CaseStmtKind;
  }
};

/// DefaultStmt - Default statement.
class DefaultStmt : public Stmt {
  Stmt *SubStmt;

public:
  DefaultStmt(SourceLocation Loc, Stmt *SubStmt)
      : Stmt(Loc), SubStmt(SubStmt) {}

  Stmt *getSubStmt() const { return SubStmt; }

  NodeKind getKind() const override { return NodeKind::DefaultStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::DefaultStmtKind;
  }
};

/// BreakStmt - Break statement.
class BreakStmt : public Stmt {
public:
  BreakStmt(SourceLocation Loc) : Stmt(Loc) {}

  NodeKind getKind() const override { return NodeKind::BreakStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "BreakStmt: break\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::BreakStmtKind;
  }
};

/// ContinueStmt - Continue statement.
class ContinueStmt : public Stmt {
public:
  ContinueStmt(SourceLocation Loc) : Stmt(Loc) {}

  NodeKind getKind() const override { return NodeKind::ContinueStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "ContinueStmt: continue\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ContinueStmtKind;
  }
};

/// GotoStmt - Goto statement.
class GotoStmt : public Stmt {
  LabelDecl *Label;

public:
  GotoStmt(SourceLocation Loc, LabelDecl *Label)
      : Stmt(Loc), Label(Label) {}

  LabelDecl *getLabel() const { return Label; }

  NodeKind getKind() const override { return NodeKind::GotoStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::GotoStmtKind;
  }
};

/// LabelStmt - Label statement.
class LabelStmt : public Stmt {
  LabelDecl *Label;
  Stmt *SubStmt;

public:
  LabelStmt(SourceLocation Loc, LabelDecl *Label, Stmt *SubStmt)
      : Stmt(Loc), Label(Label), SubStmt(SubStmt) {}

  LabelDecl *getLabel() const { return Label; }
  Stmt *getSubStmt() const { return SubStmt; }

  NodeKind getKind() const override { return NodeKind::LabelStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::LabelStmtKind;
  }
};

//===----------------------------------------------------------------------===//
// Loop Statements
//===----------------------------------------------------------------------===//

/// WhileStmt - While statement.
class WhileStmt : public Stmt {
  Expr *Cond;
  Stmt *Body;
  VarDecl *CondVar;

public:
  WhileStmt(SourceLocation Loc, Expr *Cond, Stmt *Body,
            VarDecl *CondVar = nullptr)
      : Stmt(Loc), Cond(Cond), Body(Body), CondVar(CondVar) {}

  Expr *getCond() const { return Cond; }
  Stmt *getBody() const { return Body; }
  VarDecl *getConditionVariable() const { return CondVar; }

  NodeKind getKind() const override { return NodeKind::WhileStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::WhileStmtKind;
  }
};

/// DoStmt - Do-while statement.
class DoStmt : public Stmt {
  Stmt *Body;
  Expr *Cond;

public:
  DoStmt(SourceLocation Loc, Stmt *Body, Expr *Cond)
      : Stmt(Loc), Body(Body), Cond(Cond) {}

  Stmt *getBody() const { return Body; }
  Expr *getCond() const { return Cond; }

  NodeKind getKind() const override { return NodeKind::DoStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::DoStmtKind;
  }
};

/// ForStmt - For statement.
class ForStmt : public Stmt {
  Stmt *Init;
  Expr *Cond;
  Expr *Inc;
  Stmt *Body;
  VarDecl *CondVar;

public:
  ForStmt(SourceLocation Loc, Stmt *Init, Expr *Cond, Expr *Inc, Stmt *Body,
          VarDecl *CondVar = nullptr)
      : Stmt(Loc), Init(Init), Cond(Cond), Inc(Inc), Body(Body),
        CondVar(CondVar) {}

  Stmt *getInit() const { return Init; }
  Expr *getCond() const { return Cond; }
  Expr *getInc() const { return Inc; }
  Stmt *getBody() const { return Body; }
  VarDecl *getConditionVariable() const { return CondVar; }

  NodeKind getKind() const override { return NodeKind::ForStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ForStmtKind;
  }
};

/// CXXForRangeStmt - C++11 range-based for statement.
class CXXForRangeStmt : public Stmt {
  VarDecl *LoopVar;
  Expr *Range;
  Stmt *Body;

public:
  CXXForRangeStmt(SourceLocation Loc, VarDecl *LoopVar, Expr *Range, Stmt *Body)
      : Stmt(Loc), LoopVar(LoopVar), Range(Range), Body(Body) {}

  VarDecl *getLoopVariable() const { return LoopVar; }
  Expr *getRangeInit() const { return Range; }
  Stmt *getBody() const { return Body; }

  NodeKind getKind() const override { return NodeKind::CXXForRangeStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXForRangeStmtKind;
  }
};

//===----------------------------------------------------------------------===//
// Exception Handling Statements
//===----------------------------------------------------------------------===//

/// CXXTryStmt - C++ try statement.
class CXXTryStmt : public Stmt {
  Stmt *TryBlock;
  llvm::SmallVector<Stmt *, 4> CatchBlocks;

public:
  CXXTryStmt(SourceLocation Loc, Stmt *TryBlock,
             llvm::ArrayRef<Stmt *> CatchBlocks)
      : Stmt(Loc), TryBlock(TryBlock),
        CatchBlocks(CatchBlocks.begin(), CatchBlocks.end()) {}

  Stmt *getTryBlock() const { return TryBlock; }
  llvm::ArrayRef<Stmt *> getCatchBlocks() const { return CatchBlocks; }

  NodeKind getKind() const override { return NodeKind::CXXTryStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXTryStmtKind;
  }
};

/// CXXCatchStmt - C++ catch statement.
class CXXCatchStmt : public Stmt {
  VarDecl *ExceptionDecl;
  Stmt *HandlerBlock;

public:
  CXXCatchStmt(SourceLocation Loc, VarDecl *ExceptionDecl, Stmt *HandlerBlock)
      : Stmt(Loc), ExceptionDecl(ExceptionDecl), HandlerBlock(HandlerBlock) {}

  VarDecl *getExceptionDecl() const { return ExceptionDecl; }
  Stmt *getHandlerBlock() const { return HandlerBlock; }

  NodeKind getKind() const override { return NodeKind::CXXCatchStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXCatchStmtKind;
  }
};

//===----------------------------------------------------------------------===//
// Coroutine Statements
//===----------------------------------------------------------------------===//

/// CoreturnStmt - Coreturn statement.
class CoreturnStmt : public Stmt {
  Expr *Operand;

public:
  CoreturnStmt(SourceLocation Loc, Expr *Operand)
      : Stmt(Loc), Operand(Operand) {}

  Expr *getOperand() const { return Operand; }

  NodeKind getKind() const override { return NodeKind::CoreturnStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CoreturnStmtKind;
  }
};

/// CoyieldStmt - Coyield statement.
class CoyieldStmt : public Stmt {
  Expr *Operand;

public:
  CoyieldStmt(SourceLocation Loc, Expr *Operand)
      : Stmt(Loc), Operand(Operand) {}

  Expr *getOperand() const { return Operand; }

  NodeKind getKind() const override { return NodeKind::CoyieldStmtKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CoyieldStmtKind;
  }
};

} // namespace blocktype
