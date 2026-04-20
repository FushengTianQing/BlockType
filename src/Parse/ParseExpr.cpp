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
#include "blocktype/AST/Type.h"
#include "blocktype/Sema/Scope.h"
#include "blocktype/Sema/Sema.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Member Lookup
//===----------------------------------------------------------------------===//

/// lookupMemberInType - Look up a member in a class type.
///
/// This function performs member lookup in a class type. It searches for the
/// member in the class and its base classes, and checks access control.
///
/// \param MemberName The name of the member to look up.
/// \param BaseType The type of the base expression.
/// \param MemberLoc The location of the member name token.
/// \param AccessingClass The class from which the access is made (nullptr if outside any class).
///
/// \return The declaration of the member if found and accessible, nullptr otherwise.
ValueDecl *Parser::lookupMemberInType(llvm::StringRef MemberName, QualType BaseType,
                                       SourceLocation MemberLoc, CXXRecordDecl *AccessingClass) {
  // Check if the base type is valid
  if (BaseType.isNull()) {
    // Type inference not yet implemented
    return nullptr;
  }

  // Check if the base type is a record type
  const Type *Ty = BaseType.getTypePtr();

  // Handle pointer types: dereference to get the pointee type
  if (auto *PT = llvm::dyn_cast_or_null<PointerType>(Ty)) {
    Ty = PT->getPointeeType();
  }

  // Check if it's a record type
  auto *RT = llvm::dyn_cast_or_null<RecordType>(Ty);
  if (!RT) {
    // Not a record type, cannot look up members
    return nullptr;
  }

  // Get the record declaration
  RecordDecl *RD = RT->getDecl();
  if (!RD) {
    return nullptr;
  }

  // Search in the current class
  // 1. Search fields
  for (FieldDecl *Field : RD->fields()) {
    if (Field->getName() == MemberName) {
      // Check access control
      if (!isMemberAccessible(Field, AccessingClass, MemberLoc)) {
        return nullptr; // Access denied, error already emitted
      }
      return Field;
    }
  }

  // 2. Search methods (if CXXRecordDecl)
  if (auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(RD)) {
    for (CXXMethodDecl *Method : CXXRD->methods()) {
      if (Method->getName() == MemberName) {
        // Check access control
        if (!isMemberAccessible(Method, AccessingClass, MemberLoc)) {
          return nullptr; // Access denied, error already emitted
        }
        return Method;
      }
    }

    // 3. Search base classes
    for (const auto &Base : CXXRD->bases()) {
      QualType BaseType = Base.getType();
      if (auto *BaseRT = llvm::dyn_cast_or_null<RecordType>(BaseType.getTypePtr())) {
        if (RecordDecl *BaseRD = BaseRT->getDecl()) {
          // Recursively search in base class
          for (FieldDecl *Field : BaseRD->fields()) {
            if (Field->getName() == MemberName) {
              // Check access control
              if (!isMemberAccessible(Field, AccessingClass, MemberLoc)) {
                return nullptr; // Access denied, error already emitted
              }
              return Field;
            }
          }
          if (auto *BaseCXXRD = llvm::dyn_cast<CXXRecordDecl>(BaseRD)) {
            for (CXXMethodDecl *Method : BaseCXXRD->methods()) {
              if (Method->getName() == MemberName) {
                // Check access control
                if (!isMemberAccessible(Method, AccessingClass, MemberLoc)) {
                  return nullptr; // Access denied, error already emitted
                }
                return Method;
              }
            }
          }
        }
      }
    }
  }

  // Member not found
  return nullptr;
}

