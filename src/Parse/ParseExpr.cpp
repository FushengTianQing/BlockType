//===--- ParseExpr.cpp - Expression Parsing -----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements expression parsing for the Parser.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Parse/Parser.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/Sema/Scope.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Expression parsing entry point
//===----------------------------------------------------------------------===//

Expr *Parser::parseExpression() {
  pushContext(ParsingContext::Expression);

  Expr *LHS = parseUnaryExpression();
  if (!LHS) {
    popContext();
    return nullptr;
  }

  Expr *Result = parseRHS(LHS, PrecedenceLevel::Comma);
  popContext();
  return Result;
}

Expr *Parser::parseAssignmentExpression() {
  pushContext(ParsingContext::Expression);

  Expr *LHS = parseUnaryExpression();
  if (!LHS) {
    popContext();
    return nullptr;
  }

  // Stop at comma (Assignment precedence is higher than Comma)
  Expr *Result = parseRHS(LHS, PrecedenceLevel::Assignment);
  popContext();
  return Result;
}

Expr *Parser::parseExpressionWithPrecedence(PrecedenceLevel MinPrec) {
  Expr *LHS = parseUnaryExpression();
  if (!LHS)
    return nullptr;

  return parseRHS(LHS, MinPrec);
}

//===----------------------------------------------------------------------===//
// Precedence climbing algorithm
//===----------------------------------------------------------------------===//

Expr *Parser::parseRHS(Expr *LHS, PrecedenceLevel MinPrec) {
  while (true) {
    PrecedenceLevel TokPrec = getBinOpPrecedence(Tok.getKind());

    // If this operator has lower precedence than we are allowed to parse,
    // return what we have so far.
    if (static_cast<unsigned>(TokPrec) < static_cast<unsigned>(MinPrec))
      break;

    // Consume the operator
    TokenKind BinOp = Tok.getKind();
    SourceLocation OpLoc = Tok.getLocation();
    consumeToken();

    // Handle conditional operator specially
    if (BinOp == TokenKind::question) {
      LHS = parseConditionalExpression(LHS);
      continue;
    }

    // Parse the RHS (unary expression)
    Expr *RHS = parseUnaryExpression();
    if (!RHS) {
      emitError(DiagID::err_expected_expression);
      RHS = createRecoveryExpr(OpLoc);
    }

    // Determine the precedence for the next operator
    PrecedenceLevel NextPrec = getBinOpPrecedence(Tok.getKind());

    // If the next operator binds tighter, or if this is a right-associative
    // operator and the next has the same precedence, recursively parse it.
    if (static_cast<unsigned>(NextPrec) > static_cast<unsigned>(TokPrec) ||
        (NextPrec == TokPrec && isRightAssociative(BinOp))) {
      // For right-associative operators, use the same precedence
      // For left-associative, use one higher
      PrecedenceLevel NewMinPrec = TokPrec;
      if (!isRightAssociative(BinOp)) {
        NewMinPrec = static_cast<PrecedenceLevel>(
            static_cast<unsigned>(TokPrec) + 1);
      }
      RHS = parseRHS(RHS, NewMinPrec);
      if (!RHS) {
        RHS = createRecoveryExpr(OpLoc);
      }
    }

    // Create the binary operator node
    BinaryOpKind BOK = getBinaryOpKind(BinOp);
    LHS = Context.create<BinaryOperator>(LHS->getLocation(), LHS, RHS, BOK);
  }

  return LHS;
}

//===----------------------------------------------------------------------===//
// Unary expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseUnaryExpression() {
  // Check for C++ new/delete
  if (Tok.is(TokenKind::kw_new)) {
    return parseCXXNewExpression();
  }
  if (Tok.is(TokenKind::kw_delete)) {
    return parseCXXDeleteExpression();
  }

  // Check for prefix unary operators
  PrecedenceLevel UnaryPrec = getUnaryOpPrecedence(Tok.getKind());

  if (UnaryPrec != PrecedenceLevel::Unknown) {
    SourceLocation OpLoc = Tok.getLocation();
    TokenKind OpKind = Tok.getKind();
    consumeToken();

    // Parse the operand
    Expr *Operand = parseUnaryExpression();
    if (!Operand) {
      return createRecoveryExpr(OpLoc);
    }

    // Create unary operator
    UnaryOpKind UOK = getUnaryOpKind(OpKind);
    return Context.create<UnaryOperator>(OpLoc, Operand, UOK);
  }

  // Parse postfix expression
  Expr *Base = parsePrimaryExpression();
  if (!Base)
    return nullptr;

  return parsePostfixExpression(Base);
}

