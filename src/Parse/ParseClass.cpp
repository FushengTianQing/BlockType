//===--- ParseClass.cpp - Class and Struct Parsing ------------------------===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements class, struct, union, and member parsing for the
// BlockType parser.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Parse/Parser.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/TemplateParameterList.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Lex/Token.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Class / Struct / Union Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseClassDeclaration - Parse a class declaration.
///
/// class-specifier ::= 'class' identifier? '{' member-specification? '}'
///                   | 'class' identifier base-clause? '{' member-specification? '}'
CXXRecordDecl *Parser::parseClassDeclaration(SourceLocation ClassLoc,
    llvm::SmallVector<TemplateArgument, 4> *ParsedTemplateArgs) {
  // Parse class name (optional)
  llvm::StringRef Name;
  SourceLocation NameLoc;

  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();
  }

  // Check for template specialization arguments (e.g., Vector<T*>)
  // This is used for partial and explicit specializations
  llvm::SmallVector<TemplateArgument, 4> TemplateArgs;
  if (Tok.is(TokenKind::less)) {
    consumeToken(); // consume '<'
    // Parse template argument list (stops at '>')
    if (!Tok.is(TokenKind::greater)) {
      TemplateArgs = parseTemplateArgumentList();
    }
    // Consume closing '>'
    if (Tok.is(TokenKind::greater)) {
      consumeToken();
    } else if (Tok.is(TokenKind::greatergreater)) {
      // Handle >> as two separate > tokens (common in nested templates)
      // Consume as one >> and let the outer context handle the remaining >
      consumeToken();
    } else {
      emitError(DiagID::err_expected);
    }
  }

  // Pass template arguments to caller if requested
  if (ParsedTemplateArgs) {
    *ParsedTemplateArgs = std::move(TemplateArgs);
  }

  // Create CXXRecordDecl via Sema
  CXXRecordDecl *Class = llvm::cast<CXXRecordDecl>(
      Actions.ActOnCXXRecordDeclFactory(NameLoc, Name, TagDecl::TK_class).get());

  // Class registered by Sema

  // Parse base clause if present
  if (Tok.is(TokenKind::colon)) {
    if (!parseBaseClause(Class)) {
      // Error recovery: skip to '{'
      skipUntil({TokenKind::l_brace});
    }
  }

  // Parse class body
  if (!Tok.is(TokenKind::l_brace)) {
    emitError(DiagID::err_expected_lbrace);
    return Class;
  }

  consumeToken(); // consume '{'

  // Enter class scope
  Actions.PushScope(ScopeFlags::ClassScope);
  parseClassBody(Class);
  Actions.PopScope();

  if (!Tok.is(TokenKind::r_brace)) {
    emitError(DiagID::err_expected_rbrace);
    return Class;
  }

  consumeToken(); // consume '}'
  Class->setCompleteDefinition();
  return Class;
}

/// parseStructDeclaration - Parse a struct declaration.
///
/// struct-specifier ::= 'struct' identifier? '{' member-specification? '}'
///                    | 'struct' identifier base-clause? '{' member-specification? '}'
CXXRecordDecl *Parser::parseStructDeclaration(SourceLocation StructLoc,
    llvm::SmallVector<TemplateArgument, 4> *ParsedTemplateArgs) {
  // Parse struct name (optional)
  llvm::StringRef Name;
  SourceLocation NameLoc;

  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();
  }

  // Check for template specialization arguments (e.g., Pair<int, double>)
  llvm::SmallVector<TemplateArgument, 4> TemplateArgs;
  if (Tok.is(TokenKind::less)) {
    consumeToken(); // consume '<'
    if (!Tok.is(TokenKind::greater)) {
      TemplateArgs = parseTemplateArgumentList();
    }
    // Consume closing '>'
    if (Tok.is(TokenKind::greater)) {
      consumeToken();
    } else if (Tok.is(TokenKind::greatergreater)) {
      consumeToken();
    } else {
      emitError(DiagID::err_expected);
    }
  }

  // Pass template arguments to caller if requested
  if (ParsedTemplateArgs) {
    *ParsedTemplateArgs = std::move(TemplateArgs);
  }

  // Create CXXRecordDecl (struct has public default access)
  CXXRecordDecl *Struct = llvm::cast<CXXRecordDecl>(
      Actions.ActOnCXXRecordDeclFactory(NameLoc, Name, TagDecl::TK_struct).get());

  // Struct registered by Sema

  // Parse base clause if present (struct can inherit)
  if (Tok.is(TokenKind::colon)) {
    if (!parseBaseClause(Struct)) {
      // Error recovery: skip to '{'
      skipUntil({TokenKind::l_brace});
    }
  }

  // Parse struct body
  if (!Tok.is(TokenKind::l_brace)) {
    emitError(DiagID::err_expected_lbrace);
    return Struct;
  }

  consumeToken(); // consume '{'

  // Parse members using parseClassBody
  parseClassBody(Struct);

  if (!Tok.is(TokenKind::r_brace)) {
    emitError(DiagID::err_expected_rbrace);
    return Struct;
  }

  consumeToken(); // consume '}'
  Struct->setCompleteDefinition();
  return Struct;
}
/// parseUnionDeclaration - Parse a union declaration.
///
/// union-specifier ::= 'union' identifier? '{' member-specification? '}'
CXXRecordDecl *Parser::parseUnionDeclaration(SourceLocation UnionLoc) {
  // Parse union name (optional)
  llvm::StringRef Name;
  SourceLocation NameLoc;

  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();
  }

  // Create CXXRecordDecl (union has public default access)
  CXXRecordDecl *Union = llvm::cast<CXXRecordDecl>(
      Actions.ActOnCXXRecordDeclFactory(NameLoc, Name, TagDecl::TK_union).get());

  // Union is registered in Scope+SymbolTable by ActOnCXXRecordDeclFactory

  // Parse union body
  if (!Tok.is(TokenKind::l_brace)) {
    emitError(DiagID::err_expected_lbrace);
    return Union;
  }

  consumeToken(); // consume '{'

  // Parse members using parseClassBody
  parseClassBody(Union);

  if (!Tok.is(TokenKind::r_brace)) {
    emitError(DiagID::err_expected_rbrace);
    return Union;
  }

  consumeToken(); // consume '}'

  // Expect optional semicolon
  if (Tok.is(TokenKind::semicolon)) {
    consumeToken();
  }

  return Union;
}