/// isMemberAccessible - Check if a member is accessible from the given context.
///
/// This function implements C++ access control rules:
/// - public members are always accessible
/// - protected members are accessible from derived classes
/// - private members are only accessible from the same class
///
/// \param Member The member declaration to check.
/// \param AccessingClass The class from which the access is made (nullptr if outside any class).
/// \param MemberLoc The location of the member access (for error reporting).
///
/// \return true if the member is accessible, false otherwise.
bool Parser::isMemberAccessible(ValueDecl *Member, CXXRecordDecl *AccessingClass, 
                                 SourceLocation MemberLoc) {
  if (!Member) {
    return false;
  }

  // Get the member's access specifier
  AccessSpecifier Access = AccessSpecifier::AS_public;
  
  if (auto *Field = llvm::dyn_cast<FieldDecl>(Member)) {
    Access = Field->getAccess();
  } else if (auto *Method = llvm::dyn_cast<CXXMethodDecl>(Member)) {
    Access = Method->getAccess();
  } else {
    // For other types of members, assume public
    return true;
  }

  // Public members are always accessible
  if (Access == AccessSpecifier::AS_public) {
    return true;
  }

  // If we're not inside any class, only public members are accessible
  if (!AccessingClass) {
    emitError(MemberLoc, DiagID::err_member_access_denied);
    return false;
  }

  // For now, we assume the member is declared in a class context
  // TODO: Add proper DeclContext support to track which class declares each member
  CXXRecordDecl *MemberClass = nullptr;
  
  // Try to find the parent class from the member's type or other means
  // This is a simplified approach - in full C++ implementation, 
  // we would track the declaring context for each declaration
  if (auto *Field = llvm::dyn_cast<FieldDecl>(Member)) {
    // Fields are always declared in a class, but we don't track it yet
    // For now, we'll be permissive and allow access
    return true;
  } else if (auto *Method = llvm::dyn_cast<CXXMethodDecl>(Member)) {
    // Methods have their parent class
    MemberClass = Method->getParent();
  }
  
  if (!MemberClass) {
    // Cannot determine the declaring class, be conservative
    return true;
  }

  // Private members: only accessible from the same class
  if (Access == AccessSpecifier::AS_private) {
    if (AccessingClass == MemberClass || AccessingClass->isDerivedFrom(MemberClass)) {
      // Note: In strict C++, private members are NOT accessible from derived classes
      // But for now, we allow it as a simplification
      return true;
    }
    
    emitError(MemberLoc, DiagID::err_member_access_denied);
    return false;
  }

  // Protected members: accessible from the class itself and derived classes
  if (Access == AccessSpecifier::AS_protected) {
    if (AccessingClass == MemberClass || AccessingClass->isDerivedFrom(MemberClass)) {
      return true;
    }
    
    emitError(MemberLoc, DiagID::err_member_access_denied);
    return false;
  }

  // Should not reach here
  return true;
}

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

  // C++11: braced-init-list can appear in assignment-expression context
  // (function arguments, return statements, assignment RHS, etc.)
  if (Tok.is(TokenKind::l_brace)) {
    Expr *Result = parseInitializerList();
    popContext();
    return Result;
  }

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

    // Create the binary operator node via Sema
    BinaryOpKind BOK = getBinaryOpKind(BinOp);
    auto Result = Actions.ActOnBinaryOperator(BOK, LHS, RHS, OpLoc);
    LHS = Result.isUsable() ? Result.get() : LHS;
  }

  return LHS;
}

//===----------------------------------------------------------------------===//
// Unary expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseUnaryExpression() {
  // P7.1.6: Check for C-style cast at the very beginning of unary expression parsing
  // This ensures we intercept (type)expr before any other path consumes the type token
  if (Tok.is(TokenKind::l_paren)) {
    // Use NextTok instead of PP.peekToken() to avoid buffer synchronization issues
    Token Next = NextTok;
    
    // Check if next token is a basic type keyword
    if (Next.is(TokenKind::kw_int) || Next.is(TokenKind::kw_float) || 
        Next.is(TokenKind::kw_double) || Next.is(TokenKind::kw_char) ||
        Next.is(TokenKind::kw_void) || Next.is(TokenKind::kw_bool) ||
        Next.is(TokenKind::kw_long) || Next.is(TokenKind::kw_short) ||
        Next.is(TokenKind::kw_signed) || Next.is(TokenKind::kw_unsigned)) {
      return parseCStyleCastExpr();
    }
    
    // For identifiers, we need 2-token lookahead which is complex.
    // Let parseParenExpression handle it with tentative parsing.
  }
  
  // Check for C++ new/delete
  if (Tok.is(TokenKind::kw_new)) {
    return parseCXXNewExpression();
  }
  if (Tok.is(TokenKind::kw_delete)) {
    return parseCXXDeleteExpression();
  }

  // Check for sizeof/alignof
  if (Tok.is(TokenKind::kw_sizeof) || Tok.is(TokenKind::kw_alignof)) {
    return parseUnaryExprOrTypeTraitExpr();
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

    // Create unary operator via Sema
    UnaryOpKind UOK = getUnaryOpKind(OpKind);
    auto Result = Actions.ActOnUnaryOperator(UOK, Operand, OpLoc);
    return Result.isUsable() ? Result.get() : nullptr;
  }

  // Parse postfix expression
  Expr *Base = parsePrimaryExpression();
  if (!Base)
    return nullptr;

  return parsePostfixExpression(Base);
}