//===----------------------------------------------------------------------===//
// Postfix expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parsePostfixExpression(Expr *Base) {
  while (true) {
    switch (Tok.getKind()) {
    case TokenKind::l_paren:
      // Function call
      Base = parseCallExpression(Base);
      break;

    case TokenKind::l_square: {
      // Array subscript: base[index]
      SourceLocation LLoc = Tok.getLocation();
      consumeToken();

      // Parse index expression
      Expr *Index = parseExpression();
      if (!Index) {
        Index = createRecoveryExpr(LLoc);
      }

      // Expect ']'
      if (!tryConsumeToken(TokenKind::r_square)) {
        emitError(DiagID::err_expected);
        skipUntil({TokenKind::r_square});
        tryConsumeToken(TokenKind::r_square);
      }

      // Create ArraySubscriptExpr
      Base = Context.create<ArraySubscriptExpr>(LLoc, Base, Index);
      break;
    }

    case TokenKind::period: {
      // Member access: base.member
      SourceLocation OpLoc = Tok.getLocation();
      consumeToken();

      // Expect member name
      if (!Tok.is(TokenKind::identifier)) {
        emitError(DiagID::err_expected_identifier);
        return Base;
      }

      // TODO: Lookup member in the base type
      // For now, create a placeholder
      llvm::StringRef MemberName = Tok.getText();
      SourceLocation MemberLoc = Tok.getLocation();
      consumeToken();

      // Create a placeholder ValueDecl for the member
      // In a real implementation, this would be looked up in the class/struct
      ValueDecl *MemberDecl = nullptr; // TODO: Implement proper member lookup

      // Create MemberExpr
      Base = Context.create<MemberExpr>(OpLoc, Base, MemberDecl, false);
      break;
    }

    case TokenKind::arrow: {
      // Pointer member access: base->member
      SourceLocation OpLoc = Tok.getLocation();
      consumeToken();

      // Expect member name
      if (!Tok.is(TokenKind::identifier)) {
        emitError(DiagID::err_expected_identifier);
        return Base;
      }

      // TODO: Lookup member in the base type
      // For now, create a placeholder
      llvm::StringRef MemberName = Tok.getText();
      SourceLocation MemberLoc = Tok.getLocation();
      consumeToken();

      // Create a placeholder ValueDecl for the member
      // In a real implementation, this would be looked up in the class/struct
      ValueDecl *MemberDecl = nullptr; // TODO: Implement proper member lookup

      // Create MemberExpr with IsArrow = true
      Base = Context.create<MemberExpr>(OpLoc, Base, MemberDecl, true);
      break;
    }

    case TokenKind::plusplus: {
      // Postfix increment: expr++
      SourceLocation OpLoc = Tok.getLocation();
      consumeToken();

      // Create UnaryOperator with PostInc
      Base = Context.create<UnaryOperator>(OpLoc, Base, UnaryOpKind::PostInc);
      break;
    }

    case TokenKind::minusminus: {
      // Postfix decrement: expr--
      SourceLocation OpLoc = Tok.getLocation();
      consumeToken();

      // Create UnaryOperator with PostDec
      Base = Context.create<UnaryOperator>(OpLoc, Base, UnaryOpKind::PostDec);
      break;
    }

    default:
      return Base;
    }
  }
}

