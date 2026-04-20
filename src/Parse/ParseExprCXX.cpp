//===--- ParseExprCXX.cpp - C++ Expression Parsing ----------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements C++ specific expression parsing for the Parser.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Parse/Parser.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/TemplateParameterList.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// new/delete expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseCXXNewExpression() {
  SourceLocation NewLoc = Tok.getLocation();
  consumeToken(); // consume 'new'

  // Check for placement new: new (args) T
  llvm::SmallVector<Expr *, 4> PlacementArgs;
  if (Tok.is(TokenKind::l_paren)) {
    consumeToken();
    if (!Tok.is(TokenKind::r_paren)) {
      // Parse placement arguments
      PlacementArgs = parseCallArguments();
    }
    if (!tryConsumeToken(TokenKind::r_paren)) {
      emitError(DiagID::err_expected_rparen);
    }
  }

  // Parse type
  QualType Type = parseType();
  if (Type.isNull()) {
    emitError(DiagID::err_expected_type);
    return createRecoveryExpr(NewLoc);
  }

  // Check for array new: new T[n]
  Expr *ArraySize = nullptr;
  if (Tok.is(TokenKind::l_square)) {
    consumeToken();
    ArraySize = parseExpression();
    if (!tryConsumeToken(TokenKind::r_square)) {
      emitError(DiagID::err_expected);
    }
  }

  // Parse initializer
  Expr *Initializer = nullptr;
  if (Tok.is(TokenKind::l_paren)) {
    consumeToken();
    if (!Tok.is(TokenKind::r_paren)) {
      // Parse initializer arguments
      auto Args = parseCallArguments();
      // Create CXXConstructExpr for direct initialization
      Initializer = Actions.ActOnCXXConstructExpr(NewLoc, Type, Args).get();
    }
    if (!tryConsumeToken(TokenKind::r_paren)) {
      emitError(DiagID::err_expected_rparen);
    }
  } else if (Tok.is(TokenKind::l_brace)) {
    // Brace initialization
    Initializer = parseInitializerList(Type);
  }

  // Create CXXNewExpr via Sema
  return Actions.ActOnCXXNewExprFactory(NewLoc, ArraySize, Initializer, Type).get();
}

Expr *Parser::parseCXXDeleteExpression() {
  SourceLocation DeleteLoc = Tok.getLocation();
  consumeToken(); // consume 'delete'

  // Check for array delete: delete[] ptr
  bool IsArrayDelete = false;
  if (Tok.is(TokenKind::l_square)) {
    consumeToken();
    IsArrayDelete = true;
    if (!tryConsumeToken(TokenKind::r_square)) {
      emitError(DiagID::err_expected);
    }
  }

  // Parse the argument (pointer to delete)
  Expr *Argument = parseUnaryExpression();
  if (Argument == nullptr) {
    Argument = createRecoveryExpr(DeleteLoc);
  }

  // 尝试从参数表达式推导被删除的元素类型
  QualType AllocatedType;
  if (Argument) {
    QualType ArgType = Argument->getType();
    if (auto *PtrType = llvm::dyn_cast<PointerType>(ArgType.getTypePtr())) {
      AllocatedType = PtrType->getPointeeType();
    }
  }

  // Create CXXDeleteExpr via Sema
  return Actions.ActOnCXXDeleteExprFactory(DeleteLoc, Argument, IsArrayDelete,
                                           AllocatedType).get();
}

//===----------------------------------------------------------------------===//
// this expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseCXXThisExpr() {
  SourceLocation ThisLoc = Tok.getLocation();
  consumeToken(); // consume 'this'

  return Actions.ActOnCXXThisExpr(ThisLoc).get();
}

//===----------------------------------------------------------------------===//
// throw expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseCXXThrowExpr() {
  SourceLocation ThrowLoc = Tok.getLocation();
  consumeToken(); // consume 'throw'

  // Parse optional operand
  Expr *Operand = nullptr;
  if (Tok.isNot(TokenKind::semicolon) && Tok.isNot(TokenKind::r_brace)) {
    Operand = parseExpression();
  }

  return Actions.ActOnCXXThrowExpr(ThrowLoc, Operand).get();
}

