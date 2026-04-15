//===--- ParseDecl.cpp - Declaration Parsing -----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements declaration parsing for the BlockType parser.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Parse/Parser.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Lex/Token.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseDeclarationStatement - Parse a declaration statement.
///
/// declaration-statement ::= declaration
///
Stmt *Parser::parseDeclarationStatement() {
  pushContext(ParsingContext::Declaration);
  
  Decl *D = parseDeclaration();
  
  popContext();
  
  if (!D)
    return nullptr;
  
  // Create DeclStmt
  return Context.create<DeclStmt>(D->getLocation(), D);
}

/// parseDeclaration - Parse a declaration.
///
/// declaration ::= block-declaration
///               | function-definition
///               | namespace-definition
///
/// block-declaration ::= simple-declaration
///                     | asm-definition
///                     | namespace-alias-definition
///                     | using-declaration
///                     | using-directive
///
/// simple-declaration ::= decl-specifier-seq init-declarator-list? ';'
///
Decl *Parser::parseDeclaration() {
  // Parse declaration specifiers (type)
  QualType Type = parseType();
  if (Type.isNull()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }
  
  // Parse identifier
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }
  
  llvm::StringRef Name = Tok.getText();
  SourceLocation NameLoc = Tok.getLocation();
  consumeToken();
  
  // Check if this is a function declaration
  if (Tok.is(TokenKind::l_paren)) {
    return parseFunctionDeclaration(Type, Name, NameLoc);
  }
  
  // Otherwise, it's a variable declaration
  return parseVariableDeclaration(Type, Name, NameLoc);
}

/// parseVariableDeclaration - Parse a variable declaration.
///
/// init-declarator ::= declarator initializer?
///
/// initializer ::= '=' initializer-clause
///               | '(' expression-list ')'
///               | braced-init-list
///
VarDecl *Parser::parseVariableDeclaration(QualType Type, llvm::StringRef Name,
                                          SourceLocation Loc) {
  // Parse initializer if present
  Expr *Init = nullptr;
  
  if (Tok.is(TokenKind::equal)) {
    consumeToken();
    Init = parseExpression();
  } else if (Tok.is(TokenKind::l_paren)) {
    // Direct initialization
    consumeToken();
    llvm::SmallVector<Expr *, 4> Args;
    while (!Tok.is(TokenKind::r_paren)) {
      Expr *Arg = parseExpression();
      if (Arg)
        Args.push_back(Arg);
      if (!Tok.is(TokenKind::comma))
        break;
      consumeToken();
    }
    expectAndConsume(TokenKind::r_paren, "expected ')' after initializer");
    // TODO: Create a proper initialization expression
    // For now, use the first argument if there's only one
    if (Args.size() == 1)
      Init = Args[0];
  } else if (Tok.is(TokenKind::l_brace)) {
    // Brace initialization
    // TODO: Implement brace initialization
    emitError(DiagID::err_expected);
    return nullptr;
  }
  
  // Expect semicolon
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }
  consumeToken();
  
  // Create VarDecl
  return Context.create<VarDecl>(Loc, Name, Type, Init);
}

/// parseFunctionDeclaration - Parse a function declaration.
///
/// function-declaration ::= declarator function-body?
///
/// function-body ::= compound-statement
///                 | '=' 'delete' ';'
///                 | '=' 'default' ';'
///
FunctionDecl *Parser::parseFunctionDeclaration(QualType ReturnType,
                                               llvm::StringRef Name,
                                               SourceLocation Loc) {
  // Parse parameter list
  llvm::SmallVector<ParmVarDecl *, 8> Params;
  
  expectAndConsume(TokenKind::l_paren, "expected '(' in function declaration");
  
  while (!Tok.is(TokenKind::r_paren)) {
    ParmVarDecl *Param = parseParameterDeclaration();
    if (Param)
      Params.push_back(Param);
    
    if (!Tok.is(TokenKind::comma))
      break;
    consumeToken();
  }
  
  expectAndConsume(TokenKind::r_paren, "expected ')' after parameter list");
  
  // Parse function body
  Stmt *Body = nullptr;
  
  if (Tok.is(TokenKind::l_brace)) {
    Body = parseCompoundStatement();
  } else if (Tok.is(TokenKind::equal)) {
    consumeToken();
    if (Tok.is(TokenKind::kw_delete)) {
      consumeToken();
      // Deleted function - no body
    } else if (Tok.is(TokenKind::kw_default)) {
      consumeToken();
      // Defaulted function - no body
    }
    expectAndConsume(TokenKind::semicolon, "expected ';' after function definition");
  } else if (Tok.is(TokenKind::semicolon)) {
    // Function declaration without body
    consumeToken();
  }
  
  // Create function type
  llvm::SmallVector<const Type *, 8> ParamTypes;
  for (auto *Param : Params) {
    ParamTypes.push_back(Param->getType().getTypePtr());
  }
  
  // TODO: Create a proper FunctionType
  // For now, use the return type as the function type
  // This should be replaced with proper function type creation
  
  // Create FunctionDecl
  return Context.create<FunctionDecl>(Loc, Name, ReturnType, Params, Body);
}

/// parseParameterDeclaration - Parse a parameter declaration.
///
/// parameter-declaration ::= decl-specifier-seq declarator?
///                         | decl-specifier-seq declarator '=' assignment-expression
///
ParmVarDecl *Parser::parseParameterDeclaration() {
  // Parse type
  QualType Type = parseType();
  if (Type.isNull()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }
  
  // Parse parameter name (optional)
  llvm::StringRef Name;
  SourceLocation NameLoc;
  
  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();
  }
  
  // Parse default argument
  Expr *DefaultArg = nullptr;
  if (Tok.is(TokenKind::equal)) {
    consumeToken();
    DefaultArg = parseExpression();
  }
  
  // Create ParmVarDecl
  // Use 0 as the index for now (will be set correctly later)
  return Context.create<ParmVarDecl>(NameLoc, Name, Type, 0, DefaultArg);
}

} // namespace blocktype
