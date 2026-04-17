//===--- ParseStmt.cpp - Statement Parsing ------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements statement parsing for the Parser.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Parse/Parser.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Stmt.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Declaration Detection
//===----------------------------------------------------------------------===//

/// isDeclarationStatement - Determine if the current token starts a declaration.
///
/// This function checks if the current token could be the start of a declaration.
/// Declaration keywords include: class, struct, union, enum, namespace, template,
/// typedef, using, static_assert, extern, and type names.
bool Parser::isDeclarationStatement() {
  // Check for declaration keywords
  switch (Tok.getKind()) {
  case TokenKind::kw_class:
  case TokenKind::kw_struct:
  case TokenKind::kw_union:
  case TokenKind::kw_enum:
  case TokenKind::kw_namespace:
  case TokenKind::kw_template:
  case TokenKind::kw_typedef:
  case TokenKind::kw_using:
  case TokenKind::kw_static_assert:
  case TokenKind::kw_extern:
  case TokenKind::kw_inline:
  case TokenKind::kw_constexpr:
  case TokenKind::kw_consteval:
  case TokenKind::kw_constinit:
    return true;

  // Check for type names (built-in types)
  case TokenKind::kw_void:
  case TokenKind::kw_bool:
  case TokenKind::kw_char:
  case TokenKind::kw_wchar_t:
  case TokenKind::kw_char8_t:
  case TokenKind::kw_char16_t:
  case TokenKind::kw_char32_t:
  case TokenKind::kw_int:
  case TokenKind::kw_short:
  case TokenKind::kw_long:
  case TokenKind::kw_signed:
  case TokenKind::kw_unsigned:
  case TokenKind::kw_float:
  case TokenKind::kw_double:
  case TokenKind::kw_auto:
    return true;

  // Check for identifier (could be a user-defined type)
  case TokenKind::identifier:
    // Look up the identifier in the symbol table
    // If it's a type name (TypeDecl), then this is a declaration statement
    if (CurrentScope) {
      NamedDecl *D = CurrentScope->lookup(Tok.getText());
      if (D && llvm::isa<TypeDecl>(D)) {
        // This identifier is a type name, so this is likely a declaration
        // For example: "MyType x;" or "MyType* p;"
        return true;
      }
    }
    // If not found or not a type, treat as expression statement
    return false;

  default:
    return false;
  }
}

//===----------------------------------------------------------------------===//
// Statement parsing entry point
//===----------------------------------------------------------------------===//

Stmt *Parser::parseStatement() {
  pushContext(ParsingContext::Statement);

  Stmt *Result = nullptr;

  switch (Tok.getKind()) {
  case TokenKind::l_brace:
    Result = parseCompoundStatement();
    break;

  case TokenKind::kw_return:
    Result = parseReturnStatement();
    break;

  case TokenKind::kw_break:
    Result = parseBreakStatement();
    break;

  case TokenKind::kw_continue:
    Result = parseContinueStatement();
    break;

  case TokenKind::kw_goto:
    Result = parseGotoStatement();
    break;

  case TokenKind::kw_if:
    Result = parseIfStatement();
    break;

  case TokenKind::kw_switch:
    Result = parseSwitchStatement();
    break;

  case TokenKind::kw_while:
    Result = parseWhileStatement();
    break;

  case TokenKind::kw_do:
    Result = parseDoStatement();
    break;

  case TokenKind::kw_for:
    Result = parseForStatement();
    break;

  case TokenKind::kw_try:
    Result = parseCXXTryStatement();
    break;

  case TokenKind::kw_co_return:
    Result = parseCoreturnStatement();
    break;

  case TokenKind::kw_co_yield:
    Result = parseCoyieldStatement();
    break;

  case TokenKind::kw_case:
    Result = parseCaseStatement();
    break;

  case TokenKind::kw_default:
    Result = parseDefaultStatement();
    break;

  case TokenKind::semicolon:
    Result = parseNullStatement();
    break;

  case TokenKind::identifier:
    // Check for label statement: identifier:
    if (NextTok.is(TokenKind::colon)) {
      Result = parseLabelStatement();
      break;
    }
    // Fall through to expression/declaration statement
    LLVM_FALLTHROUGH;

  default:
    // Check for declaration statement
    if (isDeclarationStatement()) {
      Result = parseDeclarationStatement();
    } else {
      // Treat as expression statement
      Result = parseExpressionStatement();
    }
    break;
  }

  popContext();
  return Result;
}

