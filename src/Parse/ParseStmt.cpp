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
#include "blocktype/AST/Stmt.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

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
    // TODO: Implement isDeclarationStatement()
    // For now, treat as expression statement
    Result = parseExpressionStatement();
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

  // TODO: Create ExpressionStmt
  // For now, return null
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Declaration statement parsing
//===----------------------------------------------------------------------===//

Stmt *Parser::parseDeclarationStatement() {
  // TODO: Implement declaration parsing
  // For now, just consume tokens until semicolon
  skipUntil({TokenKind::semicolon});
  tryConsumeToken(TokenKind::semicolon);
  return nullptr;
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

  // TODO: Create LabelStmt
  return SubStmt;
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

  // TODO: Create CaseStmt
  return SubStmt;
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

  // TODO: Create DefaultStmt
  return SubStmt;
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

  // TODO: Create BreakStmt
  return Context.create<NullStmt>(BreakLoc);
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

  // TODO: Create ContinueStmt
  return Context.create<NullStmt>(ContinueLoc);
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
  consumeToken(); // consume identifier

  if (!tryConsumeToken(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
  }

  // TODO: Create GotoStmt
  return Context.create<NullStmt>(GotoLoc);
}

} // namespace blocktype