//===----------------------------------------------------------------------===//
// Lambda expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseLambdaExpression() {
  SourceLocation LambdaLoc = Tok.getLocation();

  // C++23: Parse leading attributes [[attr]] before [captures]
  AttributeListDecl *Attrs = nullptr;
  if (Tok.is(TokenKind::l_square) && NextTok.is(TokenKind::l_square)) {
    Attrs = parseAttributeSpecifier(LambdaLoc);
  }

  consumeToken(); // consume '['

  // Parse capture list
  llvm::SmallVector<LambdaCapture, 4> Captures;
  if (!Tok.is(TokenKind::r_square)) {
    Captures = parseLambdaCaptureList();
  }

  if (!tryConsumeToken(TokenKind::r_square)) {
    emitError(DiagID::err_expected);
    return createRecoveryExpr(LambdaLoc);
  }

  // C++20: Parse template parameters <...> after ] and before (
  TemplateParameterList *TemplateParams = nullptr;
  if (Tok.is(TokenKind::kw_template)) {
    SourceLocation TemplateLoc = Tok.getLocation();
    consumeToken(); // consume 'template'

    if (!tryConsumeToken(TokenKind::less)) {
      emitError(DiagID::err_expected);
      return createRecoveryExpr(LambdaLoc);
    }

    SourceLocation LAngleLoc = Tok.getLocation();
    llvm::SmallVector<NamedDecl *, 8> TParams;
    parseTemplateParameters(TParams);

    if (!tryConsumeToken(TokenKind::greater)) {
      emitError(DiagID::err_expected);
      skipUntil({TokenKind::greater, TokenKind::l_paren, TokenKind::l_brace});
      tryConsumeToken(TokenKind::greater);
    }
    SourceLocation RAngleLoc = Tok.getLocation();

    TemplateParams = new TemplateParameterList(TemplateLoc, LAngleLoc,
                                               RAngleLoc, TParams);
  }

  // Parse parameter list (optional)
  llvm::SmallVector<ParmVarDecl *, 4> Params;
  unsigned ParamIndex = 0;
  if (Tok.is(TokenKind::l_paren)) {
    consumeToken();
    if (!Tok.is(TokenKind::r_paren)) {
      // Parse parameter declarations
      while (Tok.isNot(TokenKind::r_paren) && Tok.isNot(TokenKind::eof)) {
        ParmVarDecl *Param = parseParameterDeclaration(ParamIndex);
        if (Param != nullptr) {
          Params.push_back(Param);
          ++ParamIndex;
        }
        if (!tryConsumeToken(TokenKind::comma)) {
          break;
        }
      }
    }
    if (!tryConsumeToken(TokenKind::r_paren)) {
      emitError(DiagID::err_expected_rparen);
    }
  }

  // C++23: Parse trailing attributes [[attr]] after (params)
  if (Tok.is(TokenKind::l_square) && NextTok.is(TokenKind::l_square)) {
    AttributeListDecl *TrailingAttrs = parseAttributeSpecifier(Tok.getLocation());
    // Merge with leading attrs or use as attrs
    if (!Attrs) {
      Attrs = TrailingAttrs;
    }
    // If both exist, trailing takes precedence (simplified)
  }

  // Parse mutable (optional)
  bool IsMutable = false;
  if (Tok.is(TokenKind::kw_mutable)) {
    IsMutable = true;
    consumeToken();
  }

  // Parse return type (optional)
  QualType ReturnType;
  if (Tok.is(TokenKind::arrow)) {
    consumeToken();
    ReturnType = parseType();
  }

  // Parse body
  if (!Tok.is(TokenKind::l_brace)) {
    emitError(DiagID::err_expected_lbrace);
    return createRecoveryExpr(LambdaLoc);
  }

  SourceLocation LBraceLoc = Tok.getLocation();
  
  // P2: Enter lambda scope (future: will be associated with closure class)
  // For now, just ensure proper scoping for lambda body
  Actions.PushScope(ScopeFlags::BlockScope);
  Stmt *Body = parseCompoundStatement();
  Actions.PopScope();
  
  if (Body == nullptr) {
    Body = Actions.ActOnNullStmt(Tok.getLocation()).get();
  }
  SourceLocation RBraceLoc = Tok.getLocation();

  return Actions.ActOnLambdaExpr(LambdaLoc, Captures, Params, Body,
                                 IsMutable, ReturnType, LBraceLoc, RBraceLoc,
                                 TemplateParams, Attrs).get();
}

