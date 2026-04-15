//===--- Parser.cpp - Parser Implementation -----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Parser class.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Parse/Parser.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

Parser::Parser(Preprocessor &PP, ASTContext &Ctx)
    : PP(PP), Context(Ctx), Diags(PP.getDiagnostics()) {
  initializeTokenLookahead();
}

Parser::~Parser() = default;

//===----------------------------------------------------------------------===//
// Main entry point
//===----------------------------------------------------------------------===//

TranslationUnitDecl *Parser::parseTranslationUnit() {
  // Create translation unit scope
  pushScope(ScopeFlags::TranslationUnitScope);
  
  // Create TranslationUnitDecl
  SourceLocation StartLoc = Tok.getLocation();
  TranslationUnitDecl *TU = Context.create<TranslationUnitDecl>(StartLoc);
  
  // Parse declarations
  while (!Tok.is(TokenKind::eof)) {
    Decl *D = parseDeclaration();
    if (D) {
      TU->addDecl(D);
      
      // Add to symbol table if it's a named declaration
      if (auto *ND = llvm::dyn_cast<NamedDecl>(D)) {
        if (CurrentScope) {
          CurrentScope->addDecl(ND);
        }
      }
    } else {
      // Error recovery: skip to next token
      consumeToken();
    }
  }
  
  popScope();
  return TU;
}

//===----------------------------------------------------------------------===//
// Token operations
//===----------------------------------------------------------------------===//

void Parser::consumeToken() {
  // Move NextTok to Tok
  Tok = NextTok;

  // Get the next token from preprocessor
  advanceToken();
}

bool Parser::tryConsumeToken(TokenKind K) {
  if (Tok.is(K)) {
    consumeToken();
    return true;
  }
  return false;
}

void Parser::expectAndConsume(TokenKind K, const char *Msg) {
  if (Tok.is(K)) {
    consumeToken();
    return;
  }

  // Emit error
  emitError(DiagID::err_expected);
}

//===----------------------------------------------------------------------===//
// Error recovery
//===----------------------------------------------------------------------===//

void Parser::skipUntil(std::initializer_list<TokenKind> StopTokens) {
  while (!Tok.is(TokenKind::eof)) {
    // Check if current token is a stop token
    for (TokenKind K : StopTokens) {
      if (Tok.is(K)) {
        return;
      }
    }

    // Skip this token
    consumeToken();
  }
}

void Parser::emitError(SourceLocation Loc, DiagID ID) {
  Diags.report(Loc, ID);
  ++ErrorCount;
}

void Parser::emitError(DiagID ID) {
  emitError(Tok.getLocation(), ID);
}

Expr *Parser::createRecoveryExpr(SourceLocation Loc) {
  HasRecoveryExpr = true;

  // Create a placeholder integer literal for recovery
  // This prevents cascading errors
  return Context.create<IntegerLiteral>(Loc, llvm::APInt(32, 0));
}

//===----------------------------------------------------------------------===//
// Scope management
//===----------------------------------------------------------------------===//

void Parser::pushScope(ScopeFlags Flags) {
  CurrentScope = new Scope(CurrentScope, Flags);
}

void Parser::popScope() {
  if (CurrentScope) {
    Scope *Parent = CurrentScope->getParent();
    delete CurrentScope;
    CurrentScope = Parent;
  }
}

//===----------------------------------------------------------------------===//
// Internal helpers
//===----------------------------------------------------------------------===//

void Parser::advanceToken() {
  Token Result;
  if (!PP.lexToken(Result)) {
    // EOF reached
    NextTok.setKind(TokenKind::eof);
  } else {
    NextTok = Result;
  }
}

void Parser::initializeTokenLookahead() {
  // Get the first token
  Token First;
  if (!PP.lexToken(First)) {
    Tok.setKind(TokenKind::eof);
    NextTok.setKind(TokenKind::eof);
  } else {
    Tok = First;
    advanceToken();
  }
}

} // namespace blocktype