//===----------------------------------------------------------------------===//
// Class Body and Member Parsing
//===----------------------------------------------------------------------===//

/// parseClassBody - Parse a class body.
///
/// member-specification ::= member-specification member-declaration
void Parser::parseClassBody(CXXRecordDecl *Class) {
  while (!Tok.is(TokenKind::r_brace) && !Tok.is(TokenKind::eof)) {
    Decl *Member = parseClassMember(Class);
    if (Member) {
      Class->addMember(Member);
      // Note: semicolon consumption is handled in parseClassMember for methods
      // and data members
    } else {
      // Error recovery: skip to next ';' or '}'
      skipUntil({TokenKind::semicolon, TokenKind::r_brace});
      if (Tok.is(TokenKind::semicolon)) {
        consumeToken();
      }
    }
  }
}

/// parseClassMember - Parse a class member.
///
/// member-declaration ::= decl-specifier-seq? member-declarator-list? ';'
///                      | function-definition
///                      | access-specifier ':'
///                      | friend-declaration
///                      | nested-class-definition
Decl *Parser::parseClassMember(CXXRecordDecl *Class) {
  // Check for access specifier
  if (Tok.is(TokenKind::kw_public) || Tok.is(TokenKind::kw_protected) ||
      Tok.is(TokenKind::kw_private)) {
    SourceLocation Loc = Tok.getLocation();
    AccessSpecDecl *AccessSpec = parseAccessSpecifier(Loc);
    if (AccessSpec && Class) {
      // Update current access specifier
      Class->setCurrentAccess(static_cast<unsigned>(AccessSpec->getAccess()));
    }
    return AccessSpec;
  }

  // Check for friend declaration
  if (Tok.is(TokenKind::kw_friend)) {
    return parseFriendDeclaration(Class);
  }

  // Check for nested class/struct/union declaration
  if (Tok.is(TokenKind::kw_class) || Tok.is(TokenKind::kw_struct) ||
      Tok.is(TokenKind::kw_union)) {
    TokenKind Kind = Tok.is(TokenKind::kw_class) ? TokenKind::kw_class :
                     Tok.is(TokenKind::kw_struct) ? TokenKind::kw_struct :
                     TokenKind::kw_union;
    SourceLocation TagLoc = Tok.getLocation();
    consumeToken();

    if (Kind == TokenKind::kw_class) {
      CXXRecordDecl *NestedClass = parseClassDeclaration(TagLoc);
      if (NestedClass && Class) {
        Class->addMember(NestedClass);
      }
      // Consume optional semicolon
      if (Tok.is(TokenKind::semicolon)) {
        consumeToken();
      }
      return NestedClass;
    } else if (Kind == TokenKind::kw_struct) {
      CXXRecordDecl *NestedStruct = parseStructDeclaration(TagLoc);
      if (NestedStruct && Class) {
        Class->addMember(NestedStruct);
      }
      // Consume optional semicolon
      if (Tok.is(TokenKind::semicolon)) {
        consumeToken();
      }
      return NestedStruct;
    } else {
      CXXRecordDecl *NestedUnion = parseUnionDeclaration(TagLoc);
      if (NestedUnion && Class) {
        Class->addMember(NestedUnion);
      }
      // Consume optional semicolon
      if (Tok.is(TokenKind::semicolon)) {
        consumeToken();
      }
      return NestedUnion;
    }
  }

  // Check for using declaration (including inheriting constructors: using Base::Base;)
  if (Tok.is(TokenKind::kw_using)) {
    SourceLocation UsingLoc = Tok.getLocation();
    consumeToken(); // consume 'using'

    // Check for inheriting constructor: using Base::Base;
    if (Tok.is(TokenKind::identifier)) {
      llvm::StringRef First = Tok.getText();
      SourceLocation FirstLoc = Tok.getLocation();
      consumeToken();

      if (Tok.is(TokenKind::coloncolon)) {
        consumeToken(); // consume '::'

        if (Tok.is(TokenKind::identifier)) {
          llvm::StringRef Second = Tok.getText();
          consumeToken();

          // If Second == First, this is inheriting constructor: using Base::Base;
          if (Second == First) {
            // This is an inheriting constructor declaration
            // Create UsingDecl with IsInheritingConstructor flag
            if (!Tok.is(TokenKind::semicolon)) {
              emitError(DiagID::err_expected_semi);
              return nullptr;
            }
            consumeToken();
            return llvm::cast<UsingDecl>(Actions.ActOnUsingDecl(UsingLoc, First,
                llvm::StringRef(First.str() + "::"), true, true).get());
          }
          // Otherwise, it's a regular using declaration
          // Fall through to handle as regular using declaration
        }
      }
      // If we get here, it's a type alias
      // Put the tokens back conceptually by parsing as type alias
      // For simplicity, we'll create a UsingDecl
      if (Tok.is(TokenKind::equal)) {
        // Type alias: using X = type;
        return parseTypeAliasDeclaration(UsingLoc);
      }
      if (!Tok.is(TokenKind::semicolon)) {
        emitError(DiagID::err_expected_semi);
        return nullptr;
      }
      consumeToken();
      return llvm::cast<UsingDecl>(Actions.ActOnUsingDecl(FirstLoc, First, "", false).get());
    }
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  // Check for constructor/destructor (if we're in a class scope)
  if (Class) {
    // Check for destructor (starts with '~')
    if (Tok.is(TokenKind::tilde)) {
      SourceLocation TildeLoc = Tok.getLocation();
      consumeToken(); // consume '~'
      
      // Expect class name
      if (Tok.is(TokenKind::identifier) && Tok.getText() == Class->getName()) {
        SourceLocation NameLoc = Tok.getLocation();
        consumeToken(); // consume class name
        
        if (Tok.is(TokenKind::l_paren)) {
          return parseDestructorDeclaration(Class, NameLoc);
        }
        emitError(DiagID::err_expected);
        return nullptr;
      }
      emitError(DiagID::err_expected_identifier);
      return nullptr;
    }
    
    // Check for constructor (identifier matching class name)
    if (Tok.is(TokenKind::identifier)) {
      llvm::StringRef PotentialCtorName = Tok.getText();

      // Check if this is a constructor (name matches class name)
      if (PotentialCtorName == Class->getName()) {
        SourceLocation NameLoc = Tok.getLocation();
        consumeToken();

        // Check if this is a constructor (followed by '(')
        if (Tok.is(TokenKind::l_paren)) {
          return parseConstructorDeclaration(Class, NameLoc);
        }
        // Otherwise it might be a member variable with the same name as the class
        // This is legal in C++ but unusual - emit a warning and try to continue
        emitError(DiagID::err_expected_lparen);

        // Try to recover: skip to next ';' or '}'
        skipUntil({TokenKind::semicolon, TokenKind::r_brace});
        if (Tok.is(TokenKind::semicolon)) {
          consumeToken();
        }
        return nullptr;
      }
    }
  }

  // Parse storage class specifiers (static, mutable) and virtual using DeclSpec
  DeclSpec DS;
  parseDeclSpecifierSeq(DS);
  if (!DS.hasTypeSpecifier()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }

  QualType Type = DS.Type;

  // P7.1.3: Check for operator overloading (static operator(), static operator[])
  // Syntax: static operator()(...) or static operator[](...)
  // Also handles non-static: operator()(...) or operator[](...)
  bool IsStaticOp = (DS.SC == StorageClass::Static);
  if (Tok.is(TokenKind::kw_operator)) {
    SourceLocation OpLoc = Tok.getLocation();
    consumeToken(); // consume 'operator'

    // Determine which operator
    llvm::StringRef OperatorName;
    if (Tok.is(TokenKind::l_paren)) {
      // operator() — consume both parens
      consumeToken(); // consume '('
      if (!Tok.is(TokenKind::r_paren)) {
        emitError(DiagID::err_expected_rparen);
        return nullptr;
      }
      consumeToken(); // consume ')'
      OperatorName = "operator()";
    } else if (Tok.is(TokenKind::l_square)) {
      // operator[] — consume both brackets
      consumeToken(); // consume '['
      if (!Tok.is(TokenKind::r_square)) {
        emitError(DiagID::err_expected);
        return nullptr;
      }
      consumeToken(); // consume ']'
      OperatorName = "operator[]";
    } else {
      emitError(DiagID::err_expected);
      return nullptr;
    }

    // Parse parameter list
    llvm::SmallVector<ParmVarDecl *, 8> Params;
    unsigned ParamIndex = 0;

    if (!Tok.is(TokenKind::l_paren)) {
      emitError(DiagID::err_expected_lparen);
      return nullptr;
    }
    consumeToken(); // consume '('

    if (!Tok.is(TokenKind::r_paren)) {
      do {
        ParmVarDecl *Param = parseParameterDeclaration(ParamIndex);
        if (!Param) {
          emitError(DiagID::err_expected_type);
          skipUntil({TokenKind::r_paren, TokenKind::semicolon});
          break;
        }
        Params.push_back(Param);
        ++ParamIndex;

        if (Tok.is(TokenKind::comma)) {
          consumeToken();
        } else {
          break;
        }
      } while (true);
    }

    if (!Tok.is(TokenKind::r_paren)) {
      emitError(DiagID::err_expected_rparen);
      return nullptr;
    }
    consumeToken(); // consume ')'

    // Parse trailing return type (-> type)
    if (Tok.is(TokenKind::arrow)) {
      consumeToken();
      QualType TrailingReturnType = parseType();
      if (!TrailingReturnType.isNull())
        Type = TrailingReturnType;
    }

    // Parse function body
    Stmt *Body = nullptr;
    bool IsDefaulted = false;
    bool IsDeleted = false;

    if (Tok.is(TokenKind::l_brace)) {
      Body = parseCompoundStatement();
    } else if (Tok.is(TokenKind::equal)) {
      consumeToken();
      if (Tok.is(TokenKind::kw_default)) {
        IsDefaulted = true;
        consumeToken();
      } else if (Tok.is(TokenKind::kw_delete)) {
        IsDeleted = true;
        consumeToken();
      }
    }

    if (Tok.is(TokenKind::semicolon))
      consumeToken();

    // Create CXXMethodDecl
    AccessSpecifier Access =
        static_cast<AccessSpecifier>(Class->getCurrentAccess());

    CXXMethodDecl *Method = llvm::cast<CXXMethodDecl>(
        Actions.ActOnCXXMethodDeclFactory(OpLoc, OperatorName, Type, Params,
            Class, Body, IsStaticOp, false, false, false, false,
            false, false, IsDefaulted, IsDeleted,
            CXXMethodDecl::RQ_None, false, false, nullptr, Access).get());

    // P7.1.3: Mark as static operator if static
    if (Method && IsStaticOp)
      Method->setStaticOperator(true);

    return Method;
  }

  // Parse member name
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  llvm::StringRef Name = Tok.getText();
  SourceLocation NameLoc = Tok.getLocation();
  consumeToken();

  bool IsStatic = (DS.SC == StorageClass::Static);
  bool IsMutable = (DS.SC == StorageClass::Mutable);
  bool IsVirtual = DS.IsVirtual;

  // Check if this is a member function
  if (Tok.is(TokenKind::l_paren)) {
    // Parse member function
    llvm::SmallVector<ParmVarDecl *, 8> Params;
    unsigned ParamIndex = 0;
    consumeToken(); // consume '('

    // Parse parameters
    if (!Tok.is(TokenKind::r_paren)) {
      do {
        ParmVarDecl *Param = parseParameterDeclaration(ParamIndex);
        if (!Param) {
          emitError(DiagID::err_expected_type);
          skipUntil({TokenKind::r_paren, TokenKind::semicolon});
          break;
        }
        Params.push_back(Param);
        ++ParamIndex;

        if (Tok.is(TokenKind::comma)) {
          consumeToken();
        } else {
          break;
        }
      } while (true);
    }

    if (!Tok.is(TokenKind::r_paren)) {
      emitError(DiagID::err_expected_rparen);
      return nullptr;
    }
    consumeToken(); // consume ')'

    // Parse cv-qualifiers
    bool IsConst = false;
    bool IsVolatile = false;
    while (Tok.is(TokenKind::kw_const) || Tok.is(TokenKind::kw_volatile)) {
      if (Tok.is(TokenKind::kw_const)) {
        IsConst = true;
        consumeToken();
      } else if (Tok.is(TokenKind::kw_volatile)) {
        IsVolatile = true;
        consumeToken();
      }
    }

    // Parse ref-qualifier (&, &&)
    CXXMethodDecl::RefQualifierKind RefQual = CXXMethodDecl::RQ_None;
    if (Tok.is(TokenKind::ampamp)) {
      consumeToken();
      RefQual = CXXMethodDecl::RQ_RValue;
    } else if (Tok.is(TokenKind::amp)) {
      consumeToken();
      RefQual = CXXMethodDecl::RQ_LValue;
    }

    // Parse override and final specifiers
    // Note: override and final are identifiers with special meaning, not keywords
    bool IsOverride = false;
    bool IsFinal = false;
    while (Tok.is(TokenKind::identifier)) {
      llvm::StringRef Text = Tok.getText();
      if (Text == "override") {
        IsOverride = true;
        consumeToken();
      } else if (Text == "final") {
        IsFinal = true;
        consumeToken();
      } else {
        break;
      }
    }

    // Parse trailing return type (-> type)
    if (Tok.is(TokenKind::arrow)) {
      consumeToken(); // consume '->'
      QualType TrailingReturnType = parseType();
      if (!TrailingReturnType.isNull()) {
        // Replace the return type with the trailing return type
        Type = TrailingReturnType;
      }
    }

    // Parse noexcept specification
    bool HasNoexceptSpec = false;
    bool NoexceptValue = false;
    Expr *NoexceptExpr = nullptr;
    if (Tok.is(TokenKind::kw_noexcept)) {
      HasNoexceptSpec = true;
      consumeToken();
      
      if (Tok.is(TokenKind::l_paren)) {
        consumeToken();
        // Check if it's noexcept(true), noexcept(false), or noexcept(expression)
        if (Tok.is(TokenKind::kw_true)) {
          NoexceptValue = true;
          consumeToken();
        } else if (Tok.is(TokenKind::kw_false)) {
          NoexceptValue = false;
          consumeToken();
        } else {
          // Parse expression
          NoexceptExpr = parseExpression();
        }
        
        if (!Tok.is(TokenKind::r_paren)) {
          emitError(DiagID::err_expected_rparen);
          return nullptr;
        }
        consumeToken();
      } else {
        // noexcept without parentheses means noexcept(true)
        NoexceptValue = true;
      }
    }

    // Parse function body (if present)
    Stmt *Body = nullptr;
    bool IsPureVirtual = false;
    bool IsDefaulted = false;
    bool IsDeleted = false;
    
    if (Tok.is(TokenKind::l_brace)) {
      Body = parseCompoundStatement();
    } else if (Tok.is(TokenKind::equal)) {
      consumeToken();
      if (Tok.is(TokenKind::kw_default)) {
        IsDefaulted = true;
        consumeToken();
      } else if (Tok.is(TokenKind::kw_delete)) {
        IsDeleted = true;
        consumeToken();
      } else if (Tok.is(TokenKind::numeric_constant)) {
        // Check for = 0 (pure virtual)
        StringRef NumText = Tok.getText();
        // Check if the value is actually 0
        // Remove suffix if present
        while (!NumText.empty() && (NumText.back() == 'u' || NumText.back() == 'U' ||
                                     NumText.back() == 'l' || NumText.back() == 'L')) {
          NumText = NumText.drop_back(1);
        }
        // Check if it's 0 (in any base)
        bool IsZero = NumText == "0" || NumText == "0x0" || NumText == "0X0" ||
                      NumText == "0b0" || NumText == "0B0";
        if (IsZero) {
          IsPureVirtual = true;
        }
        consumeToken();
      }
    }

    // Consume optional semicolon
    if (Tok.is(TokenKind::semicolon)) {
      consumeToken();
    }

    // Create CXXMethodDecl via Sema
    AccessSpecifier Access =
        static_cast<AccessSpecifier>(Class->getCurrentAccess());

    // P7.1.1: Extract explicit object parameter (deducing this).
    // The explicit object parameter is the first parameter marked with `this`.
    // It is removed from the normal parameter list and stored separately.
    ParmVarDecl *ExplicitObjParam = nullptr;
    if (!Params.empty() && Params.front()->isExplicitObjectParam()) {
      ExplicitObjParam = Params.front();
      Params.erase(Params.begin());
      // Deducing this is incompatible with static/virtual/const/volatile qualifiers
      if (IsStatic)
        emitError(DiagID::err_explicit_object_param_static);
      if (IsVirtual)
        emitError(DiagID::err_explicit_object_param_virtual);
    }

    CXXMethodDecl *Method = llvm::cast<CXXMethodDecl>(
        Actions.ActOnCXXMethodDeclFactory(NameLoc, Name, Type, Params, Class, Body,
            IsStatic, IsConst, IsVolatile, IsVirtual, IsPureVirtual,
            IsOverride, IsFinal, IsDefaulted, IsDeleted,
            RefQual, HasNoexceptSpec, NoexceptValue, NoexceptExpr,
            Access).get());

    // P7.1.1: Set explicit object parameter on the method
    if (Method && ExplicitObjParam)
      Method->setExplicitObjectParam(ExplicitObjParam);

    // Method is registered in Scope+SymbolTable by ActOnCXXMethodDeclFactory

    return Method;
  }

  // Otherwise, it's a data member
  // Parse bit-field (if present)
  Expr *BitWidth = nullptr;
  if (Tok.is(TokenKind::colon)) {
    consumeToken();
    BitWidth = parseExpression();
  }

  // Parse in-class initializer (if present)
  Expr *InClassInit = nullptr;
  if (Tok.is(TokenKind::equal)) {
    consumeToken(); // consume '='
    InClassInit = parseExpression();
  }

  // Expect ';'
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }
  consumeToken();

  // Create VarDecl for static members, FieldDecl for non-static
  if (IsStatic) {
    VarDecl *VD = llvm::cast<VarDecl>(Actions.ActOnVarDeclFull(NameLoc, Name, Type, InClassInit, true).get());
    // VD registered by Sema
    return VD;
  }
  
  AccessSpecifier Access =
      static_cast<AccessSpecifier>(Class->getCurrentAccess());
  return llvm::cast<FieldDecl>(Actions.ActOnFieldDeclFactory(NameLoc, Name, Type, BitWidth, IsMutable, InClassInit, Access).get());
}