llvm::SmallVector<LambdaCapture, 4> Parser::parseLambdaCaptureList() {
  llvm::SmallVector<LambdaCapture, 4> Captures;

  while (Tok.isNot(TokenKind::r_square)) {
    LambdaCapture Capture;
    SourceLocation CaptureLoc = Tok.getLocation();

    // Check for default capture
    if (Tok.is(TokenKind::amp)) {
      // & = capture by reference
      consumeToken();
      if (Tok.is(TokenKind::identifier)) {
        Capture.Kind = LambdaCapture::ByRef;
        Capture.Name = Tok.getText();
        Capture.Loc = CaptureLoc;
        consumeToken();
      }
    } else if (Tok.is(TokenKind::equal)) {
      // = = capture by copy (default)
      consumeToken();
      continue;
    } else if (Tok.is(TokenKind::identifier)) {
      // identifier
      Capture.Kind = LambdaCapture::ByCopy;
      Capture.Name = Tok.getText();
      Capture.Loc = CaptureLoc;
      consumeToken();

      // Check for init-capture: identifier = expr
      if (Tok.is(TokenKind::equal)) {
        consumeToken();
        Capture.Kind = LambdaCapture::InitCopy;
        Capture.InitExpr = parseExpression();
      }
    }

    Captures.push_back(Capture);

    // Check for comma
    if (!tryConsumeToken(TokenKind::comma)) {
      break;
    }
  }

  return Captures;
}

//===----------------------------------------------------------------------===//
// Fold expression parsing (C++17)
//===----------------------------------------------------------------------===//

Expr *Parser::parseFoldExpression() {
  SourceLocation FoldLoc = Tok.getLocation();
  consumeToken(); // consume '('

  Expr *LHS = nullptr;
  Expr *RHS = nullptr;
  Expr *Pattern = nullptr;
  BinaryOpKind Operator = BinaryOpKind::Add;
  bool IsRightFold = false;

  // Check for unary left fold: (... op pack)
  if (Tok.is(TokenKind::ellipsis)) {
    consumeToken();

    Operator = getBinaryOpKind(Tok.getKind());
    consumeToken();

    RHS = parseExpression();
    Pattern = RHS;
    IsRightFold = false;
  } else {
    // Parse first expression (pack or init)
    LHS = parseExpression();

    // Expect ...
    if (!Tok.is(TokenKind::ellipsis)) {
      emitError(DiagID::err_expected);
      return createRecoveryExpr(FoldLoc);
    }
    consumeToken();

    // Parse operator
    Operator = getBinaryOpKind(Tok.getKind());
    consumeToken();

    // Check for second ... (unary right fold) or expression (binary fold)
    if (Tok.is(TokenKind::ellipsis)) {
      // Unary right fold: (pack op ...)
      consumeToken();
      Pattern = LHS;
      IsRightFold = true;
    } else {
      // Binary fold: (pack op ... op init) or (init op ... op pack)
      RHS = parseExpression();
      Pattern = LHS;
      IsRightFold = true;
    }
  }

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  return Actions.ActOnCXXFoldExpr(FoldLoc, LHS, RHS, Pattern, Operator,
                                   IsRightFold).get();
}

//===----------------------------------------------------------------------===//
// Requires expression parsing (C++20)
//===----------------------------------------------------------------------===//

/// Parse the optional parameter list of a requires expression.
void Parser::parseRequiresExpressionParameterList() {
  if (!Tok.is(TokenKind::l_paren)) {
    return;
  }
  
  consumeToken();
  if (!Tok.is(TokenKind::r_paren)) {
    // Skip to matching ')'
    unsigned Depth = 1;
    while (Depth > 0 && !Tok.is(TokenKind::eof)) {
      if (Tok.is(TokenKind::l_paren)) {
        ++Depth;
      } else if (Tok.is(TokenKind::r_paren)) {
        --Depth;
      }
      if (Depth > 0) {
        consumeToken();
      }
    }
  }
  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }
}

