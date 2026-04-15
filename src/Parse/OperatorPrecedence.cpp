//===--- OperatorPrecedence.cpp - Operator Precedence Impl -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements operator precedence helpers.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Parse/OperatorPrecedence.h"

namespace blocktype {

PrecedenceLevel getBinOpPrecedence(TokenKind K) {
  switch (K) {
  // Comma
  case TokenKind::comma:
    return PrecedenceLevel::Comma;

  // Assignment operators (right associative)
  case TokenKind::equal:
  case TokenKind::plusequal:
  case TokenKind::minusequal:
  case TokenKind::starequal:
  case TokenKind::slashequal:
  case TokenKind::percentequal:
  case TokenKind::ampequal:
  case TokenKind::pipeequal:
  case TokenKind::caretequal:
  case TokenKind::lesslessequal:
  case TokenKind::greatergreaterequal:
    return PrecedenceLevel::Assignment;

  // Conditional operator (right associative)
  case TokenKind::question:
    return PrecedenceLevel::Conditional;

  // Logical OR
  case TokenKind::pipepipe:
    return PrecedenceLevel::LogicalOr;

  // Logical AND
  case TokenKind::ampamp:
    return PrecedenceLevel::LogicalAnd;

  // Bitwise OR
  case TokenKind::pipe:
    return PrecedenceLevel::InclusiveOr;

  // Bitwise XOR
  case TokenKind::caret:
    return PrecedenceLevel::ExclusiveOr;

  // Bitwise AND
  case TokenKind::amp:
    return PrecedenceLevel::And;

  // Equality
  case TokenKind::equalequal:
  case TokenKind::exclaimequal:
    return PrecedenceLevel::Equality;

  // Relational
  case TokenKind::less:
  case TokenKind::greater:
  case TokenKind::lessequal:
  case TokenKind::greaterequal:
  case TokenKind::spaceship:
    return PrecedenceLevel::Relational;

  // Shift
  case TokenKind::lessless:
  case TokenKind::greatergreater:
    return PrecedenceLevel::Shift;

  // Additive
  case TokenKind::plus:
  case TokenKind::minus:
    return PrecedenceLevel::Additive;

  // Multiplicative
  case TokenKind::star:
  case TokenKind::slash:
  case TokenKind::percent:
    return PrecedenceLevel::Multiplicative;

  // Pointer-to-member
  case TokenKind::periodstar:
  case TokenKind::arrowstar:
    return PrecedenceLevel::PM;

  default:
    return PrecedenceLevel::Unknown;
  }
}

bool isRightAssociative(TokenKind K) {
  switch (K) {
  // Assignment operators
  case TokenKind::equal:
  case TokenKind::plusequal:
  case TokenKind::minusequal:
  case TokenKind::starequal:
  case TokenKind::slashequal:
  case TokenKind::percentequal:
  case TokenKind::ampequal:
  case TokenKind::pipeequal:
  case TokenKind::caretequal:
  case TokenKind::lesslessequal:
  case TokenKind::greatergreaterequal:
  // Conditional operator
  case TokenKind::question:
    return true;

  default:
    return false;
  }
}

PrecedenceLevel getUnaryOpPrecedence(TokenKind K) {
  switch (K) {
  // Prefix unary operators
  case TokenKind::plusplus:   // ++x
  case TokenKind::minusminus: // --x
  case TokenKind::plus:       // +x
  case TokenKind::minus:      // -x
  case TokenKind::exclaim:    // !x
  case TokenKind::tilde:      // ~x
  case TokenKind::star:       // *x (dereference)
  case TokenKind::amp:        // &x (address-of)
    return PrecedenceLevel::Unary;

  // sizeof, alignof
  case TokenKind::kw_sizeof:
  case TokenKind::kw_alignof:
    return PrecedenceLevel::Unary;

  // new, delete
  case TokenKind::kw_new:
  case TokenKind::kw_delete:
    return PrecedenceLevel::Unary;

  default:
    return PrecedenceLevel::Unknown;
  }
}

bool isBinaryOperator(TokenKind K) {
  return getBinOpPrecedence(K) != PrecedenceLevel::Unknown;
}

bool isUnaryOperator(TokenKind K) {
  return getUnaryOpPrecedence(K) != PrecedenceLevel::Unknown;
}

bool canStartExpression(TokenKind K) {
  switch (K) {
  // Literals
  case TokenKind::numeric_constant:
  case TokenKind::char_constant:
  case TokenKind::string_literal:
  case TokenKind::wide_string_literal:
  case TokenKind::utf8_string_literal:
  case TokenKind::utf16_string_literal:
  case TokenKind::utf32_string_literal:
  case TokenKind::kw_true:
  case TokenKind::kw_false:
  case TokenKind::kw_nullptr:
    return true;

  // Identifiers
  case TokenKind::identifier:
    return true;

  // Parenthesized expression
  case TokenKind::l_paren:
    return true;

  // Unary operators
  case TokenKind::plusplus:
  case TokenKind::minusminus:
  case TokenKind::plus:
  case TokenKind::minus:
  case TokenKind::exclaim:
  case TokenKind::tilde:
  case TokenKind::star:
  case TokenKind::amp:
    return true;

  // sizeof, alignof, new, delete
  case TokenKind::kw_sizeof:
  case TokenKind::kw_alignof:
  case TokenKind::kw_new:
  case TokenKind::kw_delete:
    return true;

  // this
  case TokenKind::kw_this:
    return true;

  // Lambda introducer
  case TokenKind::l_square:
    return true;

  default:
    return false;
  }
}

} // namespace blocktype
