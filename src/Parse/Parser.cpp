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
#include "blocktype/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

Parser::Parser(Preprocessor &PP, ASTContext &Ctx, Sema &S)
    : PP(PP), Context(Ctx), Actions(S), Diags(PP.getDiagnostics()) {
  initializeTokenLookahead();
}

Parser::~Parser() = default;

//===----------------------------------------------------------------------===//
// Main entry point
//===----------------------------------------------------------------------===//

TranslationUnitDecl *Parser::parseTranslationUnit() {
  // Sema's constructor already created a TU scope — nothing to sync.
  
  // Create TranslationUnitDecl
  SourceLocation StartLoc = Tok.getLocation();
  TranslationUnitDecl *TU = Actions.ActOnTranslationUnitDecl(StartLoc);
  
  // Parse declarations
  while (!Tok.is(TokenKind::eof)) {
    // P7.4.3: Skip preprocessing directives (#include, #define, etc.)
    if (Tok.is(TokenKind::hash)) {
      skipPreprocessingDirective();
      continue;
    }
    
    Decl *D = parseDeclaration();
    if (D) {
      // Sema's ActOn methods already register to CurContext (which is TU at top level)
      // No need to call TU->addDecl() again
    } else {
      // Error recovery: skip to next declaration boundary
      if (!skipUntilNextDeclaration()) {
        // Reached EOF during recovery
        break;
      }
    }
  }
  
  // Don't pop the TU scope — Sema's destructor will clean it up.
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

  // Emit a more specific error with the expected and actual token
  emitError(DiagID::err_expected_token_and_got,
            getTokenName(K), getTokenName(Tok.getKind()));
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

bool Parser::skipUntilBalanced(std::initializer_list<TokenKind> StopTokens) {
  unsigned ParenDepth = 0;
  unsigned BraceDepth = 0;
  unsigned SquareDepth = 0;

  while (!Tok.is(TokenKind::eof)) {
    // Track nesting depth
    if (Tok.is(TokenKind::l_paren)) {
      ++ParenDepth;
      consumeToken();
      continue;
    }
    if (Tok.is(TokenKind::r_paren)) {
      if (ParenDepth == 0) {
        // Unmatched closing paren — stop here; the caller decides what to do
        break;
      }
      --ParenDepth;
      consumeToken();
      continue;
    }
    if (Tok.is(TokenKind::l_brace)) {
      ++BraceDepth;
      consumeToken();
      continue;
    }
    if (Tok.is(TokenKind::r_brace)) {
      if (BraceDepth == 0)
        break;
      --BraceDepth;
      consumeToken();
      continue;
    }
    if (Tok.is(TokenKind::l_square)) {
      ++SquareDepth;
      consumeToken();
      continue;
    }
    if (Tok.is(TokenKind::r_square)) {
      if (SquareDepth == 0)
        break;
      --SquareDepth;
      consumeToken();
      continue;
    }

    // Only check stop tokens when we are at the top level (no open brackets)
    if (ParenDepth == 0 && BraceDepth == 0 && SquareDepth == 0) {
      for (TokenKind K : StopTokens) {
        if (Tok.is(K)) {
          return true;
        }
      }
    }

    consumeToken();
  }

  // We either found a stop token or reached EOF / unmatched close bracket
  for (TokenKind K : StopTokens) {
    if (Tok.is(K))
      return true;
  }
  return false;
}

bool Parser::skipUntilNextDeclaration() {
  // Skip to the next declaration boundary.
  // We look for:
  //  1. ';' at the top level (statement/declaration end)
  //  2. '}' at the top level (block end)
  //  3. A token that starts a new declaration

  while (!Tok.is(TokenKind::eof)) {
    // If we see a declaration-starting keyword at the top level, stop.
    if (isDeclarationStart())
      return true;

    // If we see ';' at the top level, consume it and stop at the next token
    // which (hopefully) starts a new declaration.
    if (Tok.is(TokenKind::semicolon)) {
      consumeToken();
      // After consuming ';', check if the next token starts a declaration
      if (isDeclarationStart() || Tok.is(TokenKind::eof))
        return true;
      // Otherwise keep skipping
      continue;
    }

    // If we see '}' at the top level, consume it and stop
    if (Tok.is(TokenKind::r_brace)) {
      consumeToken();  // Consume '}' to ensure progress
      return true;
    }

    // If we see '{', skip the entire balanced block
    if (Tok.is(TokenKind::l_brace)) {
      consumeToken();
      skipUntilBalanced({TokenKind::r_brace});
      if (Tok.is(TokenKind::r_brace))
        consumeToken();
      // After the block, check for declaration start or ';'
      continue;
    }

    // Skip this token
    consumeToken();
  }

  return false;
}

// P7.4.3: Skip preprocessing directive (#include, #define, etc.)
void Parser::skipPreprocessingDirective() {
  // Consume the '#' token
  consumeToken();
  
  // Skip all tokens until newline or EOF
  // In BlockType's lexer, newlines are not tokens, so we skip until:
  // 1. We hit EOF
  // 2. We hit a token that looks like it starts a new line (heuristic)
  // For simplicity, just skip until we see something that looks like a declaration
  while (!Tok.is(TokenKind::eof)) {
    // Check if next token could start a declaration
    if (isDeclarationStart()) {
      break;
    }
    consumeToken();
  }
}

bool Parser::tryRecoverMissingSemicolon() {
  // Check if the current token looks like it could start a new statement or
  // declaration. If so, the ';' was probably just forgotten.
  if (isDeclarationStart() || isStatementStart()) {
    emitError(DiagID::err_expected_semi);
    Diags.report(Tok.getLocation(), DiagID::note_insert_semicolon);
    return true; // Recovery successful — continue from current token
  }

  // If the current token is ';', just consume it (maybe there were extras)
  if (Tok.is(TokenKind::semicolon)) {
    consumeToken();
    return true;
  }

  // Could not recover — caller should use skipUntil-style recovery
  return false;
}

bool Parser::isDeclarationStart() {
  // Check if the current token (or token sequence) can start a declaration.
  TokenKind K = Tok.getKind();

  // Declaration keywords
  switch (K) {
    case TokenKind::kw_namespace:
    case TokenKind::kw_template:
    case TokenKind::kw_using:
    case TokenKind::kw_typedef:
    case TokenKind::kw_extern:
    case TokenKind::kw_static_assert:
    case TokenKind::kw_class:
    case TokenKind::kw_struct:
    case TokenKind::kw_union:
    case TokenKind::kw_enum:
    case TokenKind::kw_concept:
    case TokenKind::kw_module:
    case TokenKind::kw_import:
    case TokenKind::kw_export:
    case TokenKind::kw_inline:
    case TokenKind::kw_constexpr:
    case TokenKind::kw_consteval:
    case TokenKind::kw_constinit:
    case TokenKind::kw_friend:
    case TokenKind::kw_virtual:
    case TokenKind::kw_explicit:
    case TokenKind::kw_thread_local:
    case TokenKind::kw_mutable:
    case TokenKind::kw_static:
    case TokenKind::kw_register:
      return true;
    default:
      break;
  }

  // Type keywords
  if (isTypeKeyword(K))
    return true;

  // Identifier that could be a type name (e.g., custom types)
  // We check if the next token suggests a declaration
  if (K == TokenKind::identifier) {
    // identifier followed by identifier => likely "TypeName varName"
    // identifier followed by * or & => likely "TypeName* ptr"
    // identifier followed by :: => likely qualified type
    TokenKind Next = NextTok.getKind();
    if (Next == TokenKind::identifier ||
        Next == TokenKind::star ||
        Next == TokenKind::amp ||
        Next == TokenKind::ampamp ||
        Next == TokenKind::coloncolon ||
        Next == TokenKind::less)
      return true;
  }

  // Attribute specifier [[...]]
  if (K == TokenKind::l_square && NextTok.is(TokenKind::l_square))
    return true;

  // asm declaration
  if (K == TokenKind::kw_asm)
    return true;

  // __attribute__
  if (K == TokenKind::identifier && Tok.getText() == "__attribute__")
    return true;

  return false;
}

bool Parser::isStatementStart() {
  TokenKind K = Tok.getKind();

  switch (K) {
    // Control flow
    case TokenKind::kw_if:
    case TokenKind::kw_else:
    case TokenKind::kw_while:
    case TokenKind::kw_do:
    case TokenKind::kw_for:
    case TokenKind::kw_switch:
    case TokenKind::kw_case:
    case TokenKind::kw_default:
    // Jumps
    case TokenKind::kw_break:
    case TokenKind::kw_continue:
    case TokenKind::kw_return:
    case TokenKind::kw_goto:
    // Exception handling
    case TokenKind::kw_try:
    case TokenKind::kw_throw:
    // C++20 coroutines
    case TokenKind::kw_co_return:
    case TokenKind::kw_co_yield:
    case TokenKind::kw_co_await:
    // Block
    case TokenKind::l_brace:
    case TokenKind::semicolon:
      return true;
    default:
      break;
  }

  // An expression could also start here (identifier, literal, etc.)
  // But we don't count those as "statement starts" for semicolon recovery
  // because they could also be continuation of a previous expression.

  return false;
}

void Parser::emitError(SourceLocation Loc, DiagID ID) {
  Diags.report(Loc, ID);
  ++ErrorCount;
}

void Parser::emitError(DiagID ID) {
  emitError(Tok.getLocation(), ID);
}

void Parser::emitError(DiagID ID, llvm::StringRef Expected, llvm::StringRef Got) {
  std::string Extra = std::string("'") + Expected.str() + "' but got '" + Got.str() + "'";
  Diags.report(Tok.getLocation(), ID, Extra);
  ++ErrorCount;
}

Expr *Parser::createRecoveryExpr(SourceLocation Loc) {
  HasRecoveryExpr = true;

  // Create a placeholder integer literal for recovery
  // This prevents cascading errors
  return Actions.ActOnIntegerLiteral(Loc, llvm::APInt(32, 0)).get();
}

//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// Internal helpers
//===----------------------------------------------------------------------===//

void Parser::advanceToken() {
  Token Result;
  if (!PP.consumeToken(Result)) {
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

//===----------------------------------------------------------------------===//
// TentativeParsingAction Implementation
//===----------------------------------------------------------------------===//

TentativeParsingAction::TentativeParsingAction(Parser &Parser)
    : P(Parser), SavedTok(Parser.Tok), SavedNextTok(Parser.NextTok),
      SavedBufferIndex(Parser.PP.saveTokenBufferState()) {}

TentativeParsingAction::~TentativeParsingAction() {
  if (!Committed) {
    abort();
  }
}

void TentativeParsingAction::abort() {
  // Restore the parser's token state
  P.Tok = SavedTok;
  P.NextTok = SavedNextTok;

  // Restore the preprocessor's token buffer state
  P.PP.restoreTokenBufferState(SavedBufferIndex);

  Committed = false;
}

} // namespace blocktype