/// Parse a single requirement in a requires expression body.
Requirement *Parser::parseRequirement() {
  // Type requirement: typename T
  if (Tok.is(TokenKind::kw_typename)) {
    consumeToken();
    QualType TypeReq = parseType();
    if (!tryConsumeToken(TokenKind::semicolon)) {
      emitError(DiagID::err_expected);
    }
    return new TypeRequirement(TypeReq, Tok.getLocation());
  }

  // Nested requirement: requires constraint-expression
  if (Tok.is(TokenKind::kw_requires)) {
    SourceLocation RequiresLoc = Tok.getLocation();
    consumeToken();
    Expr *Constraint = parseExpression();
    if (!tryConsumeToken(TokenKind::semicolon)) {
      emitError(DiagID::err_expected);
    }
    return new NestedRequirement(Constraint, RequiresLoc);
  }

  // Compound requirement: { statement-seq } noexcept? return-type-requirement?
  if (Tok.is(TokenKind::l_brace)) {
    SourceLocation LBraceLoc = Tok.getLocation();
    consumeToken();

    // Parse expression/statement sequence
    Expr *Expression = nullptr;
    Stmt *Body = nullptr;

    // In compound requirements, the brace-enclosed content is an expression
    // (C++20 [expr.prim.req.compound])
    if (Tok.isNot(TokenKind::r_brace)) {
      Expression = parseExpression();
    }

    if (!tryConsumeToken(TokenKind::r_brace)) {
      emitError(DiagID::err_expected_rbrace);
    }

    // Check for noexcept
    bool IsNoexcept = false;
    if (Tok.is(TokenKind::kw_noexcept)) {
      IsNoexcept = true;
      consumeToken();
    }

    // Check for return type requirement: -> type
    QualType ReturnType;
    if (Tok.is(TokenKind::arrow)) {
      consumeToken();
      ReturnType = parseType();
    }

    if (!tryConsumeToken(TokenKind::semicolon)) {
      emitError(DiagID::err_expected);
    }

    return new CompoundRequirement(Expression, Body, IsNoexcept, ReturnType, LBraceLoc);
  }

  // Simple requirement: expression;
  Expr *ExprReq = parseExpression();
  if (ExprReq == nullptr) {
    return nullptr;
  }

  if (!tryConsumeToken(TokenKind::semicolon)) {
    // Skip to semicolon or brace
    while (Tok.isNot(TokenKind::semicolon) && Tok.isNot(TokenKind::r_brace) &&
           Tok.isNot(TokenKind::eof)) {
      consumeToken();
    }
    if (Tok.is(TokenKind::semicolon)) {
      consumeToken();
    }
  }

  return new ExprRequirement(ExprReq, false, Tok.getLocation());
}

Expr *Parser::parseRequiresExpression() {
  SourceLocation RequiresLoc = Tok.getLocation();
  consumeToken(); // consume 'requires'

  // Parse optional parameter list
  parseRequiresExpressionParameterList();

  // Parse requirement body
  if (!Tok.is(TokenKind::l_brace)) {
    emitError(DiagID::err_expected_lbrace);
    return createRecoveryExpr(RequiresLoc);
  }

  SourceLocation LBraceLoc = Tok.getLocation();
  consumeToken(); // consume '{'

  // Parse requirements
  llvm::SmallVector<Requirement *, 4> Requirements;
  while (Tok.isNot(TokenKind::r_brace) && Tok.isNot(TokenKind::eof)) {
    Requirement *Req = parseRequirement();
    if (Req != nullptr) {
      Requirements.push_back(Req);
    }
  }

  SourceLocation RBraceLoc = Tok.getLocation();
  if (!tryConsumeToken(TokenKind::r_brace)) {
    emitError(DiagID::err_expected_rbrace);
  }

  return Actions.ActOnRequiresExpr(RequiresLoc, Requirements, RequiresLoc,
                                   RBraceLoc).get();
}