//===----------------------------------------------------------------------===//
// Primary expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parsePrimaryExpression() {
  switch (Tok.getKind()) {
  // Literals
  case TokenKind::numeric_constant: {
    // Determine if integer or floating point
    StringRef Text = Tok.getText();
    bool IsFloat = Text.find('.') != StringRef::npos ||
                   Text.find('e') != StringRef::npos ||
                   Text.find('E') != StringRef::npos;
    if (IsFloat) {
      return parseFloatingLiteral();
    }
    return parseIntegerLiteral();
  }

  case TokenKind::char_constant:
    return parseCharacterLiteral();

  case TokenKind::string_literal:
  case TokenKind::wide_string_literal:
  case TokenKind::utf8_string_literal:
  case TokenKind::utf16_string_literal:
  case TokenKind::utf32_string_literal:
    return parseStringLiteral();

  case TokenKind::kw_true:
  case TokenKind::kw_false:
    return parseBoolLiteral();

  case TokenKind::kw_nullptr:
    return parseNullPtrLiteral();

  // Identifier
  case TokenKind::identifier:
    return parseIdentifier();

  // Parenthesized expression
  case TokenKind::l_paren:
    return parseParenExpression();
  
  // Brace-enclosed initializer list
  case TokenKind::l_brace:
    return parseInitializerList();

  // Lambda expression
  case TokenKind::l_square:
    return parseLambdaExpression();

  // this
  case TokenKind::kw_this:
    return parseCXXThisExpr();

  // throw
  case TokenKind::kw_throw:
    return parseCXXThrowExpr();

  // requires (C++20)
  case TokenKind::kw_requires:
    return parseRequiresExpression();

  default:
    // Emit error for unexpected token
    emitError(DiagID::err_expected_expression);
    return nullptr;
  }
}

//===----------------------------------------------------------------------===//
// Literal parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseIntegerLiteral() {
  SourceLocation Loc = Tok.getLocation();
  StringRef Text = Tok.getText();

  // Parse the integer value
  llvm::APInt Value(64, 0);
  bool Overflow = false;

  // Determine base
  unsigned Base = 10;
  if (Text.starts_with("0x") || Text.starts_with("0X")) {
    Base = 16;
    Text = Text.drop_front(2);
  } else if (Text.starts_with("0b") || Text.starts_with("0B")) {
    Base = 2;
    Text = Text.drop_front(2);
  } else if (Text.starts_with("0") && Text.size() > 1) {
    Base = 8;
    Text = Text.drop_front(1);
  }

  // Remove suffix (u, l, ll, etc.)
  // TODO: Handle suffixes properly
  while (!Text.empty() && (Text.back() == 'u' || Text.back() == 'U' ||
                           Text.back() == 'l' || Text.back() == 'L')) {
    Text = Text.drop_back(1);
  }

  // Parse the value
  if (!Text.empty()) {
    Value = llvm::APInt(64, 0);
    for (char C : Text) {
      unsigned Digit = 0;
      if (C >= '0' && C <= '9')
        Digit = C - '0';
      else if (C >= 'a' && C <= 'f')
        Digit = C - 'a' + 10;
      else if (C >= 'A' && C <= 'F')
        Digit = C - 'A' + 10;
      else
        continue;

      if (Digit >= Base)
        continue;

      Value = Value * Base + Digit;
    }
  }

  consumeToken();
  return Context.create<IntegerLiteral>(Loc, Value);
}

Expr *Parser::parseFloatingLiteral() {
  SourceLocation Loc = Tok.getLocation();
  StringRef Text = Tok.getText();

  // Parse the floating point value
  // TODO: Use llvm::APFloat properly
  double Value = 0.0;

  consumeToken();
  return Context.create<FloatingLiteral>(Loc, llvm::APFloat(Value));
}

Expr *Parser::parseStringLiteral() {
  SourceLocation Loc = Tok.getLocation();
  StringRef Text = Tok.getText();

  // Remove quotes
  if (Text.size() >= 2 && Text.front() == '"' && Text.back() == '"') {
    Text = Text.drop_front().drop_back();
  }

  consumeToken();
  return Context.create<StringLiteral>(Loc, Text.str());
}

Expr *Parser::parseCharacterLiteral() {
  SourceLocation Loc = Tok.getLocation();
  StringRef Text = Tok.getText();

  // Remove quotes
  uint32_t Value = 0;
  if (Text.size() >= 3 && Text.front() == '\'' && Text.back() == '\'') {
    StringRef Content = Text.drop_front().drop_back();
    // TODO: Handle escape sequences
    if (!Content.empty()) {
      Value = static_cast<unsigned char>(Content[0]);
    }
  }

  consumeToken();
  return Context.create<CharacterLiteral>(Loc, Value);
}