//===----------------------------------------------------------------------===//
// Access Specifier Parsing
//===----------------------------------------------------------------------===//

/// parseAccessSpecifier - Parse an access specifier.
///
/// access-specifier ::= 'public' | 'protected' | 'private'
AccessSpecDecl *Parser::parseAccessSpecifier(SourceLocation Loc) {
  AccessSpecifier Access;

  if (Tok.is(TokenKind::kw_public)) {
    Access = AccessSpecifier::AS_public;
    consumeToken();
  } else if (Tok.is(TokenKind::kw_protected)) {
    Access = AccessSpecifier::AS_protected;
    consumeToken();
  } else if (Tok.is(TokenKind::kw_private)) {
    Access = AccessSpecifier::AS_private;
    consumeToken();
  } else {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  // Expect ':'
  if (!Tok.is(TokenKind::colon)) {
    emitError(DiagID::err_expected_colon);
    return nullptr;
  }

  SourceLocation ColonLoc = Tok.getLocation();
  consumeToken();

  return llvm::cast<AccessSpecDecl>(Actions.ActOnAccessSpecDeclFactory(Loc, Access, ColonLoc).get());
}

//===----------------------------------------------------------------------===//
// Base Clause Parsing
//===----------------------------------------------------------------------===//

/// parseBaseClause - Parse a base clause.
///
/// base-clause ::= ':' base-specifier-list
bool Parser::parseBaseClause(CXXRecordDecl *Class) {
  if (!Tok.is(TokenKind::colon)) {
    return false;
  }

  consumeToken(); // consume ':'

  // Parse base specifiers
  do {
    parseBaseSpecifier(Class);

    if (Tok.is(TokenKind::comma)) {
      consumeToken();
    } else {
      break;
    }
  } while (true);

  return true;
}

/// parseBaseSpecifier - Parse a base specifier.
///
/// base-specifier ::= attribute-specifier-seq? base-type-specifier
///                  | 'virtual' access-specifier? base-type-specifier
///                  | access-specifier 'virtual'? base-type-specifier
void Parser::parseBaseSpecifier(CXXRecordDecl *Class) {
  bool IsVirtual = false;
  unsigned Access = 0; // 0 = private, 1 = protected, 2 = public

  // Parse 'virtual' and access specifier (in any order)
  while (Tok.is(TokenKind::kw_virtual) || Tok.is(TokenKind::kw_public) ||
         Tok.is(TokenKind::kw_protected) || Tok.is(TokenKind::kw_private)) {
    if (Tok.is(TokenKind::kw_virtual)) {
      IsVirtual = true;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_public)) {
      Access = 2;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_protected)) {
      Access = 1;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_private)) {
      Access = 0;
      consumeToken();
    }
  }

  // Parse base type
  QualType BaseType = parseType();
  if (BaseType.isNull()) {
    emitError(DiagID::err_expected_type);
    return;
  }

  Class->addBase(CXXRecordDecl::BaseSpecifier(BaseType, Tok.getLocation(), IsVirtual, false, Access));
}