//===----------------------------------------------------------------------===//
// Compound statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseCompoundStatement() {
  SourceLocation LBraceLoc = Tok.getLocation();
  consumeToken(); // consume '{'

  llvm::SmallVector<Stmt *, 16> Body;

  // Parse statements until we hit '}'
  while (!Tok.is(TokenKind::r_brace) && !Tok.is(TokenKind::eof)) {
    Stmt *S = parseStatement();
    if (S) {
      Body.push_back(S);
    }
  }

  if (!tryConsumeToken(TokenKind::r_brace)) {
    emitError(DiagID::err_expected_rbrace);
  }

  return Context.create<CompoundStmt>(LBraceLoc, Body);
}

//===----------------------------------------------------------------------===//
// Return statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseReturnStatement() {
  SourceLocation ReturnLoc = Tok.getLocation();
  consumeToken(); // consume 'return'

  Expr *RetVal = nullptr;

  // Parse optional return value
  if (!Tok.is(TokenKind::semicolon)) {
    RetVal = parseExpression();
    if (!RetVal) {
      RetVal = createRecoveryExpr(ReturnLoc);
    }
  }

  if (!tryConsumeToken(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
  }

  return Context.create<ReturnStmt>(ReturnLoc, RetVal);
}

//===----------------------------------------------------------------------===//
// Null statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseNullStatement() {
  SourceLocation Loc = Tok.getLocation();
  consumeToken(); // consume ';'

  return Context.create<NullStmt>(Loc);
}

//===----------------------------------------------------------------------===//
// Expression statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseExpressionStatement() {
  SourceLocation Loc = Tok.getLocation();
  Expr *E = parseExpression();
  if (!E) {
    // Error recovery: skip to semicolon
    skipUntil({TokenKind::semicolon});
    tryConsumeToken(TokenKind::semicolon);
    return nullptr;
  }

  if (!tryConsumeToken(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
  }

  return Context.create<ExprStmt>(Loc, E);
}

//===----------------------------------------------------------------------===//
// Label statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseLabelStatement() {
  SourceLocation LabelLoc = Tok.getLocation();
  StringRef LabelName = Tok.getText();
  consumeToken(); // consume identifier

  consumeToken(); // consume ':'

  // Parse the sub-statement
  Stmt *SubStmt = parseStatement();
  if (!SubStmt) {
    SubStmt = Context.create<NullStmt>(LabelLoc);
  }

  // Create LabelDecl
  LabelDecl *Label = Context.create<LabelDecl>(LabelLoc, LabelName);

  return Context.create<LabelStmt>(LabelLoc, Label, SubStmt);
}

//===----------------------------------------------------------------------===//
// Case statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseCaseStatement() {
  SourceLocation CaseLoc = Tok.getLocation();
  consumeToken(); // consume 'case'

  // Parse the case value
  Expr *CaseVal = parseExpression();
  if (!CaseVal) {
    emitError(DiagID::err_expected_expression);
    CaseVal = createRecoveryExpr(CaseLoc);
  }

  if (!tryConsumeToken(TokenKind::colon)) {
    emitError(DiagID::err_expected);
  }

  // Parse the sub-statement
  Stmt *SubStmt = parseStatement();
  if (!SubStmt) {
    SubStmt = Context.create<NullStmt>(CaseLoc);
  }

  return Context.create<CaseStmt>(CaseLoc, CaseVal, nullptr, SubStmt);
}

//===----------------------------------------------------------------------===//
// Default statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseDefaultStatement() {
  SourceLocation DefaultLoc = Tok.getLocation();
  consumeToken(); // consume 'default'

  if (!tryConsumeToken(TokenKind::colon)) {
    emitError(DiagID::err_expected);
  }

  // Parse the sub-statement
  Stmt *SubStmt = parseStatement();
  if (!SubStmt) {
    SubStmt = Context.create<NullStmt>(DefaultLoc);
  }

  return Context.create<DefaultStmt>(DefaultLoc, SubStmt);
}

