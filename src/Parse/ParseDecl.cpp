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
  // Check for class declaration
  if (Tok.is(TokenKind::kw_class)) {
    SourceLocation ClassLoc = Tok.getLocation();
    consumeToken();
    return parseClassDeclaration(ClassLoc);
  }

  // Check for struct declaration
  if (Tok.is(TokenKind::kw_struct)) {
    SourceLocation StructLoc = Tok.getLocation();
    consumeToken();
    return parseStructDeclaration(StructLoc);
  }

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

//===----------------------------------------------------------------------===//
// Class Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseClassDeclaration - Parse a class declaration.
///
/// class-specifier ::= 'class' identifier? '{' member-specification? '}'
///                    | 'class' identifier base-clause? '{' member-specification? '}'
CXXRecordDecl *Parser::parseClassDeclaration(SourceLocation ClassLoc) {
  // Parse class name (optional)
  llvm::StringRef Name;
  SourceLocation NameLoc;

  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();
  }

  // Create CXXRecordDecl
  CXXRecordDecl *Class = Context.create<CXXRecordDecl>(NameLoc, Name, TagDecl::TK_class);

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
  parseClassBody(Class);

  if (!Tok.is(TokenKind::r_brace)) {
    emitError(DiagID::err_expected_rbrace);
    return Class;
  }

  consumeToken(); // consume '}'
  return Class;
}

/// parseStructDeclaration - Parse a struct declaration.
///
/// struct-specifier ::= 'struct' identifier? '{' member-specification? '}'
RecordDecl *Parser::parseStructDeclaration(SourceLocation StructLoc) {
  // Parse struct name (optional)
  llvm::StringRef Name;
  SourceLocation NameLoc;

  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();
  }

  // Create RecordDecl (struct has public default access)
  RecordDecl *Struct = Context.create<RecordDecl>(NameLoc, Name, TagDecl::TK_struct);

  // Parse struct body
  if (!Tok.is(TokenKind::l_brace)) {
    emitError(DiagID::err_expected_lbrace);
    return Struct;
  }

  consumeToken(); // consume '{'

  // Parse members
  while (!Tok.is(TokenKind::r_brace) && !Tok.is(TokenKind::eof)) {
    Decl *Member = parseClassMember(nullptr); // For struct, use nullptr for now
    if (Member) {
      Struct->addField(static_cast<FieldDecl *>(Member));
    } else {
      // Error recovery: skip to next ';' or '}'
      skipUntil({TokenKind::semicolon, TokenKind::r_brace});
      if (Tok.is(TokenKind::semicolon)) {
        consumeToken();
      }
    }
  }

  if (!Tok.is(TokenKind::r_brace)) {
    emitError(DiagID::err_expected_rbrace);
    return Struct;
  }

  consumeToken(); // consume '}'
  return Struct;
}

/// parseClassBody - Parse a class body.
///
/// member-specification ::= member-specification member-declaration
void Parser::parseClassBody(CXXRecordDecl *Class) {
  while (!Tok.is(TokenKind::r_brace) && !Tok.is(TokenKind::eof)) {
    Decl *Member = parseClassMember(Class);
    if (Member) {
      Class->addMember(Member);
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
Decl *Parser::parseClassMember(CXXRecordDecl *Class) {
  // Check for access specifier
  if (Tok.is(TokenKind::kw_public) || Tok.is(TokenKind::kw_protected) ||
      Tok.is(TokenKind::kw_private)) {
    SourceLocation Loc = Tok.getLocation();
    return parseAccessSpecifier(Loc);
  }

  // Parse type
  QualType Type = parseType();
  if (Type.isNull()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }

  // Parse member name
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  llvm::StringRef Name = Tok.getText();
  SourceLocation NameLoc = Tok.getLocation();
  consumeToken();

  // Check if this is a member function
  if (Tok.is(TokenKind::l_paren)) {
    // Parse member function
    llvm::SmallVector<ParmVarDecl *, 8> Params;
    consumeToken(); // consume '('

    // Parse parameters
    if (!Tok.is(TokenKind::r_paren)) {
      do {
        ParmVarDecl *Param = parseParameterDeclaration();
        if (!Param) {
          emitError(DiagID::err_expected_type);
          skipUntil({TokenKind::r_paren, TokenKind::semicolon});
          break;
        }
        Params.push_back(Param);

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

    // Parse function body (if present)
    Stmt *Body = nullptr;
    if (Tok.is(TokenKind::l_brace)) {
      Body = parseCompoundStatement();
    }

    // Create CXXMethodDecl
    return Context.create<CXXMethodDecl>(NameLoc, Name, Type, Params, Class, Body,
                                         false, IsConst, false, false, false);
  }

  // Otherwise, it's a data member
  // Parse bit-field (if present)
  Expr *BitWidth = nullptr;
  if (Tok.is(TokenKind::colon)) {
    consumeToken();
    BitWidth = parseExpression();
  }

  // Expect ';'
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }
  consumeToken();

  // Create FieldDecl
  return Context.create<FieldDecl>(NameLoc, Name, Type, BitWidth, false);
}

/// parseAccessSpecifier - Parse an access specifier.
///
/// access-specifier ::= 'public' | 'protected' | 'private'
AccessSpecDecl *Parser::parseAccessSpecifier(SourceLocation Loc) {
  AccessSpecDecl::AccessSpecifier Access;

  if (Tok.is(TokenKind::kw_public)) {
    Access = AccessSpecDecl::AS_public;
    consumeToken();
  } else if (Tok.is(TokenKind::kw_protected)) {
    Access = AccessSpecDecl::AS_protected;
    consumeToken();
  } else if (Tok.is(TokenKind::kw_private)) {
    Access = AccessSpecDecl::AS_private;
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

  return Context.create<AccessSpecDecl>(Loc, Access, ColonLoc);
}

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

} // namespace blocktype