//===----------------------------------------------------------------------===//
// Constructor / Destructor Parsing
//===----------------------------------------------------------------------===//

/// parseConstructorDeclaration - Parse a constructor declaration.
///
/// constructor-declaration ::= identifier '(' parameter-declaration-clause? ')'
///                               (member-initializer-list)? function-body?
///
CXXConstructorDecl *Parser::parseConstructorDeclaration(CXXRecordDecl *Class,
                                                         SourceLocation Loc) {
  // Parse parameter list
  llvm::SmallVector<ParmVarDecl *, 8> Params;
  unsigned ParamIndex = 0;

  expectAndConsume(TokenKind::l_paren, "expected '(' after constructor name");

  // Parse parameters
  if (!Tok.is(TokenKind::r_paren)) {
    do {
      ParmVarDecl *Param = parseParameterDeclaration(ParamIndex);
      if (!Param) {
        emitError(DiagID::err_expected_type);
        skipUntil({TokenKind::r_paren, TokenKind::semicolon});
        break;
      }
      Params.push_back(Param);
      ++ParamIndex;

      if (Tok.is(TokenKind::comma)) {
        consumeToken();
      } else {
        break;
      }
    } while (true);
  }

  if (!Tok.is(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
    return nullptr;
  }
  consumeToken(); // consume ')'

  // Create CXXConstructorDecl via Sema
  CXXConstructorDecl *Ctor = llvm::cast<CXXConstructorDecl>(
      Actions.ActOnCXXConstructorDeclFactory(Loc, Class, Params, nullptr, false).get());

  // Parse member initializer list (if present)
  if (Tok.is(TokenKind::colon)) {
    parseMemberInitializerList(Ctor);
  }

  // Parse function body (if present)
  Stmt *Body = nullptr;
  if (Tok.is(TokenKind::l_brace)) {
    Body = parseCompoundStatement();
  }

  Ctor->setBody(Body);

  // Constructor registered by Sema

  return Ctor;
}

/// parseDestructorDeclaration - Parse a destructor declaration.
///
/// destructor-declaration ::= '~' identifier '(' ')' function-body?
///
CXXDestructorDecl *Parser::parseDestructorDeclaration(CXXRecordDecl *Class,
                                                       SourceLocation Loc) {
  // Parse parameter list (destructors have no parameters)
  expectAndConsume(TokenKind::l_paren, "expected '(' after destructor name");

  if (!Tok.is(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
    return nullptr;
  }
  consumeToken(); // consume ')'

  // Parse function body (if present)
  Stmt *Body = nullptr;
  if (Tok.is(TokenKind::l_brace)) {
    Body = parseCompoundStatement();
  }

  // Create CXXDestructorDecl via Sema
  CXXDestructorDecl *Dtor = llvm::cast<CXXDestructorDecl>(
      Actions.ActOnCXXDestructorDeclFactory(Loc, Class, Body).get());

  // Destructor is registered in Scope+SymbolTable by ActOnCXXDestructorDeclFactory

  return Dtor;
}

//===----------------------------------------------------------------------===//
// Member Initializer List Parsing
//===----------------------------------------------------------------------===//

/// parseMemberInitializerList - Parse a member initializer list.
///
/// member-initializer-list ::= ':' member-initializer (',' member-initializer)*
///
void Parser::parseMemberInitializerList(CXXConstructorDecl *Ctor) {
  assert(Tok.is(TokenKind::colon) && "Expected ':'");
  consumeToken(); // consume ':'

  do {
    CXXCtorInitializer *Init = parseMemberInitializer(Ctor);
    if (Init) {
      Ctor->addInitializer(Init);
      // Check for delegating constructor
      if (Init->isDelegatingInitializer()) {
        // Delegating constructor cannot have other initializers
        break;
      }
    } else {
      // Error recovery: skip to next ',' or '{'
      skipUntil({TokenKind::comma, TokenKind::l_brace, TokenKind::semicolon});
    }

    if (Tok.is(TokenKind::comma)) {
      consumeToken();
    } else {
      break;
    }
  } while (true);
}

/// parseMemberInitializer - Parse a single member initializer.
///
/// member-initializer ::= identifier '(' expr-list? ')'
///                      | identifier '{' expr-list? '}'
///                      | identifier
///
/// For delegating constructors: ClassName '(' args ')' or ClassName '{' args '}'
CXXCtorInitializer *Parser::parseMemberInitializer(CXXConstructorDecl *Ctor) {
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  SourceLocation MemberLoc = Tok.getLocation();
  llvm::StringRef MemberName = Tok.getText();
  consumeToken();

  // Check if this is a delegating constructor (name matches class name)
  bool IsDelegating = false;
  if (Ctor && Ctor->getParent() && MemberName == Ctor->getParent()->getName()) {
    IsDelegating = true;
  }

  llvm::SmallVector<Expr *, 4> Args;

  // Check for '(' or '{'
  if (Tok.is(TokenKind::l_paren)) {
    consumeToken(); // consume '('

    // Parse arguments
    if (!Tok.is(TokenKind::r_paren)) {
      do {
        Expr *Arg = parseExpression();
        if (Arg) {
          Args.push_back(Arg);
        } else {
          emitError(DiagID::err_expected_expression);
          skipUntil({TokenKind::comma, TokenKind::r_paren});
        }

        if (Tok.is(TokenKind::comma)) {
          consumeToken();
        } else {
          break;
        }
      } while (true);
    }

    if (!Tok.is(TokenKind::r_paren)) {
      emitError(DiagID::err_expected_rparen);
      return nullptr;
    }
    consumeToken(); // consume ')'
  } else if (Tok.is(TokenKind::l_brace)) {
    consumeToken(); // consume '{'

    // Parse arguments (uniform initialization)
    if (!Tok.is(TokenKind::r_brace)) {
      do {
        Expr *Arg = parseExpression();
        if (Arg) {
          Args.push_back(Arg);
        } else {
          emitError(DiagID::err_expected_expression);
          skipUntil({TokenKind::comma, TokenKind::r_brace});
        }

        if (Tok.is(TokenKind::comma)) {
          consumeToken();
        } else {
          break;
        }
      } while (true);
    }

    if (!Tok.is(TokenKind::r_brace)) {
      emitError(DiagID::err_expected_rbrace);
      return nullptr;
    }
    consumeToken(); // consume '}'
  }
  // else: member initializer without arguments (value initialization)

  return new CXXCtorInitializer(MemberLoc, MemberName, Args, false, IsDelegating);
}

//===----------------------------------------------------------------------===//
// Friend Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseFriendDeclaration - Parse a friend declaration.
///
/// friend-declaration ::= 'friend' friend-declaration-specifier
///
/// friend-declaration-specifier ::= class-key identifier
///                                | type-specifier-seq declarator
FriendDecl *Parser::parseFriendDeclaration(CXXRecordDecl *Class) {
  assert(Tok.is(TokenKind::kw_friend) && "Expected 'friend'");
  SourceLocation FriendLoc = Tok.getLocation();
  consumeToken(); // consume 'friend'

  // Check for friend class/struct/union
  if (Tok.is(TokenKind::kw_class) || Tok.is(TokenKind::kw_struct) ||
      Tok.is(TokenKind::kw_union)) {
    consumeToken(); // consume class/struct/union

    // Parse the type name
    if (!Tok.is(TokenKind::identifier)) {
      emitError(DiagID::err_expected_identifier);
      return nullptr;
    }

    llvm::StringRef TypeName = Tok.getText();
    SourceLocation TypeNameLoc = Tok.getLocation();
    consumeToken();

    // Create friend type declaration via Sema (handles type lookup + forward decl)
    FriendDecl *FD = llvm::cast<FriendDecl>(
        Actions.ActOnFriendTypeDecl(FriendLoc, TypeName, TypeNameLoc).get());

    // Expect semicolon
    if (!Tok.is(TokenKind::semicolon)) {
      emitError(DiagID::err_expected_semi);
      return FD;
    }
    consumeToken();

    return FD;
  }

  // Friend function declaration
  // Parse type
  QualType Type = parseType();
  if (Type.isNull()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }

  // Parse function name
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  llvm::StringRef Name = Tok.getText();
  SourceLocation NameLoc = Tok.getLocation();
  consumeToken();

  // Parse parameter list
  if (!Tok.is(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return nullptr;
  }

  llvm::SmallVector<ParmVarDecl *, 8> Params;
  unsigned ParamIndex = 0;
  consumeToken(); // consume '('

  if (!Tok.is(TokenKind::r_paren)) {
    do {
      ParmVarDecl *Param = parseParameterDeclaration(ParamIndex);
      if (Param) {
        Params.push_back(Param);
        ++ParamIndex;
      }

      if (Tok.is(TokenKind::comma)) {
        consumeToken();
      } else {
        break;
      }
    } while (true);
  }

  if (!Tok.is(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
    return nullptr;
  }
  consumeToken(); // consume ')'

  // Expect semicolon
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }
  consumeToken();

  // Create friend function declaration via Sema
  return llvm::cast<FriendDecl>(
      Actions.ActOnFriendFunctionDecl(FriendLoc, NameLoc, Name, Type, Params).get());
}

} // namespace blocktype