//===----------------------------------------------------------------------===//
// Break statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseBreakStatement() {
  SourceLocation BreakLoc = Tok.getLocation();
  consumeToken(); // consume 'break'

  if (!tryConsumeToken(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
  }

  return Context.create<BreakStmt>(BreakLoc);
}

//===----------------------------------------------------------------------===//
// Continue statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseContinueStatement() {
  SourceLocation ContinueLoc = Tok.getLocation();
  consumeToken(); // consume 'continue'

  if (!tryConsumeToken(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
  }

  return Context.create<ContinueStmt>(ContinueLoc);
}

//===----------------------------------------------------------------------===//
// Goto statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseGotoStatement() {
  SourceLocation GotoLoc = Tok.getLocation();
  consumeToken(); // consume 'goto'

  // Expect identifier
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    skipUntil({TokenKind::semicolon});
    tryConsumeToken(TokenKind::semicolon);
    return Context.create<NullStmt>(GotoLoc);
  }

  StringRef LabelName = Tok.getText();
  SourceLocation LabelLoc = Tok.getLocation();
  consumeToken(); // consume identifier

  if (!tryConsumeToken(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
  }

  // Create LabelDecl
  LabelDecl *Label = Context.create<LabelDecl>(LabelLoc, LabelName);

  return Context.create<GotoStmt>(GotoLoc, Label);
}

//===----------------------------------------------------------------------===//
// If statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseIfStatement() {
  SourceLocation IfLoc = Tok.getLocation();
  consumeToken(); // consume 'if'

  // Check for C++23 if consteval / if !consteval
  bool IsConsteval = false;
  bool IsNegated = false;
  if (Tok.is(TokenKind::kw_consteval)) {
    IsConsteval = true;
    consumeToken(); // consume 'consteval'
  } else if (Tok.is(TokenKind::exclaim) && NextTok.is(TokenKind::kw_consteval)) {
    IsConsteval = true;
    IsNegated = true;
    consumeToken(); // consume '!'
    consumeToken(); // consume 'consteval'
  }

  Expr *Cond = nullptr;
  if (!IsConsteval) {
    // Parse condition (only for regular if)
    if (!tryConsumeToken(TokenKind::l_paren)) {
      emitError(DiagID::err_expected_lparen);
      return Context.create<NullStmt>(IfLoc);
    }

    Cond = parseExpression();
    if (!Cond) {
      emitError(DiagID::err_expected_expression);
      Cond = createRecoveryExpr(IfLoc);
    }

    if (!tryConsumeToken(TokenKind::r_paren)) {
      emitError(DiagID::err_expected_rparen);
    }
  }

  // Parse then statement
  Stmt *ThenStmt = parseStatement();
  if (!ThenStmt) {
    ThenStmt = Context.create<NullStmt>(IfLoc);
  }

  // Parse optional else statement
  Stmt *ElseStmt = nullptr;
  if (Tok.is(TokenKind::kw_else)) {
    consumeToken(); // consume 'else'
    ElseStmt = parseStatement();
    if (!ElseStmt) {
      ElseStmt = Context.create<NullStmt>(IfLoc);
    }
  }

  return Context.create<IfStmt>(IfLoc, Cond, ThenStmt, ElseStmt, nullptr,
                                IsConsteval, IsNegated);
}

//===----------------------------------------------------------------------===//
// Switch statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseSwitchStatement() {
  SourceLocation SwitchLoc = Tok.getLocation();
  consumeToken(); // consume 'switch'

  // Parse condition
  if (!tryConsumeToken(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return Context.create<NullStmt>(SwitchLoc);
  }

  Expr *Cond = parseExpression();
  if (!Cond) {
    emitError(DiagID::err_expected_expression);
    Cond = createRecoveryExpr(SwitchLoc);
  }

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  // Parse body
  Stmt *Body = parseStatement();
  if (!Body) {
    Body = Context.create<NullStmt>(SwitchLoc);
  }

  return Context.create<SwitchStmt>(SwitchLoc, Cond, Body);
}