//===----------------------------------------------------------------------===//
// C-style cast expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseCStyleCastExpr() {
  SourceLocation LParenLoc = Tok.getLocation();
  consumeToken(); // consume '('

  // P7.1.6: Parse a simple type for C-style cast
  // Instead of using full parseType(), use a simplified version
  // that only handles basic type keywords
  QualType CastType;
  
  // Parse CVR qualifiers
  Qualifier Quals = Qualifier::None;
  while (Tok.is(TokenKind::kw_const) || Tok.is(TokenKind::kw_volatile)) {
    if (Tok.is(TokenKind::kw_const)) {
      Quals = Quals | Qualifier::Const;
      consumeToken();
    }
    if (Tok.is(TokenKind::kw_volatile)) {
      Quals = Quals | Qualifier::Volatile;
      consumeToken();
    }
  }
  
  // Parse base type
  CastType = parseBuiltinType();
  
  if (CastType.isNull()) {
    // Try identifier (user-defined type)
    if (Tok.is(TokenKind::identifier)) {
      llvm::StringRef TypeName = Tok.getText();
      SourceLocation TypeNameLoc = Tok.getLocation();
      consumeToken();
      
      // Lookup the type
      if (NamedDecl *LookupDecl = Actions.LookupName(TypeName)) {
        if (auto *RecDecl = llvm::dyn_cast<RecordDecl>(LookupDecl)) {
          CastType = Context.getRecordType(RecDecl);
        } else if (auto *TypedefDeclPtr = llvm::dyn_cast<TypedefDecl>(LookupDecl)) {
          CastType = TypedefDeclPtr->getUnderlyingType();
        }
      }
      
      if (CastType.isNull()) {
        emitError(DiagID::err_expected_type);
        return createRecoveryExpr(LParenLoc);
      }
    } else {
      emitError(DiagID::err_expected_type);
      return createRecoveryExpr(LParenLoc);
    }
  }
  
  // Apply qualifiers
  if (!CastType.isNull() && Quals != Qualifier::None) {
    CastType = Context.getQualifiedType(CastType.getTypePtr(), Quals);
  }
  
  // Handle pointer types (e.g., int*, int**)
  while (Tok.is(TokenKind::star)) {
    consumeToken(); // consume '*'
    
    // Parse CVR qualifiers for pointer
    Qualifier PtrQuals = Qualifier::None;
    while (Tok.is(TokenKind::kw_const) || Tok.is(TokenKind::kw_volatile)) {
      if (Tok.is(TokenKind::kw_const)) {
        PtrQuals = PtrQuals | Qualifier::Const;
        consumeToken();
      }
      if (Tok.is(TokenKind::kw_volatile)) {
        PtrQuals = PtrQuals | Qualifier::Volatile;
        consumeToken();
      }
    }
    
    // Create pointer type
    CastType = Context.getPointerType(CastType.getTypePtr());
    
    // Apply pointer qualifiers
    if (PtrQuals != Qualifier::None) {
      CastType = Context.getQualifiedType(CastType.getTypePtr(), PtrQuals);
    }
  }
  
  // Handle pointer types (e.g., int*, int**)
  while (Tok.is(TokenKind::star)) {
    consumeToken(); // consume '*'
    
    // Parse CVR qualifiers for pointer
    Qualifier PtrQuals = Qualifier::None;
    while (Tok.is(TokenKind::kw_const) || Tok.is(TokenKind::kw_volatile)) {
      if (Tok.is(TokenKind::kw_const)) {
        PtrQuals = PtrQuals | Qualifier::Const;
        consumeToken();
      }
      if (Tok.is(TokenKind::kw_volatile)) {
        PtrQuals = PtrQuals | Qualifier::Volatile;
        consumeToken();
      }
    }
    
    // Create pointer type
    CastType = Context.getPointerType(CastType.getTypePtr());
    
    // Apply pointer qualifiers
    if (PtrQuals != Qualifier::None) {
      CastType = Context.getQualifiedType(CastType.getTypePtr(), PtrQuals);
    }
  }

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
    return createRecoveryExpr(LParenLoc);
  }

  // Parse the sub-expression
  Expr *SubExpr = parseUnaryExpression();
  if (SubExpr == nullptr) {
    SubExpr = createRecoveryExpr(LParenLoc);
  }

  return Actions.ActOnCastExpr(CastType, SubExpr, LParenLoc, LParenLoc).get();
}