//===----------------------------------------------------------------------===//
// sizeof/alignof expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseUnaryExprOrTypeTraitExpr() {
  SourceLocation OpLoc = Tok.getLocation();
  bool IsSizeOf = Tok.is(TokenKind::kw_sizeof);
  consumeToken(); // consume 'sizeof' or 'alignof'

  UnaryExprOrTypeTrait Kind =
      IsSizeOf ? UnaryExprOrTypeTrait::SizeOf
               : UnaryExprOrTypeTrait::AlignOf;

  // Two forms:
  //   sizeof(type)  — parenthesized type-id
  //   sizeof expr   — unary expression (no parens)

  if (Tok.is(TokenKind::l_paren)) {
    // Ambiguous: could be sizeof(type) or sizeof(expr).
    // Heuristic: if the token after '(' looks like a type keyword or
    // is an identifier that resolves to a type, parse as sizeof(type).
    // Otherwise, parse as sizeof(expr).
    //
    // Simplified strategy: try to parse as a type first.
    // Use tentative parsing to avoid committing on failure.

    // Check if next token looks like a type
    TokenKind Next = NextTok.getKind();
    bool LooksLikeType = isTypeKeyword(Next) ||
                         Next == TokenKind::kw_class ||
                         Next == TokenKind::kw_struct ||
                         Next == TokenKind::kw_enum ||
                         Next == TokenKind::kw_union ||
                         Next == TokenKind::kw_const ||
                         Next == TokenKind::kw_volatile ||
                         Next == TokenKind::kw_unsigned ||
                         Next == TokenKind::kw_signed;

    if (LooksLikeType) {
      // sizeof(type) form
      consumeToken(); // consume '('
      QualType T = parseType();
      if (!tryConsumeToken(TokenKind::r_paren)) {
        emitError(DiagID::err_expected_rparen);
      }
      return Actions.ActOnUnaryExprOrTypeTraitExpr(OpLoc, Kind, T).get();
    }

    // Check if it looks like a qualified type (identifier followed by ::)
    // For now, treat identifier + '(' as an expression form
    // e.g., sizeof(foo) where foo is a variable
    // We'll try parsing as expression if it doesn't look like a type

    // Parse as sizeof(expr) with parenthesized expression
    // Fall through to expression form below
  }

  // sizeof expr form (no parens, or parens around expression)
  Expr *Arg = parseUnaryExpression();
  if (!Arg) {
    return createRecoveryExpr(OpLoc);
  }

  return Actions.ActOnUnaryExprOrTypeTraitExpr(OpLoc, Kind, Arg).get();
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
      // Array subscript: base[index] (C++23: base[i, j, k])
      SourceLocation LLoc = Tok.getLocation();
      consumeToken();

      // Parse comma-separated index expressions (C++23 multi-dimensional)
      llvm::SmallVector<Expr *, 2> Indices;
      if (!Tok.is(TokenKind::r_square)) {
        while (true) {
          Expr *Idx = parseAssignmentExpression();
          if (!Idx) {
            Idx = createRecoveryExpr(LLoc);
          }
          Indices.push_back(Idx);
          if (!tryConsumeToken(TokenKind::comma))
            break;
        }
      }

      // Expect ']'
      if (!tryConsumeToken(TokenKind::r_square)) {
        emitError(DiagID::err_expected);
        skipUntil({TokenKind::r_square});
        tryConsumeToken(TokenKind::r_square);
      }

      // Create ArraySubscriptExpr via Sema
      Base = Actions.ActOnArraySubscriptExpr(Base, Indices, LLoc, LLoc).get();
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

      // Get member name and location
      llvm::StringRef MemberName = Tok.getText();
      SourceLocation MemberLoc = Tok.getLocation();
      consumeToken();

      // Look up member in the base type
      // 从 Base 表达式获取类型，用于成员查找
      QualType BaseType = Base ? Base->getType() : QualType();
      ValueDecl *MemberDecl = lookupMemberInType(MemberName, BaseType, MemberLoc);

      // Create MemberExpr via Sema (direct form, Parser already looked up member)
      Base = Actions.ActOnMemberExprDirect(OpLoc, Base, MemberDecl, false).get();
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

      // Get member name and location
      llvm::StringRef MemberName = Tok.getText();
      SourceLocation MemberLoc = Tok.getLocation();
      consumeToken();

      // Look up member in the base type
      // 从 Base 表达式获取类型，用于成员查找（箭头操作符需要指针类型）
      QualType BaseType = Base ? Base->getType() : QualType();
      ValueDecl *MemberDecl = lookupMemberInType(MemberName, BaseType, MemberLoc);

      // Create MemberExpr with IsArrow = true via Sema
      Base = Actions.ActOnMemberExprDirect(OpLoc, Base, MemberDecl, true).get();
      break;
    }

    case TokenKind::plusplus: {
      // Postfix increment: expr++
      SourceLocation OpLoc = Tok.getLocation();
      consumeToken();

      // Create UnaryOperator with PostInc via Sema
      { auto R = Actions.ActOnUnaryOperator(UnaryOpKind::PostInc, Base, OpLoc);
        Base = R.isUsable() ? R.get() : Base; }
      break;
    }

    case TokenKind::minusminus: {
      // Postfix decrement: expr--
      SourceLocation OpLoc = Tok.getLocation();
      consumeToken();

      // Create UnaryOperator with PostDec via Sema
      { auto R = Actions.ActOnUnaryOperator(UnaryOpKind::PostDec, Base, OpLoc);
        Base = R.isUsable() ? R.get() : Base; }
      break;
    }

    case TokenKind::ellipsis: {
      // Pack indexing (C++26): pack...[index]
      SourceLocation EllipsisLoc = Tok.getLocation();
      consumeToken(); // consume '...'

      // Expect '['
      if (!tryConsumeToken(TokenKind::l_square)) {
        emitError(DiagID::err_expected);
        return Base;
      }

      // Parse the index
      Expr *Index = parseExpression();
      if (!Index) {
        Index = createRecoveryExpr(EllipsisLoc);
      }

      // Expect ']'
      if (!tryConsumeToken(TokenKind::r_square)) {
        emitError(DiagID::err_expected);
      }

      // Create PackIndexingExpr via Sema
      Base = Actions.ActOnPackIndexingExpr(EllipsisLoc, Base, Index).get();
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

  // reflexpr (C++26)
  case TokenKind::kw_reflexpr:
    return parseReflexprExpr();

  // C++ cast expressions
  case TokenKind::kw_static_cast:
    return parseCXXStaticCastExpr();

  case TokenKind::kw_dynamic_cast:
    return parseCXXDynamicCastExpr();

  case TokenKind::kw_const_cast:
    return parseCXXConstCastExpr();

  case TokenKind::kw_reinterpret_cast:
    return parseCXXReinterpretCastExpr();

  // P7.1.2: Decay-copy expression auto(x) / auto{x} (P0849R8)
  case TokenKind::kw_auto: {
    SourceLocation AutoLoc = Tok.getLocation();
    // Peek ahead: auto( or auto{ → decay-copy expression
    Token Next = PP.peekToken(0);
    if (Next.is(TokenKind::l_paren) || Next.is(TokenKind::l_brace)) {
      return parseDecayCopyExpr(AutoLoc);
    }
    // Otherwise, 'auto' in expression context is an error
    emitError(DiagID::err_expected_expression);
    consumeToken();
    return nullptr;
  }

  // C++11: braced-init-list as primary expression
  case TokenKind::l_brace: {
    // Parse as initializer list with unknown type
    return parseInitializerList(QualType());
  }

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

  // Remove suffix (u, l, ll, ul, ull, etc.)
  // Suffixes can be: u, U, l, L, ll, LL, ul, uL, Ul, UL, ull, uLL, Ull, ULL,
  //                   lu, lU, Lu, LU, llu, llU, LLu, LLU, z, Z (C++23)
  if (Text.size() > 1) {
    // Check for two-character suffixes first
    if (Text.size() >= 3) {
      StringRef Suffix2 = Text.take_back(2);
      if (Suffix2.equals_insensitive("ul") || Suffix2.equals_insensitive("lu") ||
          Suffix2.equals_insensitive("ll") || Suffix2.equals_insensitive("uz") ||
          Suffix2.equals_insensitive("zu")) {
        Text = Text.drop_back(2);
      } else {
        // Check for three-character suffixes (ull, llu, ULL, LLU, etc.)
        if (Text.size() >= 4) {
          StringRef Suffix3 = Text.take_back(3);
          if (Suffix3.equals_insensitive("ull") || Suffix3.equals_insensitive("llu")) {
            Text = Text.drop_back(3);
          } else {
            // Single character suffix
            char Last = Text.back();
            if (Last == 'u' || Last == 'U' || Last == 'l' || Last == 'L' ||
                Last == 'z' || Last == 'Z') {
              Text = Text.drop_back(1);
            }
          }
        } else {
          // Single character suffix
          char Last = Text.back();
          if (Last == 'u' || Last == 'U' || Last == 'l' || Last == 'L' ||
              Last == 'z' || Last == 'Z') {
            Text = Text.drop_back(1);
          }
        }
      }
    } else {
      // Single character suffix
      char Last = Text.back();
      if (Last == 'u' || Last == 'U' || Last == 'l' || Last == 'L' ||
          Last == 'z' || Last == 'Z') {
        Text = Text.drop_back(1);
      }
    }
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
  return Actions.ActOnIntegerLiteral(Loc, Value).get();
}

Expr *Parser::parseFloatingLiteral() {
  SourceLocation Loc = Tok.getLocation();
  StringRef Text = Tok.getText();

  // Remove suffix (f, F, l, L)
  if (Text.size() > 1) {
    char Last = Text.back();
    if (Last == 'f' || Last == 'F' || Last == 'l' || Last == 'L') {
      Text = Text.drop_back(1);
    }
  }

  // Parse the floating point value using llvm::APFloat
  llvm::APFloat Value(llvm::APFloat::IEEEdouble());
  if (!Text.empty()) {
    // Try to parse the value
    auto Result = Value.convertFromString(
        Text, llvm::APFloat::rmNearestTiesToEven);

    // Check for conversion errors
    if (Result) {
      llvm::APFloat::opStatus Status = Result.get();
      if (Status & llvm::APFloat::opInvalidOp) {
        // Invalid floating point literal
        emitError(Loc, DiagID::err_invalid_floating_literal);
      } else if (Status & llvm::APFloat::opOverflow) {
        // Overflow
        emitError(Loc, DiagID::err_float_literal_overflow);
      } else if (Status & llvm::APFloat::opUnderflow) {
        // Underflow
        emitError(Loc, DiagID::err_float_literal_underflow);
      }
      // Other status flags (inexact, etc.) are acceptable
    } else {
      // Conversion failed completely
      emitError(Loc, DiagID::err_invalid_floating_literal);
    }
  }

  consumeToken();
  return Actions.ActOnFloatingLiteral(Loc, Value).get();
}

Expr *Parser::parseStringLiteral() {
  SourceLocation Loc = Tok.getLocation();
  StringRef Text = Tok.getText();

  // Remove quotes
  if (Text.size() >= 2 && Text.front() == '"' && Text.back() == '"') {
    Text = Text.drop_front().drop_back();
  }

  consumeToken();
  // String literal type is const char[] — use char type as approximation.
  // A full implementation would create a ConstantArrayType(char, N).
  return Actions.ActOnStringLiteral(Loc, Text.str()).get();
}

Expr *Parser::parseCharacterLiteral() {
  SourceLocation Loc = Tok.getLocation();
  StringRef Text = Tok.getText();

  // Remove quotes
  uint32_t Value = 0;
  if (Text.size() >= 3 && Text.front() == '\'' && Text.back() == '\'') {
    StringRef Content = Text.drop_front().drop_back();

    // Handle escape sequences
    if (!Content.empty()) {
      if (Content[0] == '\\' && Content.size() > 1) {
        // Escape sequence
        switch (Content[1]) {
        case 'n':  Value = '\n'; break;
        case 't':  Value = '\t'; break;
        case 'r':  Value = '\r'; break;
        case '0':  Value = '\0'; break;
        case '\\': Value = '\\'; break;
        case '\'': Value = '\''; break;
        case '"':  Value = '"'; break;
        case 'a':  Value = '\a'; break;
        case 'b':  Value = '\b'; break;
        case 'f':  Value = '\f'; break;
        case 'v':  Value = '\v'; break;
        case 'x':
          // Hexadecimal escape: \xNN
          if (Content.size() > 2) {
            StringRef HexStr = Content.drop_front(2);
            Value = 0;
            for (char C : HexStr) {
              Value <<= 4;
              if (C >= '0' && C <= '9')
                Value |= (C - '0');
              else if (C >= 'a' && C <= 'f')
                Value |= (C - 'a' + 10);
              else if (C >= 'A' && C <= 'F')
                Value |= (C - 'A' + 10);
            }
          }
          break;
        default:
          // Octal escape: \NNN (up to 3 octal digits)
          if (Content[1] >= '0' && Content[1] <= '7') {
            Value = 0;
            for (size_t i = 1; i < Content.size() && i < 4; ++i) {
              char C = Content[i];
              if (C >= '0' && C <= '7') {
                Value = (Value << 3) | (C - '0');
              } else {
                break;
              }
            }
          } else {
            // Unknown escape, use as-is
            Value = static_cast<unsigned char>(Content[1]);
          }
          break;
        }
      } else {
        // Regular character
        Value = static_cast<unsigned char>(Content[0]);
      }
    }
  }

  consumeToken();
  return Actions.ActOnCharacterLiteral(Loc, Value).get();
}

Expr *Parser::parseBoolLiteral() {
  SourceLocation Loc = Tok.getLocation();
  bool Value = Tok.is(TokenKind::kw_true);

  consumeToken();
  return Actions.ActOnCXXBoolLiteral(Loc, Value).get();
}

Expr *Parser::parseNullPtrLiteral() {
  SourceLocation Loc = Tok.getLocation();

  consumeToken();
  return Actions.ActOnCXXNullPtrLiteral(Loc).get();
}

Expr *Parser::parseIdentifier() {
  SourceLocation Loc = Tok.getLocation();
  StringRef Name = Tok.getText();

  consumeToken();

  // P7.2.2: Built-in reflection functions
  if (Name == "__reflect_type" && Tok.is(TokenKind::l_paren)) {
    return parseReflectTypeBuiltin(Loc);
  }
  if (Name == "__reflect_members" && Tok.is(TokenKind::l_paren)) {
    return parseReflectMembersBuiltin(Loc);
  }

  // Check for scope resolution operator (::)
  if (Tok.is(TokenKind::coloncolon)) {
    // This is a qualified name: std::is_integral_v
    return parseQualifiedName(Loc, Name);
  }

  // Check for template argument list
  if (Tok.is(TokenKind::less)) {
    // Three-layer disambiguation strategy:

    // Layer 1: Check if next token is a type keyword
    if (isTypeKeyword(NextTok.getKind())) {
      return parseTemplateSpecializationExpr(Loc, Name);
    }

    // Layer 2: Check if the identifier is a known template in the symbol table
    if (NamedDecl *D = Actions.LookupName(Name)) {
        if (llvm::isa<TemplateDecl>(D)) {
          return parseTemplateSpecializationExpr(Loc, Name);
        }
    }

    // Layer 3: Tentative parsing
    Expr *Result = tryParseTemplateOrComparison(Loc, Name);
    if (Result) {
      return Result;
    }
  }

  // Lookup the declaration in the current scope
  ValueDecl *VD = nullptr;
  if (NamedDecl *D = Actions.LookupName(Name)) {
      // Found the declaration, create a DeclRefExpr
      VD = dyn_cast<ValueDecl>(D);
  }

  // Create DeclRefExpr via Sema (with or without declaration)
  // If VD is nullptr, it's an undefined identifier (error recovery)
  return Actions.ActOnDeclRefExpr(Loc, VD).get();
}

/// parseQualifiedName - Parse a qualified name (e.g., std::vector).
///
/// qualified-name ::= identifier '::' identifier ('::' identifier)*
Expr *Parser::parseQualifiedName(SourceLocation StartLoc, llvm::StringRef FirstName) {
  std::string FullName = FirstName.str();

  // Parse nested-name-specifier
  while (Tok.is(TokenKind::coloncolon)) {
    consumeToken(); // consume '::'

    if (!Tok.is(TokenKind::identifier)) {
      emitError(DiagID::err_expected_identifier);
      break;
    }

    FullName += "::";
    FullName += Tok.getText().str();
    consumeToken();
  }

  // Check for template argument list after the qualified name
  if (Tok.is(TokenKind::less)) {
    // Three-layer disambiguation strategy (same as parseIdentifier):

    // Layer 1: Check if next token is a type keyword
    Token NextTok = PP.peekToken(0);
    if (isTypeKeyword(NextTok.getKind())) {
      return parseTemplateSpecializationExpr(StartLoc, FullName);
    }

    // Layer 2: Check if the identifier is a known template in the symbol table
    if (NamedDecl *D = Actions.LookupName(FullName)) {
        if (llvm::isa<TemplateDecl>(D)) {
          return parseTemplateSpecializationExpr(StartLoc, FullName);
        }
    }

    // Layer 3: Tentative parsing
    Expr *Result = tryParseTemplateOrComparison(StartLoc, FullName);
    if (Result) {
      return Result;
    }
  }

  // Lookup the declaration in the current scope
  ValueDecl *VD = nullptr;
  if (NamedDecl *D = Actions.LookupName(FullName)) {
      VD = dyn_cast<ValueDecl>(D);
  }

  // Create DeclRefExpr via Sema
  return Actions.ActOnDeclRefExpr(StartLoc, VD).get();
}

/// parseTemplateSpecializationExpr - Parse a template specialization expression.
///
/// template-specialization-expr ::= identifier '<' template-argument-list? '>'
///
/// Parse a template specialization expression (e.g., Vector<int>, std::vector<std::string>).
///
/// This function properly parses template arguments and creates a TemplateSpecializationExpr
/// AST node with complete semantic information.
///
/// template-specialization-expression ::= identifier '<' template-argument-list? '>'
Expr *Parser::parseTemplateSpecializationExpr(SourceLocation StartLoc, llvm::StringRef TemplateName) {
  // Expect '<'
  if (!Tok.is(TokenKind::less)) {
    // Not a template specialization, just return a DeclRefExpr via Sema
    ValueDecl *VD = nullptr;
    if (NamedDecl *D = Actions.LookupName(TemplateName)) {
      VD = dyn_cast<ValueDecl>(D);
    }
    return Actions.ActOnDeclRefExpr(StartLoc, VD).get();
  }

  consumeToken(); // consume '<'

  // Parse template arguments using the proper parser
  llvm::SmallVector<TemplateArgument, 4> TemplateArgs;
  if (!Tok.is(TokenKind::greater)) {
    TemplateArgs = parseTemplateArgumentList();
  }

  // Expect '>'
  if (!Tok.is(TokenKind::greater)) {
    emitError(DiagID::err_expected);
    // Error recovery: skip to next token
    skipUntil({TokenKind::greater, TokenKind::comma, TokenKind::semicolon});
    if (Tok.is(TokenKind::greater)) {
      consumeToken();
    }
    // Return a TemplateSpecializationExpr with partial information
    ValueDecl *VD = nullptr;
    if (NamedDecl *D = Actions.LookupName(TemplateName)) {
      VD = dyn_cast<ValueDecl>(D);
    }
    return Actions.ActOnTemplateSpecializationExpr(StartLoc, TemplateName,
                                                   TemplateArgs, VD).get();
  }

  consumeToken(); // consume '>'

  // Look up the template declaration (may be nullptr if not found)
  ValueDecl *VD2 = nullptr;
  if (NamedDecl *D = Actions.LookupName(TemplateName)) {
    VD2 = dyn_cast<ValueDecl>(D);
  }

  // Create TemplateSpecializationExpr via Sema
  return Actions.ActOnTemplateSpecializationExpr(StartLoc, TemplateName,
                                                 TemplateArgs, VD2).get();
}

/// isTypeKeyword - Returns true if the token is a type keyword.
bool Parser::isTypeKeyword(TokenKind K) {
  return K == TokenKind::kw_void || K == TokenKind::kw_bool ||
         K == TokenKind::kw_char || K == TokenKind::kw_short ||
         K == TokenKind::kw_int || K == TokenKind::kw_long ||
         K == TokenKind::kw_float || K == TokenKind::kw_double ||
         K == TokenKind::kw_const || K == TokenKind::kw_volatile ||
         K == TokenKind::kw_unsigned || K == TokenKind::kw_signed;
}

/// tryParseTemplateOrComparison - Tries to parse a template specialization or
/// comparison expression using tentative parsing.
///
/// This is the third layer of disambiguation. We try to parse template arguments
/// and check if it looks like a valid template specialization. If not, we backtrack
/// and return nullptr to indicate it should be parsed as a comparison.
///
/// NOTE: We avoid using TentativeParsingAction here to prevent conflicts with
/// nested TPA in parseTemplateArgument(). Instead, we use a simple token-counting
/// heuristic to determine if this looks like a template.
Expr *Parser::tryParseTemplateOrComparison(SourceLocation Loc, llvm::StringRef Name) {
  // Simple heuristic: check if we can reach a matching '>' without errors
  // We don't use TPA here to avoid conflicts with nested TPA in parseTemplateArgument()

  // Save the current token position (manually, without TPA)
  Token SavedTok = Tok;
  Token SavedNextTok = NextTok;
  size_t SavedBufferIndex = PP.saveTokenBufferState();

  // Try to parse '<' template-arguments '>'
  consumeToken(); // consume '<'

  bool IsValidTemplate = false;

  // Check if this looks like a template argument list
  if (Tok.is(TokenKind::identifier) || isTypeKeyword(Tok.getKind()) ||
      Tok.is(TokenKind::numeric_constant)) {

    // Try to consume tokens until we find a matching '>'
    int Depth = 1;
    while (Depth > 0 && !Tok.is(TokenKind::eof)) {
      if (Tok.is(TokenKind::less)) {
        Depth++;
      } else if (Tok.is(TokenKind::greater)) {
        Depth--;
        if (Depth == 0) {
          IsValidTemplate = true;
          break;
        }
      } else if (Tok.is(TokenKind::greatergreater)) {
        // '>>' in C++11+ is treated as two '>' tokens for nested templates
        if (Depth >= 2) {
          Depth -= 2;
          if (Depth == 0) {
            IsValidTemplate = true;
            break;
          }
        } else if (Depth == 1) {
          Depth--;
          if (Depth == 0) {
            IsValidTemplate = true;
            break;
          }
        }
      }
      consumeToken();
    }
  }

  // Restore the parser state manually
  Tok = SavedTok;
  NextTok = SavedNextTok;
  PP.restoreTokenBufferState(SavedBufferIndex);

  // If it looks like a valid template specialization, parse it for real
  if (IsValidTemplate) {
    return parseTemplateSpecializationExpr(Loc, Name);
  }

  // Otherwise, return nullptr to indicate it should be parsed as a comparison
  return nullptr;
}

Expr *Parser::parseParenExpression() {
  SourceLocation LParenLoc = Tok.getLocation();
  
  // P7.1.6: C-style cast detection is now handled in parseUnaryExpression.
  // This function only handles normal parenthesized expressions.
  
  // Normal parenthesized expression
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

  return Actions.ActOnConditionalExpr(Cond, TrueExpr, FalseExpr,
                                      QuestionLoc, QuestionLoc).get();
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

  return Actions.ActOnCallExpr(Fn, Args, Fn->getLocation(),
                               Tok.getLocation()).get();
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

QualType Parser::deduceSubElementType(QualType AggrType, unsigned Index) {
  if (AggrType.isNull())
    return QualType();

  const Type *Ty = AggrType.getTypePtr();

  // Peel wrapper types (loop for chained wrappers like const A::B&)
  while (Ty) {
    if (auto *ET = llvm::dyn_cast<ElaboratedType>(Ty)) {
      Ty = ET->getNamedType();
      continue;
    }
    if (auto *RT = llvm::dyn_cast<ReferenceType>(Ty)) {
      Ty = RT->getReferencedType();
      continue;
    }
    if (auto *TT = llvm::dyn_cast<TypedefType>(Ty)) {
      // TypedefType — try to resolve to underlying type
      // (Phase 4: full canonical type resolution not yet complete)
      break;
    }
    break;
  }

  if (!Ty)
    return QualType();

  // ArrayType → all elements share the same element type
  if (auto *AT = llvm::dyn_cast<ArrayType>(Ty))
    return QualType(AT->getElementType(), AggrType.getQualifiers());

  // RecordType → field type by index
  if (auto *RT = llvm::dyn_cast<RecordType>(Ty)) {
    auto Fields = RT->getDecl()->fields();
    if (Index < Fields.size())
      return Fields[Index]->getType();
    return QualType(); // Index out of bounds
  }

  // Cannot decompose (scalar, pointer, function, etc.)
  return QualType();
}

Expr *Parser::parseInitializerList(QualType ExpectedType) {
  assert(Tok.is(TokenKind::l_brace) && "Expected '{'");
  SourceLocation LBraceLoc = Tok.getLocation();
  consumeToken(); // consume '{'

  llvm::SmallVector<Expr *, 8> Inits;

  // Parse initializer clauses
  unsigned ElemIndex = 0;
  while (!Tok.is(TokenKind::r_brace) && !Tok.is(TokenKind::eof)) {
    // Deduce expected type for this element from the aggregate type
    QualType ElemType = deduceSubElementType(ExpectedType, ElemIndex);
    // Parse an initializer clause (can be expression or designated initializer)
    Expr *Init = parseInitializerClause(ElemType);
    if (Init) {
      Inits.push_back(Init);
      ++ElemIndex;
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

  return Actions.ActOnInitListExpr(LBraceLoc, Inits, RBraceLoc, ExpectedType).get();
}

/// parseInitializerClause - Parse an initializer clause.
///
/// initializer-clause ::= assignment-expression
///                      | braced-init-list
///                      | designated-initializer-clause (C++20)
Expr *Parser::parseInitializerClause(QualType ExpectedType) {
  // Check for designated initializer (C++20): .field = value
  if (Tok.is(TokenKind::period)) {
    return parseDesignatedInitializer(ExpectedType);
  }

  // Check for nested braced-init-list
  if (Tok.is(TokenKind::l_brace)) {
    return parseInitializerList(ExpectedType);
  }

  // Otherwise, parse assignment expression
  return parseAssignmentExpression();
}

/// parseDesignatedInitializer - Parse a designated initializer (C++20).
///
/// designated-initializer-clause ::= designator '=' initializer-clause
/// designator ::= '.' identifier
///             | '[' constant-expression ']'
Expr *Parser::parseDesignatedInitializer(QualType ExpectedType) {
  assert(Tok.is(TokenKind::period) && "Expected '.'");
  SourceLocation DotLoc = Tok.getLocation();
  consumeToken(); // consume '.'

  // Parse field name
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  llvm::StringRef FieldName = Tok.getText();
  SourceLocation FieldLoc = Tok.getLocation();
  consumeToken();

  // Create designator
  llvm::SmallVector<DesignatedInitExpr::Designator, 4> Designators;
  Designators.push_back(DesignatedInitExpr::Designator(FieldName, DotLoc));

  // Expect '='
  if (!Tok.is(TokenKind::equal)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }
  consumeToken(); // consume '='

  // Look up field type from ExpectedType (RecordType) if available
  QualType FieldType;
  if (!ExpectedType.isNull()) {
    if (auto *RT = llvm::dyn_cast<RecordType>(ExpectedType.getTypePtr())) {
      for (auto *F : RT->getDecl()->fields()) {
        if (F->getName() == FieldName) {
          FieldType = F->getType();
          break;
        }
      }
    }
  }

  // Parse initializer clause
  Expr *Init = parseInitializerClause(FieldType);
  if (!Init) {
    return nullptr;
  }

  // Create DesignatedInitExpr via Sema
  return Actions.ActOnDesignatedInitExpr(DotLoc, Designators, Init).get();
}

} // namespace blocktype