//===----------------------------------------------------------------------===//
// While statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseWhileStatement() {
  SourceLocation WhileLoc = Tok.getLocation();
  consumeToken(); // consume 'while'

  // Parse condition
  if (!tryConsumeToken(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return Context.create<NullStmt>(WhileLoc);
  }

  Expr *Cond = parseExpression();
  if (!Cond) {
    emitError(DiagID::err_expected_expression);
    Cond = createRecoveryExpr(WhileLoc);
  }

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  // Parse body
  Stmt *Body = parseStatement();
  if (!Body) {
    Body = Context.create<NullStmt>(WhileLoc);
  }

  return Context.create<WhileStmt>(WhileLoc, Cond, Body);
}

//===----------------------------------------------------------------------===//
// Do-while statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseDoStatement() {
  SourceLocation DoLoc = Tok.getLocation();
  consumeToken(); // consume 'do'

  // Parse body
  Stmt *Body = parseStatement();
  if (!Body) {
    Body = Context.create<NullStmt>(DoLoc);
  }

  // Parse 'while'
  if (!tryConsumeToken(TokenKind::kw_while)) {
    emitError(DiagID::err_expected);
    return Context.create<DoStmt>(DoLoc, Body, nullptr);
  }

  // Parse condition
  if (!tryConsumeToken(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return Context.create<DoStmt>(DoLoc, Body, nullptr);
  }

  Expr *Cond = parseExpression();
  if (!Cond) {
    Cond = createRecoveryExpr(DoLoc);
  }

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  if (!tryConsumeToken(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
  }

  return Context.create<DoStmt>(DoLoc, Body, Cond);
}

//===----------------------------------------------------------------------===//
// For statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseForStatement() {
  SourceLocation ForLoc = Tok.getLocation();
  consumeToken(); // consume 'for'

  if (!tryConsumeToken(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return Context.create<NullStmt>(ForLoc);
  }

  // Check for range-based for: for (decl : range)
  // Use tentative parsing to disambiguate between range-based and traditional for
  bool IsRangeBased = false;

  // Use tentative parsing to check if this is a range-based for
  // Save current state
  TentativeParsingAction TPA(*this);

  // Try to parse as a range-based for declaration
  // A range-based for has pattern: type? identifier ':'
  // Try to parse type (optional)
  if (Tok.is(TokenKind::kw_auto) || Tok.is(TokenKind::kw_const) ||
      Tok.is(TokenKind::kw_volatile) || Tok.is(TokenKind::kw_int) ||
      Tok.is(TokenKind::kw_float) || Tok.is(TokenKind::kw_double) ||
      Tok.is(TokenKind::kw_char) || Tok.is(TokenKind::kw_bool) ||
      Tok.is(TokenKind::kw_void) || Tok.is(TokenKind::kw_long) ||
      Tok.is(TokenKind::kw_short) || Tok.is(TokenKind::kw_signed) ||
      Tok.is(TokenKind::kw_unsigned) || Tok.is(TokenKind::identifier)) {
    // Try to parse type using DeclSpec + Declarator
    DeclSpec TmpDS;
    parseDeclSpecifierSeq(TmpDS);
    QualType Type;
    if (TmpDS.hasTypeSpecifier()) {
      Type = TmpDS.Type;
    }

    // Check for identifier
    if (Tok.is(TokenKind::identifier)) {
      consumeToken();

      // Check for ':' - this confirms it's a range-based for
      if (Tok.is(TokenKind::colon)) {
        IsRangeBased = true;
      }
    }
  }

  // Restore state
  TPA.abort();

  if (IsRangeBased) {
    // Parse range-based for: for (decl : range)
    // Parse full declaration with complete type specifier
    DeclSpec RangeDS;
    parseDeclSpecifierSeq(RangeDS);
    QualType VarType = RangeDS.Type;
    if (VarType.isNull()) {
      emitError(DiagID::err_expected_type);
      VarType = Context.getAutoType(); // Recovery
    }

    StringRef VarName;
    SourceLocation VarLoc;
    if (Tok.is(TokenKind::identifier)) {
      VarLoc = Tok.getLocation();
      VarName = Tok.getText();
      consumeToken();
    }

    // Consume ':'
    if (!tryConsumeToken(TokenKind::colon)) {
      emitError(DiagID::err_expected);
      return Context.create<NullStmt>(ForLoc);
    }

    // Parse range expression
    Expr *Range = parseExpression();
    if (!Range) {
      Range = createRecoveryExpr(ForLoc);
    }

    if (!tryConsumeToken(TokenKind::r_paren)) {
      emitError(DiagID::err_expected_rparen);
    }

    // Parse body
    Stmt *Body = parseStatement();
    if (!Body) {
      Body = Context.create<NullStmt>(ForLoc);
    }

    // Create range variable declaration
    VarDecl *RangeVar = Context.create<VarDecl>(VarLoc, VarName, VarType, nullptr);

    // Create CXXForRangeStmt
    return Context.create<CXXForRangeStmt>(ForLoc, RangeVar, Range, Body);
  }

  // Parse traditional for loop
  // Parse init (expression statement or declaration)
  Stmt *Init = nullptr;
  if (!Tok.is(TokenKind::semicolon)) {
    // Check if it's a declaration
    if (isDeclarationStatement()) {
      Init = parseDeclarationStatement();
    } else {
      // Parse as expression statement
      SourceLocation InitLoc = Tok.getLocation();
      Expr *InitExpr = parseExpression();
      if (InitExpr) {
        Init = Context.create<ExprStmt>(InitLoc, InitExpr);
      }
    }
  }
  if (!tryConsumeToken(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
  }

  // Parse condition
  Expr *Cond = nullptr;
  if (!Tok.is(TokenKind::semicolon)) {
    Cond = parseExpression();
  }
  if (!tryConsumeToken(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
  }

  // Parse increment
  Expr *Inc = nullptr;
  if (!Tok.is(TokenKind::r_paren)) {
    Inc = parseExpression();
  }

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  // Parse body
  Stmt *Body = parseStatement();
  if (!Body) {
    Body = Context.create<NullStmt>(ForLoc);
  }

  return Context.create<ForStmt>(ForLoc, Init, Cond, Inc, Body);
}

//===----------------------------------------------------------------------===//
// C++11 range-based for statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseCXXForRangeStatement() {
  SourceLocation ForLoc = Tok.getLocation();
  consumeToken(); // consume 'for'

  if (!tryConsumeToken(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return Context.create<NullStmt>(ForLoc);
  }

  // Parse range declaration
  QualType VarType;
  if (Tok.is(TokenKind::kw_auto)) {
    consumeToken();
    VarType = Context.getAutoType();
  } else {
    // Parse type using DeclSpec + Declarator
    DeclSpec DS;
    parseDeclSpecifierSeq(DS);
    if (DS.hasTypeSpecifier()) {
      Declarator D(DS, DeclaratorContext::ConditionContext);
      parseDeclarator(D);
      VarType = D.buildType(Context);
    }
  }

  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    skipUntil({TokenKind::r_paren});
    tryConsumeToken(TokenKind::r_paren);
    return Context.create<NullStmt>(ForLoc);
  }

  SourceLocation VarLoc = Tok.getLocation();
  StringRef VarName = Tok.getText();
  consumeToken(); // consume variable name

  // Expect ':'
  if (!tryConsumeToken(TokenKind::colon)) {
    emitError(DiagID::err_expected);
    skipUntil({TokenKind::r_paren});
    tryConsumeToken(TokenKind::r_paren);
    return Context.create<NullStmt>(ForLoc);
  }

  // Parse range expression
  Expr *Range = parseExpression();
  if (!Range) {
    Range = createRecoveryExpr(ForLoc);
  }

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  // Parse body
  Stmt *Body = parseStatement();
  if (!Body) {
    Body = Context.create<NullStmt>(ForLoc);
  }

  // Create range variable declaration
  VarDecl *RangeVar = Context.create<VarDecl>(VarLoc, VarName, VarType, nullptr);

  // Create CXXForRangeStmt
  return Context.create<CXXForRangeStmt>(ForLoc, RangeVar, Range, Body);
}

} // namespace blocktype