Expr *Parser::parseBoolLiteral() {
  SourceLocation Loc = Tok.getLocation();
  bool Value = Tok.is(TokenKind::kw_true);

  consumeToken();
  return Context.create<CXXBoolLiteral>(Loc, Value);
}

Expr *Parser::parseNullPtrLiteral() {
  SourceLocation Loc = Tok.getLocation();

  consumeToken();
  return Context.create<CXXNullPtrLiteral>(Loc);
}

Expr *Parser::parseIdentifier() {
  SourceLocation Loc = Tok.getLocation();
  StringRef Name = Tok.getText();

  consumeToken();

  // Lookup the declaration in the current scope
  ValueDecl *VD = nullptr;
  if (CurrentScope) {
    if (NamedDecl *D = CurrentScope->lookup(Name)) {
      // Found the declaration, create a DeclRefExpr
      VD = dyn_cast<ValueDecl>(D);
    }
  }

  // Create DeclRefExpr (with or without declaration)
  // If VD is nullptr, it's an undefined identifier (error recovery)
  return Context.create<DeclRefExpr>(Loc, VD);
}

Expr *Parser::parseParenExpression() {
  SourceLocation LParenLoc = Tok.getLocation();
  consumeToken();

  Expr *Inner = parseExpression();
  if (!Inner) {
    Inner = createRecoveryExpr(LParenLoc);
  }

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
    skipUntil({TokenKind::r_paren});
    tryConsumeToken(TokenKind::r_paren);
  }

  return Inner;
}

//===----------------------------------------------------------------------===//
// Conditional expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseConditionalExpression(Expr *Cond) {
  SourceLocation QuestionLoc = Tok.getLocation();

  // Parse the true expression
  Expr *TrueExpr = parseExpression();
  if (!TrueExpr) {
    emitError(DiagID::err_expected_expression);
    TrueExpr = createRecoveryExpr(QuestionLoc);
  }

  // Expect ':'
  if (!tryConsumeToken(TokenKind::colon)) {
    emitError(DiagID::err_expected);
    return Cond;
  }

  // Parse the false expression
  Expr *FalseExpr = parseExpression();
  if (!FalseExpr) {
    emitError(DiagID::err_expected_expression);
    FalseExpr = createRecoveryExpr(QuestionLoc);
  }

  return Context.create<ConditionalOperator>(Cond->getLocation(), Cond,
                                              TrueExpr, FalseExpr);
}

//===----------------------------------------------------------------------===//
// Call expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseCallExpression(Expr *Fn) {
  consumeToken(); // consume '('

  llvm::SmallVector<Expr *, 8> Args = parseCallArguments();

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  return Context.create<CallExpr>(Fn->getLocation(), Fn, Args);
}

llvm::SmallVector<Expr *, 8> Parser::parseCallArguments() {
  llvm::SmallVector<Expr *, 8> Args;

  // Empty argument list
  if (Tok.is(TokenKind::r_paren))
    return Args;

  while (true) {
    // Parse assignment expression (stops at comma)
    Expr *Arg = parseAssignmentExpression();
    if (!Arg) {
      emitError(DiagID::err_expected_expression);
      Arg = createRecoveryExpr(Tok.getLocation());
    }
    Args.push_back(Arg);

    if (!tryConsumeToken(TokenKind::comma))
      break;
  }

  return Args;
}

//===----------------------------------------------------------------------===//
// Helper functions
//===----------------------------------------------------------------------===//

