//===--- OperatorPrecedence.h - Operator Precedence --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines operator precedence levels for the parser.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Lex/Token.h"

namespace blocktype {

/// PrecedenceLevel - Operator precedence levels (from low to high).
/// Higher values mean higher precedence (binds tighter).
enum class PrecedenceLevel {
  Unknown = 0,        // Unknown precedence
  Comma = 1,          // ,
  Assignment = 2,     // = += -= *= /= %= &= |= ^= <<= >>= (right associative)
  Conditional = 3,    // ?: (right associative)
  LogicalOr = 4,      // ||
  LogicalAnd = 5,     // &&
  InclusiveOr = 6,    // |
  ExclusiveOr = 7,    // ^
  And = 8,            // &
  Equality = 9,       // == !=
  Relational = 10,    // < > <= >= <=>
  Shift = 11,         // << >>
  Additive = 12,      // + -
  Multiplicative = 13,// * / %
  PM = 14,            // .* ->*
  Unary = 15,         // ++ -- + - ! ~ * & sizeof new delete (prefix)
  Postfix = 16,       // () [] . -> ++ -- typeid
  Primary = 17,       // literals, identifiers, (expr)
};

/// Returns the precedence level for a binary operator token.
/// Returns PrecedenceLevel::Unknown if the token is not a binary operator.
PrecedenceLevel getBinOpPrecedence(TokenKind K);

/// Returns true if the operator is right-associative.
/// Right-associative operators: = += -= *= /= %= &= |= ^= <<= >>=, ?:
bool isRightAssociative(TokenKind K);

/// Returns the precedence level for a unary operator token (prefix).
/// Returns PrecedenceLevel::Unknown if the token is not a unary operator.
PrecedenceLevel getUnaryOpPrecedence(TokenKind K);

/// Returns true if the token is a binary operator.
bool isBinaryOperator(TokenKind K);

/// Returns true if the token is a unary operator (prefix).
bool isUnaryOperator(TokenKind K);

/// Returns true if the token can start an expression.
bool canStartExpression(TokenKind K);

} // namespace blocktype
