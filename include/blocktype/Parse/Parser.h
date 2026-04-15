//===--- Parser.h - Parser Interface -----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Parser class which handles parsing of C++ code.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Lex/Token.h"
#include "blocktype/Parse/OperatorPrecedence.h"
#include "blocktype/Sema/Scope.h"
#include "llvm/ADT/SmallVector.h"
#include <initializer_list>
#include <vector>

namespace blocktype {

class TranslationUnitDecl;
class VarDecl;
class FunctionDecl;
class ParmVarDecl;
class Decl;
class RecordDecl;
class CXXRecordDecl;
class AccessSpecDecl;

/// LambdaCapture - Represents a lambda capture.
/// ParsingContext - Represents the current parsing context.
enum class ParsingContext {
  Expression,        // Parsing an expression
  Statement,         // Parsing a statement
  Declaration,       // Parsing a declaration
  MemberInitializer, // Parsing a member initializer
  TemplateArgument,  // Parsing a template argument
};

/// Parser - Parses C++ source code into an AST.
class Parser {
  Preprocessor &PP;
  ASTContext &Context;
  DiagnosticsEngine &Diags;

  // Current token lookahead
  Token Tok;      // Current token
  Token NextTok;  // Next token (for 1-token lookahead)

  // Parsing context stack
  std::vector<ParsingContext> ContextStack;

  // Scope management
  Scope *CurrentScope = nullptr;

  // Error recovery state
  unsigned ErrorCount = 0;
  bool HasRecoveryExpr = false;

public:
  Parser(Preprocessor &PP, ASTContext &Ctx);
  ~Parser();

  // Non-copyable
  Parser(const Parser &) = delete;
  Parser &operator=(const Parser &) = delete;

  //===--------------------------------------------------------------------===//
  // Main entry point
  //===--------------------------------------------------------------------===//

  /// Parses a translation unit.
  TranslationUnitDecl *parseTranslationUnit();

  //===--------------------------------------------------------------------===//
  // Token operations
  //===--------------------------------------------------------------------===//

  /// Consumes the current token and advances to the next.
  void consumeToken();

  /// Tries to consume the current token if it matches the given kind.
  /// Returns true if the token was consumed.
  bool tryConsumeToken(TokenKind K);

  /// Expects the current token to be of the given kind and consumes it.
  /// Emits an error if the token doesn't match.
  void expectAndConsume(TokenKind K, const char *Msg);

  /// Returns the next token without consuming it.
  const Token &peekToken() const { return NextTok; }

  /// Returns the current token.
  const Token &currentToken() const { return Tok; }

  /// Returns true if the current token matches the given kind.
  bool isToken(TokenKind K) const { return Tok.is(K); }

  /// Returns true if the next token matches the given kind.
  bool isNextToken(TokenKind K) const { return NextTok.is(K); }

  //===--------------------------------------------------------------------===//
  // Error recovery
  //===--------------------------------------------------------------------===//

  /// Skips tokens until one of the stop tokens is found.
  void skipUntil(std::initializer_list<TokenKind> StopTokens);

  /// Emits an error at the given location.
  void emitError(SourceLocation Loc, DiagID ID);

  /// Emits an error at the current token location.
  void emitError(DiagID ID);

  /// Creates a recovery expression for error recovery.
  Expr *createRecoveryExpr(SourceLocation Loc);

  /// Returns the number of errors encountered.
  unsigned getErrorCount() const { return ErrorCount; }

  /// Returns true if any errors have been encountered.
  bool hasErrors() const { return ErrorCount > 0 || Diags.hasErrorOccurred(); }

  //===--------------------------------------------------------------------===//
  // Parsing context
  //===--------------------------------------------------------------------===//

  /// Pushes a parsing context onto the stack.
  void pushContext(ParsingContext Ctx) { ContextStack.push_back(Ctx); }

  /// Pops a parsing context from the stack.
  void popContext() {
    if (!ContextStack.empty())
      ContextStack.pop_back();
  }

  /// Returns the current parsing context.
  ParsingContext getCurrentContext() const {
    return ContextStack.empty() ? ParsingContext::Expression
                                : ContextStack.back();
  }