BinaryOpKind Parser::getBinaryOpKind(TokenKind K) {
  switch (K) {
  case TokenKind::star:
    return BinaryOpKind::Mul;
  case TokenKind::slash:
    return BinaryOpKind::Div;
  case TokenKind::percent:
    return BinaryOpKind::Rem;
  case TokenKind::plus:
    return BinaryOpKind::Add;
  case TokenKind::minus:
    return BinaryOpKind::Sub;
  case TokenKind::lessless:
    return BinaryOpKind::Shl;
  case TokenKind::greatergreater:
    return BinaryOpKind::Shr;
  case TokenKind::less:
    return BinaryOpKind::LT;
  case TokenKind::greater:
    return BinaryOpKind::GT;
  case TokenKind::lessequal:
    return BinaryOpKind::LE;
  case TokenKind::greaterequal:
    return BinaryOpKind::GE;
  case TokenKind::equalequal:
    return BinaryOpKind::EQ;
  case TokenKind::exclaimequal:
    return BinaryOpKind::NE;
  case TokenKind::amp:
    return BinaryOpKind::And;
  case TokenKind::caret:
    return BinaryOpKind::Xor;
  case TokenKind::pipe:
    return BinaryOpKind::Or;
  case TokenKind::ampamp:
    return BinaryOpKind::LAnd;
  case TokenKind::pipepipe:
    return BinaryOpKind::LOr;
  case TokenKind::equal:
    return BinaryOpKind::Assign;
  case TokenKind::plusequal:
    return BinaryOpKind::AddAssign;
  case TokenKind::minusequal:
    return BinaryOpKind::SubAssign;
  case TokenKind::starequal:
    return BinaryOpKind::MulAssign;
  case TokenKind::slashequal:
    return BinaryOpKind::DivAssign;
  case TokenKind::percentequal:
    return BinaryOpKind::RemAssign;
  case TokenKind::ampequal:
    return BinaryOpKind::AndAssign;
  case TokenKind::caretequal:
    return BinaryOpKind::XorAssign;
  case TokenKind::pipeequal:
    return BinaryOpKind::OrAssign;
  case TokenKind::lesslessequal:
    return BinaryOpKind::ShlAssign;
  case TokenKind::greatergreaterequal:
    return BinaryOpKind::ShrAssign;
  case TokenKind::comma:
    return BinaryOpKind::Comma;
  default:
    // Unknown operator - default to Add
    return BinaryOpKind::Add;
  }
}

UnaryOpKind Parser::getUnaryOpKind(TokenKind K) {
  switch (K) {
  case TokenKind::plusplus:
    return UnaryOpKind::PreInc;
  case TokenKind::minusminus:
    return UnaryOpKind::PreDec;
  case TokenKind::plus:
    return UnaryOpKind::Plus;
  case TokenKind::minus:
    return UnaryOpKind::Minus;
  case TokenKind::exclaim:
    return UnaryOpKind::LNot;
  case TokenKind::tilde:
    return UnaryOpKind::Not;
  case TokenKind::star:
    return UnaryOpKind::Deref;
  case TokenKind::amp:
    return UnaryOpKind::AddrOf;
  default:
    // Unknown operator - default to Plus
    return UnaryOpKind::Plus;
  }
}

//===----------------------------------------------------------------------===//
// Initializer List Parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseInitializerList() {
  assert(Tok.is(TokenKind::l_brace) && "Expected '{'");
  SourceLocation LBraceLoc = Tok.getLocation();
  consumeToken(); // consume '{'
  
  llvm::SmallVector<Expr *, 8> Inits;
  
  // Parse initializer clauses
  while (!Tok.is(TokenKind::r_brace) && !Tok.is(TokenKind::eof)) {
    // Parse an initializer clause (can be an expression or nested init list)
    Expr *Init = parseAssignmentExpression();
    if (Init) {
      Inits.push_back(Init);
    } else {
      // Error recovery: skip to next comma or '}'
      while (!Tok.is(TokenKind::comma) && !Tok.is(TokenKind::r_brace) && 
             !Tok.is(TokenKind::eof)) {
        consumeToken();
      }
    }
    
    // Check for comma separator
    if (Tok.is(TokenKind::comma)) {
      consumeToken();
      // Allow trailing comma
      if (Tok.is(TokenKind::r_brace))
        break;
    } else if (!Tok.is(TokenKind::r_brace)) {
      // Missing comma or '}'
      emitError(DiagID::err_expected);
      break;
    }
  }
  
  // Expect '}'
  if (!Tok.is(TokenKind::r_brace)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }
  SourceLocation RBraceLoc = Tok.getLocation();
  consumeToken(); // consume '}'
  
  return Context.create<InitListExpr>(LBraceLoc, Inits, RBraceLoc);
}

} // namespace blocktype