//===----------------------------------------------------------------------===//
// C++ cast expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parseCXXStaticCastExpr() {
  SourceLocation CastLoc = Tok.getLocation();
  consumeToken(); // consume 'static_cast'

  // Parse '<'
  if (!tryConsumeToken(TokenKind::less)) {
    emitError(DiagID::err_expected);
    return createRecoveryExpr(CastLoc);
  }

  // Parse type
  QualType CastType = parseType();
  if (CastType.isNull()) {
    emitError(DiagID::err_expected_type);
    return createRecoveryExpr(CastLoc);
  }

  // Parse '>'
  if (!tryConsumeToken(TokenKind::greater)) {
    emitError(DiagID::err_expected);
    return createRecoveryExpr(CastLoc);
  }

  // Parse '('
  if (!tryConsumeToken(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return createRecoveryExpr(CastLoc);
  }

  // Parse the sub-expression
  Expr *SubExpr = parseExpression();
  if (SubExpr == nullptr) {
    SubExpr = createRecoveryExpr(CastLoc);
  }

  // Parse ')'
  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  return Actions.ActOnCXXNamedCastExprWithType(CastLoc, SubExpr, CastType, "static_cast").get();
}

Expr *Parser::parseCXXDynamicCastExpr() {
  SourceLocation CastLoc = Tok.getLocation();
  consumeToken(); // consume 'dynamic_cast'

  // Parse '<'
  if (!tryConsumeToken(TokenKind::less)) {
    emitError(DiagID::err_expected);
    return createRecoveryExpr(CastLoc);
  }

  // Parse type
  QualType CastType = parseType();
  if (CastType.isNull()) {
    emitError(DiagID::err_expected_type);
    return createRecoveryExpr(CastLoc);
  }

  // Parse '>'
  if (!tryConsumeToken(TokenKind::greater)) {
    emitError(DiagID::err_expected);
    return createRecoveryExpr(CastLoc);
  }

  // Parse '('
  if (!tryConsumeToken(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return createRecoveryExpr(CastLoc);
  }

  // Parse the sub-expression
  Expr *SubExpr = parseExpression();
  if (SubExpr == nullptr) {
    SubExpr = createRecoveryExpr(CastLoc);
  }

  // Parse ')'
  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  return Actions.ActOnCXXNamedCastExprWithType(CastLoc, SubExpr, CastType,
                                               "dynamic_cast").get();
}

Expr *Parser::parseCXXConstCastExpr() {
  SourceLocation CastLoc = Tok.getLocation();
  consumeToken(); // consume 'const_cast'

  // Parse '<'
  if (!tryConsumeToken(TokenKind::less)) {
    emitError(DiagID::err_expected);
    return createRecoveryExpr(CastLoc);
  }

  // Parse type
  QualType CastType = parseType();
  if (CastType.isNull()) {
    emitError(DiagID::err_expected_type);
    return createRecoveryExpr(CastLoc);
  }

  // Parse '>'
  if (!tryConsumeToken(TokenKind::greater)) {
    emitError(DiagID::err_expected);
    return createRecoveryExpr(CastLoc);
  }

  // Parse '('
  if (!tryConsumeToken(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return createRecoveryExpr(CastLoc);
  }

  // Parse the sub-expression
  Expr *SubExpr = parseExpression();
  if (SubExpr == nullptr) {
    SubExpr = createRecoveryExpr(CastLoc);
  }

  // Parse ')'
  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  return Actions.ActOnCXXNamedCastExprWithType(CastLoc, SubExpr, CastType, "const_cast").get();
}

Expr *Parser::parseCXXReinterpretCastExpr() {
  SourceLocation CastLoc = Tok.getLocation();
  consumeToken(); // consume 'reinterpret_cast'

  // Parse '<'
  if (!tryConsumeToken(TokenKind::less)) {
    emitError(DiagID::err_expected);
    return createRecoveryExpr(CastLoc);
  }

  // Parse type
  QualType CastType = parseType();
  if (CastType.isNull()) {
    emitError(DiagID::err_expected_type);
    return createRecoveryExpr(CastLoc);
  }

  // Parse '>'
  if (!tryConsumeToken(TokenKind::greater)) {
    emitError(DiagID::err_expected);
    return createRecoveryExpr(CastLoc);
  }

  // Parse '('
  if (!tryConsumeToken(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return createRecoveryExpr(CastLoc);
  }

  // Parse the sub-expression
  Expr *SubExpr = parseExpression();
  if (SubExpr == nullptr) {
    SubExpr = createRecoveryExpr(CastLoc);
  }

  // Parse ')'
  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  return Actions.ActOnCXXNamedCastExprWithType(CastLoc, SubExpr, CastType, "reinterpret_cast").get();
}

//===----------------------------------------------------------------------===//
// C++26 expression parsing
//===----------------------------------------------------------------------===//

Expr *Parser::parsePackIndexingExpr() {
  SourceLocation PackLoc = Tok.getLocation();

  // Parse the pack expression
  Expr *Pack = parsePrimaryExpression();
  if (Pack == nullptr) {
    Pack = createRecoveryExpr(PackLoc);
  }

  // Expect ...[
  if (!Tok.is(TokenKind::ellipsis)) {
    emitError(DiagID::err_expected);
    return Pack;
  }
  consumeToken(); // consume '...'

  if (!tryConsumeToken(TokenKind::l_square)) {
    emitError(DiagID::err_expected);
    return Pack;
  }

  // Parse the index
  Expr *Index = parseExpression();
  if (Index == nullptr) {
    Index = createRecoveryExpr(PackLoc);
  }

  if (!tryConsumeToken(TokenKind::r_square)) {
    emitError(DiagID::err_expected);
  }

  return Actions.ActOnPackIndexingExpr(PackLoc, Pack, Index).get();
}

/// parseReflexprExpr - Parse a C++26 reflexpr expression.
///
/// Grammar:
///   reflexpr-expression ::= 'reflexpr' '(' type-id ')'
///                         | 'reflexpr' '(' expression ')'
///
/// The parser first tries to parse a type-id. If the next token
/// looks like a type specifier, parse as type; otherwise parse as
/// expression. This matches how Clang handles similar constructs
/// (sizeof, alignof).
Expr *Parser::parseReflexprExpr() {
  SourceLocation ReflexprLoc = Tok.getLocation();
  consumeToken(); // consume 'reflexpr'

  // Parse '('
  if (!tryConsumeToken(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return createRecoveryExpr(ReflexprLoc);
  }

  // P7.2.1 P1-fix: Warn about redundant parentheses around the operand.
  // E.g. reflexpr((int)) or reflexpr((x)) — the inner parens are redundant.
  if (Tok.is(TokenKind::l_paren)) {
    Diags.report(Tok.getLocation(), DiagID::warn_reflexpr_paren);
  }

  // Heuristic: check if the next token could start a type-id.
  // Type-id tokens: keyword types (int, void, class, struct, enum, ...),
  // or an identifier that resolves to a type name via symbol lookup.
  // Otherwise fall back to expression parsing.
  bool IsType = false;
  TokenKind Next = Tok.getKind();

  // Check for known type-starting tokens
  if (isTypeKeyword(Next) ||
      Next == TokenKind::kw_class ||
      Next == TokenKind::kw_struct ||
      Next == TokenKind::kw_enum ||
      Next == TokenKind::kw_union ||
      Next == TokenKind::kw_const ||
      Next == TokenKind::kw_volatile ||
      Next == TokenKind::kw_unsigned ||
      Next == TokenKind::kw_signed ||
      Next == TokenKind::kw_auto ||
      Next == TokenKind::kw_typename ||
      Next == TokenKind::kw_wchar_t ||
      Next == TokenKind::kw_char8_t ||
      Next == TokenKind::kw_char16_t ||
      Next == TokenKind::kw_char32_t) {
    IsType = true;
  }

  // P7.2.1 P0-fix: identifier token may be a user-defined type name.
  // Check via symbol table lookup whether the identifier resolves to a
  // type declaration (RecordDecl, TypedefDecl, TypeAliasDecl, etc.).
  // This handles cases like reflexpr(MyClass) where MyClass is a struct.
  if (!IsType && Next == TokenKind::identifier) {
    llvm::StringRef Name = Tok.getText();
    if (NamedDecl *D = Actions.LookupName(Name)) {
      // Check if the declaration is a type (record, typedef, type alias,
      // template type parameter, enum, etc.)
      if (llvm::isa<RecordDecl>(D) ||
          llvm::isa<TypedefDecl>(D) ||
          llvm::isa<TypeAliasDecl>(D) ||
          llvm::isa<TemplateTypeParmDecl>(D) ||
          llvm::isa<EnumDecl>(D)) {
        IsType = true;
      }
    }
  }

  Expr *Result = nullptr;

  if (IsType) {
    // Parse as type-id
    QualType T = parseType();
    if (!T.isNull()) {
      Result = Actions.ActOnReflexprTypeExpr(ReflexprLoc, T).get();
    } else {
      // Type parsing failed, try as expression
      Result = nullptr;
    }
  }

  if (!IsType || Result == nullptr) {
    // Parse as expression
    Expr *Arg = parseExpression();
    if (Arg == nullptr) {
      Arg = createRecoveryExpr(ReflexprLoc);
    }
    Result = Actions.ActOnReflexprExpr(ReflexprLoc, Arg).get();
  }

  // Parse ')'
  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  return Result ? Result : createRecoveryExpr(ReflexprLoc);
}

//===----------------------------------------------------------------------===//
// P7.2.2: Built-in reflection functions
//===----------------------------------------------------------------------===//

/// parseReflectTypeBuiltin - Parse __reflect_type(expr)
///
/// __reflect_type(expr) returns reflexpr(decltype(expr))
Expr *Parser::parseReflectTypeBuiltin(SourceLocation Loc) {
  // '(' already confirmed by caller
  consumeToken(); // consume '('

  Expr *Arg = parseExpression();
  if (!Arg) {
    Arg = createRecoveryExpr(Loc);
  }

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  return Actions.ActOnReflectTypeBuiltin(Loc, Arg).get();
}

/// parseReflectMembersBuiltin - Parse __reflect_members(type)
///
/// __reflect_members(type-id) returns a reflection of all members of the type
Expr *Parser::parseReflectMembersBuiltin(SourceLocation Loc) {
  // '(' already confirmed by caller
  consumeToken(); // consume '('

  // Try to parse as a type first
  QualType T = parseType();
  if (T.isNull()) {
    // Fall back to expression
    Expr *Arg = parseExpression();
    if (Arg && !Arg->getType().isNull()) {
      T = Arg->getType();
    }
  }

  if (!tryConsumeToken(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
  }

  if (T.isNull()) {
    return createRecoveryExpr(Loc);
  }

  return Actions.ActOnReflectMembersBuiltin(Loc, T).get();
}

//===----------------------------------------------------------------------===//
// P7.1.2: Decay-copy expression parsing (P0849R8)
//===----------------------------------------------------------------------===//

Expr *Parser::parseDecayCopyExpr(SourceLocation AutoLoc) {
  // Syntax: auto( expression ) or auto{ expression }
  assert(Tok.is(TokenKind::kw_auto) && "Expected 'auto'");
  consumeToken(); // consume 'auto'

  bool IsDirectInit = false;
  if (Tok.is(TokenKind::l_paren)) {
    IsDirectInit = false;  // auto(expr) — copy-initialization
    consumeToken(); // consume '('
  } else if (Tok.is(TokenKind::l_brace)) {
    IsDirectInit = true;   // auto{expr} — direct-initialization
    consumeToken(); // consume '{'
  } else {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  // Parse the sub-expression
  Expr *SubExpr = parseExpression();
  if (!SubExpr) {
    emitError(DiagID::err_expected_expression);
    return nullptr;
  }

  // Consume closing delimiter
  if (IsDirectInit) {
    // auto{expr} — expect '}'
    if (!Tok.is(TokenKind::r_brace)) {
      emitError(DiagID::err_expected_rbrace);
      return nullptr;
    }
    consumeToken(); // consume '}'
  } else {
    // auto(expr) — expect ')'
    if (!Tok.is(TokenKind::r_paren)) {
      emitError(DiagID::err_expected_rparen);
      return nullptr;
    }
    consumeToken(); // consume ')'
  }

  return Actions.ActOnDecayCopyExpr(AutoLoc, SubExpr, IsDirectInit).get();
}

} // namespace blocktype