  /// Returns true if we're in the given context.
  bool isInContext(ParsingContext Ctx) const {
    for (const auto &C : ContextStack) {
      if (C == Ctx)
        return true;
    }
    return false;
  }

  //===--------------------------------------------------------------------===//
  // Scope management
  //===--------------------------------------------------------------------===//

  /// Returns the current scope.
  Scope *getCurrentScope() const { return CurrentScope; }

  /// Pushes a new scope.
  void pushScope(ScopeFlags Flags = ScopeFlags::None);

  /// Pops the current scope.
  void popScope();

  //===--------------------------------------------------------------------===//
  // Accessors
  //===--------------------------------------------------------------------===//

  Preprocessor &getPreprocessor() { return PP; }
  ASTContext &getASTContext() { return Context; }
  DiagnosticsEngine &getDiagnostics() { return Diags; }

  //===--------------------------------------------------------------------===//
  // Expression parsing
  //===--------------------------------------------------------------------===//

  /// Parses an expression.
  Expr *parseExpression();

  /// Parses an assignment expression (stops at comma).
  /// Used for function call arguments.
  Expr *parseAssignmentExpression();

  /// Parses an expression with a minimum precedence level.
  Expr *parseExpressionWithPrecedence(PrecedenceLevel MinPrec);

  /// Parses the right-hand side of a binary expression using precedence climbing.
  Expr *parseRHS(Expr *LHS, PrecedenceLevel MinPrec);

  /// Parses a unary expression (prefix operators).
  Expr *parseUnaryExpression();

  /// Parses a postfix expression.
  Expr *parsePostfixExpression(Expr *Base);

  /// Parses a primary expression (literals, identifiers, parentheses).
  Expr *parsePrimaryExpression();

  /// Parses an integer literal.
  Expr *parseIntegerLiteral();

  /// Parses a floating point literal.
  Expr *parseFloatingLiteral();

  /// Parses a string literal.
  Expr *parseStringLiteral();

  /// Parses a character literal.
  Expr *parseCharacterLiteral();

  /// Parses a boolean literal (true/false).
  Expr *parseBoolLiteral();

  /// Parses a nullptr literal.
  Expr *parseNullPtrLiteral();

  /// Parses an identifier.
  Expr *parseIdentifier();

  /// Parses a parenthesized expression.
  Expr *parseParenExpression();

  /// Parses a conditional expression (?:).
  Expr *parseConditionalExpression(Expr *Cond);

  /// Parses a function call expression.
  Expr *parseCallExpression(Expr *Fn);

  /// Parses call arguments.
  llvm::SmallVector<Expr *, 8> parseCallArguments();

  //===--------------------------------------------------------------------===//
  // C++ expression parsing
  //===--------------------------------------------------------------------===//

  /// Parses a C++ new expression.
  Expr *parseCXXNewExpression();

  /// Parses a C++ delete expression.
  Expr *parseCXXDeleteExpression();

  /// Parses a C++ this expression.
  Expr *parseCXXThisExpr();

  /// Parses a C++ throw expression.
  Expr *parseCXXThrowExpr();

  /// Parses a lambda expression.
  Expr *parseLambdaExpression();

  /// Parses a lambda capture list.
  llvm::SmallVector<LambdaCapture, 4> parseLambdaCaptureList();

  /// Parses a fold expression (C++17).
  Expr *parseFoldExpression();

  /// Parses a requires expression (C++20).
  Expr *parseRequiresExpression();

  /// Parses the optional parameter list of a requires expression.
  void parseRequiresExpressionParameterList();

  /// Parses a single requirement in a requires expression body.
  Requirement *parseRequirement();

  /// Parses a C-style cast expression.
  Expr *parseCStyleCastExpr();

  //===--------------------------------------------------------------------===//
  // Type parsing
  //===--------------------------------------------------------------------===//

  /// Parses a complete type.
  QualType parseType();

  /// Parses a type specifier (builtin types, named types).
  QualType parseTypeSpecifier();

  /// Parses a builtin type.
  QualType parseBuiltinType();

  /// Parses a declarator (pointers, references, arrays).
  QualType parseDeclarator(QualType Base);

  /// Parses an array dimension.
  QualType parseArrayDimension(QualType Base);

  //===--------------------------------------------------------------------===//
  // Declaration parsing
  //===--------------------------------------------------------------------===//

  /// Parses a declaration.
  Decl *parseDeclaration();

  /// Parses a variable declaration.
  VarDecl *parseVariableDeclaration(QualType Type, llvm::StringRef Name,
                                    SourceLocation Loc);

  /// Parses a function declaration.
  FunctionDecl *parseFunctionDeclaration(QualType ReturnType,
                                         llvm::StringRef Name,
                                         SourceLocation Loc);

  /// Parses a parameter declaration.
  ParmVarDecl *parseParameterDeclaration();

  /// Parses a class declaration.
  CXXRecordDecl *parseClassDeclaration(SourceLocation ClassLoc);

  /// Parses a struct declaration.
  RecordDecl *parseStructDeclaration(SourceLocation StructLoc);

  /// Parses a class/struct body.
  void parseClassBody(CXXRecordDecl *Class);

  /// Parses a class member.
  Decl *parseClassMember(CXXRecordDecl *Class);

  /// Parses an access specifier.
  AccessSpecDecl *parseAccessSpecifier(SourceLocation Loc);

  /// Parses a base clause.
  bool parseBaseClause(CXXRecordDecl *Class);

  /// Parses a base specifier.
  void parseBaseSpecifier(CXXRecordDecl *Class);

  //===--------------------------------------------------------------------===//
  // Statement parsing
  //===--------------------------------------------------------------------===//

  /// Parses a statement.
  Stmt *parseStatement();

  /// Parses a compound statement (block).
  Stmt *parseCompoundStatement();

  /// Parses a return statement.
  Stmt *parseReturnStatement();

  /// Parses a null statement (empty statement).
  Stmt *parseNullStatement();

  /// Parses an expression statement.
  Stmt *parseExpressionStatement();

  /// Parses a declaration statement.
  Stmt *parseDeclarationStatement();

  /// Parses a label statement.
  Stmt *parseLabelStatement();

  /// Parses a case statement.
  Stmt *parseCaseStatement();

  /// Parses a default statement.
  Stmt *parseDefaultStatement();

  /// Parses a break statement.
  Stmt *parseBreakStatement();

  /// Parses a continue statement.
  Stmt *parseContinueStatement();

  /// Parses a goto statement.
  Stmt *parseGotoStatement();

  /// Parses an if statement.
  Stmt *parseIfStatement();

  /// Parses a switch statement.
  Stmt *parseSwitchStatement();

  /// Parses a while statement.
  Stmt *parseWhileStatement();

  /// Parses a do-while statement.
  Stmt *parseDoStatement();

  /// Parses a for statement.
  Stmt *parseForStatement();

  /// Parses a C++11 range-based for statement.
  Stmt *parseCXXForRangeStatement();

  /// Parses a C++ try statement.
  Stmt *parseCXXTryStatement();

  /// Parses a C++ catch clause.
  Stmt *parseCXXCatchClause();

  /// Parses a C++20 co_return statement.
  Stmt *parseCoreturnStatement();

  /// Parses a C++20 co_yield statement.
  Stmt *parseCoyieldStatement();

  /// Parses a C++20 co_await expression.
  Expr *parseCoawaitExpression();

  /// Parses a C++26 pack indexing expression.
  Expr *parsePackIndexingExpr();

  /// Parses a C++26 reflexpr expression.
  Expr *parseReflexprExpr();

private:
  //===--------------------------------------------------------------------===//
  // Internal helpers
  //===--------------------------------------------------------------------===//

  /// Advances to the next token from the preprocessor.
  void advanceToken();

  /// Initializes the token lookahead.
  void initializeTokenLookahead();

  /// Converts TokenKind to BinaryOpKind.
  static BinaryOpKind getBinaryOpKind(TokenKind K);

  /// Converts TokenKind to UnaryOpKind.
  static UnaryOpKind getUnaryOpKind(TokenKind K);
};

} // namespace blocktype
