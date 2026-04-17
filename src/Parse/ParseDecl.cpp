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
  // Check for module declaration (C++20)
  if (Tok.is(TokenKind::kw_module)) {
    return parseModuleDeclaration();
  }

  // Check for import declaration (C++20)
  if (Tok.is(TokenKind::kw_import)) {
    return parseImportDeclaration();
  }

  // Check for export declaration (C++20)
  if (Tok.is(TokenKind::kw_export)) {
    // Look ahead to determine if it's an export module/import or export declaration
    Token NextTok = peekToken();
    if (NextTok.is(TokenKind::kw_module)) {
      return parseModuleDeclaration();
    } else if (NextTok.is(TokenKind::kw_import)) {
      return parseImportDeclaration();
    } else {
      return parseExportDeclaration();
    }
  }

  // Check for template declaration
  if (Tok.is(TokenKind::kw_template)) {
    return parseTemplateDeclaration();
  }

  // Check for namespace declaration
  // Note: 'inline' can be a namespace specifier (inline namespace) or a function specifier
  // We need to look ahead to distinguish between them
  if (Tok.is(TokenKind::kw_namespace)) {
    return parseNamespaceDeclaration();
  }
  if (Tok.is(TokenKind::kw_inline)) {
    // Look ahead to see if this is an inline namespace
    Token NextTok = peekToken();
    if (NextTok.is(TokenKind::kw_namespace)) {
      return parseNamespaceDeclaration();
    }
    // Otherwise, it's a function specifier, fall through
  }

  // Check for using declaration, directive, or type alias
  if (Tok.is(TokenKind::kw_using)) {
    // Look ahead to determine if it's a using directive, type alias, or using declaration
    Token NextTok = peekToken();
    if (NextTok.is(TokenKind::kw_namespace)) {
      return parseUsingDirective();
    } else if (NextTok.is(TokenKind::identifier)) {
      // Could be type alias (using X = ...) or using declaration (using A::B)
      // For type alias, the pattern is: using identifier = type
      // For using declaration, the pattern is: using [typename] A::B
      // We need to look for '=' or '::' after the identifier
      // Look ahead two tokens to check for '=' or '::'
      SourceLocation UsingLoc = Tok.getLocation();
      consumeToken(); // consume 'using'
      
      // Check for 'typename' keyword (optional)
      if (Tok.is(TokenKind::kw_typename)) {
        consumeToken(); // consume 'typename'
      }
      
      // Parse the first identifier
      if (!Tok.is(TokenKind::identifier)) {
        emitError(DiagID::err_expected_identifier);
        return nullptr;
      }
      
      llvm::StringRef FirstName = Tok.getText();
      consumeToken();
      
      // Check if this is a type alias (using X = ...)
      if (Tok.is(TokenKind::equal)) {
        // This is a type alias
        consumeToken(); // consume '='
        QualType TargetType = parseType();
        if (TargetType.isNull()) {
          emitError(DiagID::err_expected_type);
          return nullptr;
        }
        
        if (!tryConsumeToken(TokenKind::semicolon)) {
          emitError(DiagID::err_expected_semi);
        }
        
        return Context.create<TypeAliasDecl>(UsingLoc, FirstName, TargetType);
      } else if (Tok.is(TokenKind::coloncolon)) {
        // This is a using declaration (using A::B)
        // Put back the tokens and parse as using declaration
        // For simplicity, just create a UsingDecl
        // Parse the rest of the qualified name
        while (Tok.is(TokenKind::coloncolon)) {
          consumeToken(); // consume '::'
          if (Tok.is(TokenKind::identifier)) {
            consumeToken(); // consume identifier
          }
        }
        
        if (!tryConsumeToken(TokenKind::semicolon)) {
          emitError(DiagID::err_expected_semi);
        }
        
        return Context.create<UsingDecl>(UsingLoc, "", FirstName, true);
      } else {
        // Unknown using declaration
        emitError(DiagID::err_expected);
        return nullptr;
      }
    } else {
      return parseUsingDeclaration();
    }
  }

  // Check for class declaration
  if (Tok.is(TokenKind::kw_class)) {
    SourceLocation ClassLoc = Tok.getLocation();
    consumeToken();
    Decl *Result = parseClassDeclaration(ClassLoc);
    // Consume optional semicolon after class definition
    if (Tok.is(TokenKind::semicolon)) {
      consumeToken();
    }
    return Result;
  }

  // Check for struct declaration
  if (Tok.is(TokenKind::kw_struct)) {
    SourceLocation StructLoc = Tok.getLocation();
    consumeToken();
    Decl *Result = parseStructDeclaration(StructLoc);
    // Consume optional semicolon after struct definition
    if (Tok.is(TokenKind::semicolon)) {
      consumeToken();
    }
    return Result;
  }

  // Check for enum declaration
  if (Tok.is(TokenKind::kw_enum)) {
    SourceLocation EnumLoc = Tok.getLocation();
    consumeToken();
    Decl *Result = parseEnumDeclaration(EnumLoc);
    // Consume optional semicolon after enum definition
    if (Tok.is(TokenKind::semicolon)) {
      consumeToken();
    }
    return Result;
  }

  // Check for union declaration
  if (Tok.is(TokenKind::kw_union)) {
    SourceLocation UnionLoc = Tok.getLocation();
    consumeToken();
    Decl *Result = parseUnionDeclaration(UnionLoc);
    // Consume optional semicolon after union definition
    if (Tok.is(TokenKind::semicolon)) {
      consumeToken();
    }
    return Result;
  }

  // Check for typedef declaration
  if (Tok.is(TokenKind::kw_typedef)) {
    SourceLocation TypedefLoc = Tok.getLocation();
    consumeToken();
    return parseTypedefDeclaration(TypedefLoc);
  }

  // Check for static_assert declaration
  if (Tok.is(TokenKind::kw_static_assert)) {
    SourceLocation Loc = Tok.getLocation();
    consumeToken();
    return parseStaticAssertDeclaration(Loc);
  }

  // Check for asm declaration
  if (Tok.is(TokenKind::kw_asm)) {
    SourceLocation Loc = Tok.getLocation();
    consumeToken();
    return parseAsmDeclaration(Loc);
  }

  // Check for attribute specifier (C++11)
  if (Tok.is(TokenKind::l_square)) {
    // Look ahead to see if this is an attribute specifier [[...]]
    Token NextTok = peekToken();
    if (NextTok.is(TokenKind::l_square)) {
      SourceLocation Loc = Tok.getLocation();
      return parseAttributeSpecifier(Loc);
    }
    // Otherwise, it's an array declarator, fall through
  }

  // Check for extern linkage specification
  if (Tok.is(TokenKind::kw_extern)) {
    // Look ahead to see if this is extern "C" or extern "C++"
    Token NextTok = peekToken();
    if (NextTok.is(TokenKind::string_literal)) {
      SourceLocation Loc = Tok.getLocation();
      consumeToken();
      return parseLinkageSpecDeclaration(Loc);
    }
    // Otherwise, it's a regular extern declaration, fall through
  }

  // Parse storage class specifiers and function specifiers
  bool IsStatic = false;
  bool IsConstexpr = false;
  bool IsInline = false;
  
  while (Tok.is(TokenKind::kw_static) || Tok.is(TokenKind::kw_constexpr) ||
         Tok.is(TokenKind::kw_inline)) {
    if (Tok.is(TokenKind::kw_static)) {
      IsStatic = true;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_constexpr)) {
      IsConstexpr = true;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_inline)) {
      IsInline = true;
      consumeToken();
    }
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

  // Check for qualified name (e.g., ClassName::member)
  if (Tok.is(TokenKind::coloncolon)) {
    consumeToken(); // consume '::'
    
    // Parse the member name
    if (!Tok.is(TokenKind::identifier)) {
      emitError(DiagID::err_expected_identifier);
      return nullptr;
    }
    
    llvm::StringRef MemberName = Tok.getText();
    SourceLocation MemberLoc = Tok.getLocation();
    consumeToken();

    // This is a static member definition
    // Check if this is a function definition
    if (Tok.is(TokenKind::l_paren)) {
      return parseFunctionDeclaration(Type, MemberName, MemberLoc, IsStatic, IsConstexpr, IsInline);
    }

    return parseVariableDeclaration(Type, MemberName, MemberLoc, IsStatic, IsConstexpr);
  }

  // Check if this is a function declaration
  if (Tok.is(TokenKind::l_paren)) {
    return parseFunctionDeclaration(Type, Name, NameLoc, IsStatic, IsConstexpr, IsInline);
  }

  // Otherwise, it's a variable declaration
  return parseVariableDeclaration(Type, Name, NameLoc, IsStatic, IsConstexpr);
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
                                          SourceLocation Loc, bool IsStatic,
                                          bool IsConstexpr) {
  // Parse initializer if present
  Expr *Init = nullptr;
  
  if (Tok.is(TokenKind::equal)) {
    consumeToken();
    Init = parseExpression();
  } else if (Tok.is(TokenKind::l_paren)) {
    // Direct initialization: int x(10);
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

    // Create CXXConstructExpr for direct initialization
    Init = Context.create<CXXConstructExpr>(Loc, Args);
  } else if (Tok.is(TokenKind::l_brace)) {
    // Brace initialization: int x{10}; or int x = {10};
    Init = parseInitializerList();
    if (!Init) {
      return nullptr;
    }
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
                                               SourceLocation Loc, bool IsStatic,
                                               bool IsConstexpr, bool IsInline) {
  // Parse parameter list
  llvm::SmallVector<ParmVarDecl *, 8> Params;
  unsigned ParamIndex = 0;

  expectAndConsume(TokenKind::l_paren, "expected '(' in function declaration");

  while (!Tok.is(TokenKind::r_paren)) {
    ParmVarDecl *Param = parseParameterDeclaration(ParamIndex);
    if (Param) {
      Params.push_back(Param);
      ++ParamIndex;
    }

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

  // Create proper FunctionType
  QualType FuncType = Context.getFunctionType(ReturnType.getTypePtr(),
                                               ParamTypes, false);

  // Create FunctionDecl with proper function type
  return Context.create<FunctionDecl>(Loc, Name, FuncType, Params, Body);
}

/// parseParameterDeclaration - Parse a parameter declaration.
///
/// parameter-declaration ::= decl-specifier-seq declarator?
///                         | decl-specifier-seq declarator '=' assignment-expression
///
ParmVarDecl *Parser::parseParameterDeclaration(unsigned Index) {
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

  // Create ParmVarDecl with the correct index
  return Context.create<ParmVarDecl>(NameLoc, Name, Type, Index, DefaultArg);
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

  // Check for template specialization arguments (e.g., Vector<T*>)
  // This is used for partial and explicit specializations
  llvm::SmallVector<TemplateArgument, 4> TemplateArgs;
  if (Tok.is(TokenKind::less)) {
    // Parse template argument list
    TemplateArgs = parseTemplateArgumentList();
  }

  // Create CXXRecordDecl
  CXXRecordDecl *Class = Context.create<CXXRecordDecl>(NameLoc, Name, TagDecl::TK_class);
  
  // Note: TemplateArgs are stored but not yet used in CXXRecordDecl
  // This can be added later if needed for template specialization
  (void)TemplateArgs; // Suppress unused warning
  
  // Add class to current scope before parsing body
  if (CurrentScope) {
    CurrentScope->addDecl(Class);
  }

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
  pushScope(ScopeFlags::ClassScope);
  parseClassBody(Class);
  popScope();

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
///                    | 'struct' identifier base-clause? '{' member-specification? '}'
CXXRecordDecl *Parser::parseStructDeclaration(SourceLocation StructLoc) {
  // Parse struct name (optional)
  llvm::StringRef Name;
  SourceLocation NameLoc;

  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();
  }

  // Create CXXRecordDecl (struct has public default access)
  CXXRecordDecl *Struct = Context.create<CXXRecordDecl>(NameLoc, Name, TagDecl::TK_struct);

  // Add struct to current scope before parsing body
  if (CurrentScope) {
    CurrentScope->addDecl(Struct);
  }

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
            return Context.create<UsingDecl>(UsingLoc, First, llvm::StringRef(First.str() + "::"),
                                             true, true);
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
      return Context.create<UsingDecl>(FirstLoc, First);
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

  // Parse storage class specifiers (static, mutable) and virtual
  bool IsStatic = false;
  bool IsMutable = false;
  bool IsVirtual = false;

  while (Tok.is(TokenKind::kw_static) || Tok.is(TokenKind::kw_mutable) ||
         Tok.is(TokenKind::kw_virtual)) {
    if (Tok.is(TokenKind::kw_static)) {
      IsStatic = true;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_mutable)) {
      IsMutable = true;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_virtual)) {
      IsVirtual = true;
      consumeToken();
    }
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

    // Create CXXMethodDecl
    AccessSpecifier Access =
        static_cast<AccessSpecifier>(Class->getCurrentAccess());
    CXXMethodDecl *Method = Context.create<CXXMethodDecl>(NameLoc, Name, Type, Params, Class, Body,
                                         IsStatic, IsConst, IsVolatile, IsVirtual, IsPureVirtual,
                                         IsOverride, IsFinal, IsDefaulted, IsDeleted,
                                         RefQual, HasNoexceptSpec, NoexceptValue, NoexceptExpr,
                                         Access);

    // Add method to current scope
    if (CurrentScope) {
      CurrentScope->addDecl(Method);
    }

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
    VarDecl *VD = Context.create<VarDecl>(NameLoc, Name, Type, InClassInit, true);
    if (CurrentScope) {
      CurrentScope->addDecl(VD);
    }
    return VD;
  }
  
  AccessSpecifier Access =
      static_cast<AccessSpecifier>(Class->getCurrentAccess());
  return Context.create<FieldDecl>(NameLoc, Name, Type, BitWidth, IsMutable, InClassInit, Access);
}

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

//===----------------------------------------------------------------------===//
// Template Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseTemplateDeclaration - Parse a template declaration.
///
/// template-declaration ::= 'template' '<' template-parameter-list '>' declaration
///                        | 'template' '<' '>' declaration  (explicit specialization)
///                        | 'template' declaration           (explicit instantiation)
TemplateDecl *Parser::parseTemplateDeclaration() {
  // Expect 'template' keyword
  if (!Tok.is(TokenKind::kw_template)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  SourceLocation TemplateLoc = Tok.getLocation();
  consumeToken(); // consume 'template'

  // Check for explicit instantiation: template class Vector<int>;
  // This is when 'template' is not followed by '<'
  if (!Tok.is(TokenKind::less)) {
    // Explicit instantiation
    Decl *InstantiatedDecl = parseDeclaration();
    if (!InstantiatedDecl) {
      return nullptr;
    }

    // Create a TemplateDecl for the instantiation
    // Note: In a real compiler, this would be marked as an explicit instantiation
    TemplateDecl *Template = Context.create<TemplateDecl>(TemplateLoc, "", InstantiatedDecl);
    return Template;
  }

  consumeToken(); // consume '<'

  // Check for explicit specialization: template<> class Vector<int> {}
  if (Tok.is(TokenKind::greater)) {
    consumeToken(); // consume '>'

    // Parse the specialized declaration
    Decl *SpecializedDecl = parseDeclaration();
    if (!SpecializedDecl) {
      return nullptr;
    }

    // Create appropriate specialization declaration based on the specialized type
    TemplateDecl *Template = nullptr;
    
    if (auto *ClassDecl = llvm::dyn_cast<CXXRecordDecl>(SpecializedDecl)) {
      // Class template specialization
      // Note: We need to find the primary template and create a ClassTemplateSpecializationDecl
      // For now, create a generic TemplateDecl - full implementation requires symbol table lookup
      Template = Context.create<TemplateDecl>(TemplateLoc, ClassDecl->getName(), SpecializedDecl);
    } else if (auto *VD = llvm::dyn_cast<VarDecl>(SpecializedDecl)) {
      // Variable template specialization
      Template = Context.create<TemplateDecl>(TemplateLoc, VD->getName(), SpecializedDecl);
    } else if (auto *FuncDecl = llvm::dyn_cast<FunctionDecl>(SpecializedDecl)) {
      // Function template specialization
      Template = Context.create<TemplateDecl>(TemplateLoc, FuncDecl->getName(), SpecializedDecl);
    } else {
      // Fallback for other types
      Template = Context.create<TemplateDecl>(TemplateLoc, "", SpecializedDecl);
    }
    
    return Template;
  }

  // Parse template parameters
  llvm::SmallVector<NamedDecl *, 8> Params;
  parseTemplateParameters(Params);

  // Expect '>'
  if (!Tok.is(TokenKind::greater)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume '>'

  // Check for concept definition (C++20)
  if (Tok.is(TokenKind::kw_concept)) {
    ConceptDecl *Concept = parseConceptDefinition(TemplateLoc, Params);
    if (!Concept) {
      return nullptr;
    }
    // Return the concept's template
    return Concept->getTemplate();
  }

  // Check for requires-clause (C++20)
  Expr *RequiresClause = nullptr;
  if (Tok.is(TokenKind::kw_requires)) {
    RequiresClause = parseRequiresClause();
  }

  // Parse the templated declaration
  Decl *TemplatedDecl = parseDeclaration();
  if (!TemplatedDecl) {
    return nullptr;
  }

  // Create appropriate template type based on the templated declaration
  TemplateDecl *Template = nullptr;
  
  if (auto *FuncDecl = llvm::dyn_cast<FunctionDecl>(TemplatedDecl)) {
    // Function template
    Template = Context.create<FunctionTemplateDecl>(TemplateLoc, FuncDecl->getName(), TemplatedDecl);
  } else if (auto *ClassDecl = llvm::dyn_cast<CXXRecordDecl>(TemplatedDecl)) {
    // Class template
    Template = Context.create<ClassTemplateDecl>(TemplateLoc, ClassDecl->getName(), TemplatedDecl);
  } else if (auto *VD = llvm::dyn_cast<VarDecl>(TemplatedDecl)) {
    // Variable template
    Template = Context.create<VarTemplateDecl>(TemplateLoc, VD->getName(), TemplatedDecl);
  } else if (auto *TAD = llvm::dyn_cast<TypeAliasDecl>(TemplatedDecl)) {
    // Type alias template
    Template = Context.create<TypeAliasTemplateDecl>(TemplateLoc, TAD->getName(), TemplatedDecl);
  } else {
    // Fallback for other types (e.g., ConceptDecl already wraps itself)
    Template = Context.create<TemplateDecl>(TemplateLoc, "", TemplatedDecl);
  }

  // Add template parameters
  for (auto *Param : Params) {
    Template->addTemplateParameter(Param);
  }

  // Set requires-clause if present
  if (RequiresClause) {
    Template->setRequiresClause(RequiresClause);
  }

  return Template;
}

/// parseTemplateParameters - Parse template parameter list.
///
/// template-parameter-list ::= template-parameter (',' template-parameter)*
void Parser::parseTemplateParameters(llvm::SmallVector<NamedDecl *, 8> &Params) {
  while (!Tok.is(TokenKind::greater) && !Tok.is(TokenKind::eof)) {
    NamedDecl *Param = parseTemplateParameter();
    if (Param) {
      Params.push_back(Param);
    } else {
      // Error recovery: skip to ',' or '>'
      skipUntil({TokenKind::comma, TokenKind::greater});
    }

    // Check for comma
    if (Tok.is(TokenKind::comma)) {
      consumeToken();
    } else {
      break;
    }
  }
}

/// parseTemplateParameter - Parse a template parameter.
///
/// template-parameter ::= type-parameter | parameter-declaration
NamedDecl *Parser::parseTemplateParameter() {
  // Check for type parameter (typename, class)
  if (Tok.is(TokenKind::kw_typename) || Tok.is(TokenKind::kw_class)) {
    return parseTemplateTypeParameter();
  }

  // Check for template template parameter
  if (Tok.is(TokenKind::kw_template)) {
    return parseTemplateTemplateParameter();
  }

  // Otherwise, it's a non-type template parameter
  return parseNonTypeTemplateParameter();
}

/// parseTemplateTypeParameter - Parse a template type parameter.
///
/// type-parameter ::= 'typename' identifier? ('=' type-id)?
///                  | 'typename' '...' identifier?
///                  | 'class' identifier? ('=' type-id)?
///                  | 'class' '...' identifier?
TemplateTypeParmDecl *Parser::parseTemplateTypeParameter() {
  bool IsTypename = Tok.is(TokenKind::kw_typename);
  consumeToken(); // consume 'typename' or 'class'

  // Check for parameter pack
  bool IsParameterPack = false;
  if (Tok.is(TokenKind::ellipsis)) {
    IsParameterPack = true;
    consumeToken(); // consume '...'
  }

  // Parse identifier (optional)
  llvm::StringRef Name;
  SourceLocation NameLoc;

  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();
  }

  // Create TemplateTypeParmDecl
  // Use 0 for depth and index (will be set correctly later)
  TemplateTypeParmDecl *Param = Context.create<TemplateTypeParmDecl>(
      NameLoc, Name, 0, 0, IsParameterPack, IsTypename);

  // Parse default argument (optional)
  if (Tok.is(TokenKind::equal)) {
    consumeToken(); // consume '='
    QualType DefaultType = parseType();
    if (!DefaultType.isNull()) {
      Param->setDefaultArgument(DefaultType);
    }
  }

  return Param;
}

/// parseNonTypeTemplateParameter - Parse a non-type template parameter.
///
/// parameter-declaration ::= decl-specifier-seq declarator ('=' assignment-expression)?
///                          | decl-specifier-seq declarator '...'
NonTypeTemplateParmDecl *Parser::parseNonTypeTemplateParameter() {
  // Parse type
  QualType Type = parseType();
  if (Type.isNull()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }

  // Parse declarator (handles pointers, references, arrays, etc.)
  Type = parseDeclarator(Type);
  if (Type.isNull()) {
    return nullptr;
  }

  // Check for parameter pack (can appear before or after the name)
  bool IsParameterPack = false;
  if (Tok.is(TokenKind::ellipsis)) {
    IsParameterPack = true;
    consumeToken(); // consume '...'
  }

  // Parse identifier (optional for unnamed parameters)
  llvm::StringRef Name;
  SourceLocation NameLoc;

  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();

    // Check for parameter pack after name (alternative syntax)
    if (!IsParameterPack && Tok.is(TokenKind::ellipsis)) {
      IsParameterPack = true;
      consumeToken(); // consume '...'
    }
  }

  // Create NonTypeTemplateParmDecl
  // Use 0 for depth and index (will be set correctly later)
  NonTypeTemplateParmDecl *Param = Context.create<NonTypeTemplateParmDecl>(
      NameLoc, Name, Type, 0, 0, IsParameterPack);

  // Parse default argument (optional)
  if (Tok.is(TokenKind::equal)) {
    consumeToken(); // consume '='
    Expr *DefaultArg = parseExpression();
    if (DefaultArg) {
      Param->setDefaultArgument(DefaultArg);
    }
  }

  return Param;
}

/// parseTemplateTemplateParameter - Parse a template template parameter.
///
/// template-template-parameter ::= 'template' '<' template-parameter-list '>' type-constraint? 'class' identifier? ('=' id-expression)?
/// type-constraint ::= 'requires' constraint-expression (C++20)
TemplateTemplateParmDecl *Parser::parseTemplateTemplateParameter() {
  // Expect 'template'
  if (!Tok.is(TokenKind::kw_template)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume 'template'

  // Expect '<'
  if (!Tok.is(TokenKind::less)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume '<'

  // Parse template parameters
  llvm::SmallVector<NamedDecl *, 8> Params;
  parseTemplateParameters(Params);

  // Expect '>'
  if (!Tok.is(TokenKind::greater)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume '>'

  // Parse optional requires-clause (C++20)
  // type-constraint ::= 'requires' constraint-expression
  Expr *Constraint = nullptr;
  if (Tok.is(TokenKind::kw_requires)) {
    consumeToken(); // consume 'requires'
    // Parse constraint expression
    Constraint = parseConstraintExpression();
  }

  // Expect 'class' or 'typename'
  if (!Tok.is(TokenKind::kw_class) && !Tok.is(TokenKind::kw_typename)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume 'class' or 'typename'

  // Check for parameter pack
  bool IsParameterPack = false;
  if (Tok.is(TokenKind::ellipsis)) {
    IsParameterPack = true;
    consumeToken(); // consume '...'
  }

  // Parse identifier (optional)
  llvm::StringRef Name;
  SourceLocation NameLoc;

  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();
  }

  // Create TemplateTemplateParmDecl
  // Use 0 for depth and index (will be set correctly later)
  TemplateTemplateParmDecl *Param = Context.create<TemplateTemplateParmDecl>(
      NameLoc, Name, 0, 0, IsParameterPack);

  // Set constraint if present
  if (Constraint) {
    Param->setConstraint(Constraint);
  }

  // Add template parameters
  for (auto *P : Params) {
    Param->addTemplateParameter(P);
  }

  // Parse default argument (optional)
  // For template template parameter, default is a template name
  if (Tok.is(TokenKind::equal)) {
    consumeToken(); // consume '='
    
    // Parse nested-name-specifier (optional)
    llvm::StringRef Qualifier = parseNestedNameSpecifier();
    
    // Parse template name
    if (Tok.is(TokenKind::identifier)) {
      llvm::StringRef TemplateName = Tok.getText();
      SourceLocation TemplateNameLoc = Tok.getLocation();
      consumeToken();
      
      // Look up the template in the symbol table
      TemplateDecl *DefaultTemplate = nullptr;
      if (CurrentScope) {
        if (NamedDecl *Found = CurrentScope->lookup(TemplateName)) {
          DefaultTemplate = llvm::dyn_cast<TemplateDecl>(Found);
        }
      }
      
      // If not found, create a placeholder TemplateDecl
      if (!DefaultTemplate) {
        DefaultTemplate = Context.create<TemplateDecl>(
            TemplateNameLoc, TemplateName, nullptr);
      }
      
      Param->setDefaultArgument(DefaultTemplate);
    } else {
      emitError(DiagID::err_expected_identifier);
    }
  }

  return Param;
}

/// parseTemplateArgument - Parse a template argument.
///
/// template-argument ::= type-id | constant-expression | id-expression
///                     | '...' identifier  (pack expansion)
TemplateArgument Parser::parseTemplateArgument() {
  // Check for pack expansion: ...Args
  if (Tok.is(TokenKind::ellipsis)) {
    consumeToken(); // consume '...'

    // Parse the pack pattern (could be a type or expression)
    TemplateArgument Pattern = parseTemplateArgument();
    // ✅ Mark this as a pack expansion
    Pattern.setPackExpansion(true);
    return Pattern;
  }

  // Try to parse as a type first using tentative parsing
  // We need to determine if this is a type or an expression
  // Use tentative parsing instead of heuristics

  // Save current state for tentative parsing
  TentativeParsingAction TPA(*this);

  // Try to parse as type
  QualType Type = parseType();

  // Check if parsing succeeded and we're at a valid position for template argument
  // Valid positions: ',', '>', '>>', '...', or end of template argument list
  if (!Type.isNull() && (Tok.is(TokenKind::comma) ||
                         Tok.is(TokenKind::greater) ||
                         Tok.is(TokenKind::greatergreater) ||
                         Tok.is(TokenKind::ellipsis))) {
    // Successfully parsed as type
    TPA.commit();

    // Check for pack expansion after type: Type...
    bool IsPackExpansion = false;
    if (Tok.is(TokenKind::ellipsis)) {
      consumeToken(); // consume '...'
      IsPackExpansion = true;
    }

    TemplateArgument Arg(Type);
    if (IsPackExpansion) {
      Arg.setPackExpansion(true);
    }
    return Arg;
  }

  // Failed to parse as type, backtrack and try as expression
  TPA.abort();

  // Parse as expression (could be a constant or id-expression)
  Expr *E = parseExpression();

  // Check for pack expansion after expression: expr...
  bool IsPackExpansion = false;
  if (E && Tok.is(TokenKind::ellipsis)) {
    consumeToken(); // consume '...'
    IsPackExpansion = true;
  }

  if (E) {
    TemplateArgument Arg(E);
    if (IsPackExpansion) {
      Arg.setPackExpansion(true);
    }
    return Arg;
  }
  return TemplateArgument(static_cast<Expr *>(nullptr));
}

/// parseTemplateArgumentList - Parse a template argument list.
///
/// template-argument-list ::= template-argument (',' template-argument)*
llvm::SmallVector<TemplateArgument, 4> Parser::parseTemplateArgumentList() {
  llvm::SmallVector<TemplateArgument, 4> Args;

  while (!Tok.is(TokenKind::greater) && !Tok.is(TokenKind::eof)) {
    TemplateArgument Arg = parseTemplateArgument();
    Args.push_back(Arg);

    // Check for comma
    if (Tok.is(TokenKind::comma)) {
      consumeToken();
    } else {
      break;
    }
  }

  return Args;
}

/// parseTemplateId - Parse a template-id (e.g., Vector<int>).
///
/// template-id ::= identifier '<' template-argument-list? '>'
TemplateSpecializationType *Parser::parseTemplateId(llvm::StringRef Name) {
  SourceLocation NameLoc = Tok.getLocation();

  // Expect '<'
  if (!Tok.is(TokenKind::less)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume '<'

  // Parse template arguments
  llvm::SmallVector<TemplateArgument, 4> Args;
  if (!Tok.is(TokenKind::greater)) {
    Args = parseTemplateArgumentList();
  }

  // Expect '>'
  if (!Tok.is(TokenKind::greater)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume '>'

  // Create TemplateSpecializationType
  // Note: TemplateSpecializationType is not an ASTNode, so we create it directly
  TemplateSpecializationType *SpecType = new TemplateSpecializationType(Name, nullptr);

  // Add template arguments
  for (const TemplateArgument &Arg : Args) {
    SpecType->addTemplateArg(Arg);
  }

  return SpecType;
}

//===----------------------------------------------------------------------===//
// Namespace Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseNamespaceDeclaration - Parse a namespace declaration.
///
/// namespace-definition ::= 'namespace' identifier? '{' namespace-body '}'
///                       | 'inline' 'namespace' identifier '{' namespace-body '}'
///                       | 'namespace' identifier '::' identifier '{' namespace-body '}'
///                       | 'namespace' identifier '=' qualified-namespace-specifier ';'
Decl *Parser::parseNamespaceDeclaration() {
  // Check for 'inline' keyword
  bool IsInline = false;
  if (Tok.is(TokenKind::kw_inline)) {
    IsInline = true;
    consumeToken();
  }

  // Expect 'namespace' keyword
  if (!Tok.is(TokenKind::kw_namespace)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  SourceLocation NamespaceLoc = Tok.getLocation();
  consumeToken(); // consume 'namespace'

  // Parse namespace name (optional for anonymous namespace)
  llvm::StringRef Name;
  SourceLocation NameLoc;

  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();

    // Check for namespace alias: namespace AB = A::B;
    if (Tok.is(TokenKind::equal)) {
      // Parse as namespace alias, passing the already-parsed alias name
      return parseNamespaceAlias(Name, NameLoc);
    }

    // Check for nested namespace definition (C++17): namespace A::B::C { ... }
    if (Tok.is(TokenKind::coloncolon)) {
      // Parse nested namespace definition
      llvm::SmallVector<std::pair<llvm::StringRef, SourceLocation>, 4> Names;
      Names.push_back({Name, NameLoc});

      while (Tok.is(TokenKind::coloncolon)) {
        consumeToken(); // consume '::'

        if (!Tok.is(TokenKind::identifier)) {
          emitError(DiagID::err_expected_identifier);
          return nullptr;
        }

        Names.push_back({Tok.getText(), Tok.getLocation()});
        consumeToken();
      }

      // Create nested namespaces (innermost first, then wrap in outer)
      NamespaceDecl *InnerNS = nullptr;
      for (auto It = Names.rbegin(); It != Names.rend(); ++It) {
        NamespaceDecl *NS = Context.create<NamespaceDecl>(It->second, It->first, IsInline && (It == Names.rend() - 1));
        if (InnerNS) {
          NS->addDecl(InnerNS);
        }
        InnerNS = NS;
      }

      // Expect '{'
      if (!Tok.is(TokenKind::l_brace)) {
        emitError(DiagID::err_expected_lbrace);
        return InnerNS;
      }

      consumeToken(); // consume '{'

      // Parse namespace body for the innermost namespace
      parseNamespaceBody(InnerNS);

      // Expect '}'
      if (!Tok.is(TokenKind::r_brace)) {
        emitError(DiagID::err_expected_rbrace);
        return InnerNS;
      }

      consumeToken(); // consume '}'

      // Return the outermost namespace
      return Names.size() == 1 ? InnerNS : InnerNS;
    }
  }

  // Create NamespaceDecl
  NamespaceDecl *NS = Context.create<NamespaceDecl>(NameLoc, Name, IsInline);

  // Expect '{'
  if (!Tok.is(TokenKind::l_brace)) {
    emitError(DiagID::err_expected_lbrace);
    return NS;
  }

  consumeToken(); // consume '{'

  // Parse namespace body
  parseNamespaceBody(NS);

  // Expect '}'
  if (!Tok.is(TokenKind::r_brace)) {
    emitError(DiagID::err_expected_rbrace);
    return NS;
  }

  consumeToken(); // consume '}'
  return NS;
}

/// parseNamespaceBody - Parse a namespace body.
///
/// namespace-body ::= declaration*
void Parser::parseNamespaceBody(NamespaceDecl *NS) {
  while (!Tok.is(TokenKind::r_brace) && !Tok.is(TokenKind::eof)) {
    Decl *D = parseDeclaration();
    if (D) {
      NS->addDecl(D);
    } else {
      // Error recovery: skip to next ';' or '}'
      skipUntil({TokenKind::semicolon, TokenKind::r_brace});
      if (Tok.is(TokenKind::semicolon)) {
        consumeToken();
      }
    }
  }
}

/// parseUsingDeclaration - Parse a using declaration.
///
/// using-declaration ::= 'using' using-declarator-list ';'
/// using-declarator-list ::= using-declarator (',' using-declarator)*
/// using-declarator ::= 'typename'? nested-name-specifier unqualified-id
///                      | 'using' 'enum' enum-name ';'
Decl *Parser::parseUsingDeclaration() {
  // Expect 'using' keyword
  if (!Tok.is(TokenKind::kw_using)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  SourceLocation UsingLoc = Tok.getLocation();
  consumeToken(); // consume 'using'

  // Check for 'typename' keyword (optional)
  if (Tok.is(TokenKind::kw_typename)) {
    consumeToken(); // consume 'typename'
  }

  // Parse nested-name-specifier (optional)
  llvm::StringRef NestedName = parseNestedNameSpecifier();
  bool HasNested = !NestedName.empty();

  // Check for 'enum' keyword (C++20 using enum)
  if (Tok.is(TokenKind::kw_enum)) {
    consumeToken(); // consume 'enum'

    // Parse enum name
    if (!Tok.is(TokenKind::identifier)) {
      emitError(DiagID::err_expected_identifier);
      return nullptr;
    }

    llvm::StringRef EnumName = Tok.getText();
    SourceLocation EnumNameLoc = Tok.getLocation();
    consumeToken();

    // Expect ';'
    if (!Tok.is(TokenKind::semicolon)) {
      emitError(DiagID::err_expected_semi);
      return nullptr;
    }

    consumeToken(); // consume ';'

    return Context.create<UsingEnumDecl>(EnumNameLoc, EnumName, NestedName, HasNested);
  }

  // Parse unqualified-id
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  llvm::StringRef Name = Tok.getText();
  SourceLocation NameLoc = Tok.getLocation();
  consumeToken();

  // Expect ';'
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }

  consumeToken(); // consume ';'

  return Context.create<UsingDecl>(NameLoc, Name, NestedName, HasNested);
}

/// parseUsingDirective - Parse a using directive.
///
/// using-directive ::= 'using' 'namespace' nested-name-specifier? namespace-name ';'
UsingDirectiveDecl *Parser::parseUsingDirective() {
  // Expect 'using' keyword
  if (!Tok.is(TokenKind::kw_using)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  SourceLocation UsingLoc = Tok.getLocation();
  consumeToken(); // consume 'using'

  // Expect 'namespace' keyword
  if (!Tok.is(TokenKind::kw_namespace)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume 'namespace'

  // Parse nested-name-specifier (optional)
  llvm::StringRef NestedName = parseNestedNameSpecifier();
  bool HasNested = !NestedName.empty();

  // Parse namespace name
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  llvm::StringRef Name = Tok.getText();
  SourceLocation NameLoc = Tok.getLocation();
  consumeToken();

  // Expect ';'
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }

  consumeToken(); // consume ';'

  return Context.create<UsingDirectiveDecl>(NameLoc, Name, NestedName, HasNested);
}

/// parseNamespaceAlias - Parse a namespace alias declaration.
///
/// namespace-alias ::= 'namespace' identifier '=' qualified-namespace-specifier ';'
/// qualified-namespace-specifier ::= nested-name-specifier? namespace-name
///
/// If AliasName and AliasLoc are provided, the alias name has already been parsed.
NamespaceAliasDecl *Parser::parseNamespaceAlias(llvm::StringRef AliasName,
                                                SourceLocation AliasLoc) {
  // Parse alias name if not already provided
  if (AliasName.empty()) {
    if (!Tok.is(TokenKind::identifier)) {
      emitError(DiagID::err_expected_identifier);
      return nullptr;
    }

    AliasName = Tok.getText();
    AliasLoc = Tok.getLocation();
    consumeToken();
  }

  // Expect '='
  if (!Tok.is(TokenKind::equal)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume '='

  // Parse nested-name-specifier (optional)
  llvm::StringRef NestedName = parseNestedNameSpecifier();

  // Parse target namespace name
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  llvm::StringRef TargetName = Tok.getText();
  consumeToken();

  // Expect ';'
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }

  consumeToken(); // consume ';'

  return Context.create<NamespaceAliasDecl>(AliasLoc, AliasName, TargetName, NestedName);
}

//===----------------------------------------------------------------------===//
// Module Declaration Parsing (C++20)
//===----------------------------------------------------------------------===//

/// parseModuleDeclaration - Parse a module declaration.
///
/// module-declaration ::= 'export'? 'module' module-name module-partition? attribute-specifier-seq? ';'
///                      | 'module' ';'                                    // global module fragment
///                      | 'module' ':' 'private' attribute-specifier-seq? ';' // private module fragment
/// module-name ::= identifier ('.' identifier)*
/// module-partition ::= ':' identifier
ModuleDecl *Parser::parseModuleDeclaration() {
  // Check for 'export' keyword (optional)
  bool IsExported = false;
  if (Tok.is(TokenKind::kw_export)) {
    IsExported = true;
    consumeToken();
  }

  // Expect 'module' keyword
  if (!Tok.is(TokenKind::kw_module)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  SourceLocation ModuleLoc = Tok.getLocation();
  consumeToken(); // consume 'module'

  // Check for global module fragment: module;
  // This appears at the beginning of a module unit, before any declarations
  if (Tok.is(TokenKind::semicolon)) {
    consumeToken(); // consume ';'
    // Create a ModuleDecl for global module fragment
    return Context.create<ModuleDecl>(ModuleLoc, "", IsExported, "", false, true, false);
  }

  // Check for private module fragment: module :private;
  if (Tok.is(TokenKind::colon)) {
    consumeToken(); // consume ':'

    // Expect 'private' keyword
    if (!Tok.is(TokenKind::kw_private)) {
      emitError(DiagID::err_expected);
      return nullptr;
    }
    consumeToken(); // consume 'private'

    // Parse optional attributes
    while (Tok.is(TokenKind::l_square)) {
      consumeToken(); // consume '['
      if (!Tok.is(TokenKind::l_square)) {
        emitError(DiagID::err_expected);
        skipUntil({TokenKind::r_square});
        if (Tok.is(TokenKind::r_square)) {
          consumeToken();
        }
        continue;
      }
      consumeToken(); // consume second '['

      while (!Tok.is(TokenKind::r_square) && !Tok.is(TokenKind::eof)) {
        consumeToken();
      }

      if (!Tok.is(TokenKind::r_square)) {
        emitError(DiagID::err_expected);
        break;
      }
      consumeToken(); // consume first ']'

      if (!Tok.is(TokenKind::r_square)) {
        emitError(DiagID::err_expected);
        break;
      }
      consumeToken(); // consume second ']'
    }

    // Expect ';'
    if (!Tok.is(TokenKind::semicolon)) {
      emitError(DiagID::err_expected_semi);
      return nullptr;
    }
    consumeToken(); // consume ';'

    // Create a ModuleDecl for private module fragment
    return Context.create<ModuleDecl>(ModuleLoc, "", IsExported, "", false, false, true);
  }

  // Parse module name
  llvm::StringRef FullModuleName = parseModuleName();

  // Parse module partition (optional)
  llvm::StringRef PartitionName;
  bool IsModulePartition = false;
  if (Tok.is(TokenKind::colon)) {
    IsModulePartition = true;
    PartitionName = parseModulePartition();
  }

  // Create ModuleDecl with full module name
  ModuleDecl *MD = Context.create<ModuleDecl>(ModuleLoc, FullModuleName, IsExported, PartitionName, IsModulePartition);

  // Parse attribute-specifier-seq (optional)
  // Attributes are specified using [[...]] syntax
  while (Tok.is(TokenKind::l_square)) {
    // Parse attribute specifier
    consumeToken(); // consume '['

    if (!Tok.is(TokenKind::l_square)) {
      emitError(DiagID::err_expected);
      // Error recovery: skip to ']'
      skipUntil({TokenKind::r_square});
      if (Tok.is(TokenKind::r_square)) {
        consumeToken();
      }
      continue;
    }

    consumeToken(); // consume second '['

    // Parse attribute list
    while (!Tok.is(TokenKind::r_square) && !Tok.is(TokenKind::eof)) {
      // Parse attribute name
      if (Tok.is(TokenKind::identifier)) {
        std::string AttrName = Tok.getText().str();
        consumeToken();

        // Parse optional attribute argument clause
        if (Tok.is(TokenKind::l_paren)) {
          consumeToken(); // consume '('
          while (!Tok.is(TokenKind::r_paren) && !Tok.is(TokenKind::eof)) {
            AttrName += Tok.getText().str();
            consumeToken();
          }
          if (Tok.is(TokenKind::r_paren)) {
            AttrName += ")";
            consumeToken(); // consume ')'
          }
        }

        // Store the attribute in ModuleDecl
        MD->addAttribute(llvm::StringRef(AttrName));
      } else {
        consumeToken();
      }
    }

    if (!Tok.is(TokenKind::r_square)) {
      emitError(DiagID::err_expected);
      break;
    }
    consumeToken(); // consume first ']'

    if (!Tok.is(TokenKind::r_square)) {
      emitError(DiagID::err_expected);
      break;
    }
    consumeToken(); // consume second ']'
  }

  // Expect ';'
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }

  consumeToken(); // consume ';'

  return MD;
}

/// parseImportDeclaration - Parse an import declaration.
///
/// module-import ::= 'export'? 'import' module-name module-partition? ';'
///                 | 'export'? 'import' header-name ';'
/// header-name ::= '<' header-name-tokens '>' | '"' header-name-tokens '"'
ImportDecl *Parser::parseImportDeclaration() {
  // Check for 'export' keyword (optional)
  bool IsExported = false;
  if (Tok.is(TokenKind::kw_export)) {
    IsExported = true;
    consumeToken();
  }

  // Expect 'import' keyword
  if (!Tok.is(TokenKind::kw_import)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  SourceLocation ImportLoc = Tok.getLocation();
  consumeToken(); // consume 'import'

  // Check for header import: import <header> or import "header"
  llvm::StringRef HeaderName;
  bool IsHeaderImport = false;

  if (Tok.is(TokenKind::less)) {
    // System header import: import <iostream>
    IsHeaderImport = true;
    consumeToken(); // consume '<'

    // Parse header name (until '>')
    std::string Header;
    while (!Tok.is(TokenKind::greater) && !Tok.is(TokenKind::eof)) {
      Header += Tok.getText().str();
      consumeToken();
    }

    if (!Tok.is(TokenKind::greater)) {
      emitError(DiagID::err_expected);
      return nullptr;
    }
    consumeToken(); // consume '>'

    // Store header name properly
    HeaderName = Context.saveString(Header);

  } else if (Tok.is(TokenKind::string_literal)) {
    // User header import: import "header.h"
    IsHeaderImport = true;
    HeaderName = Tok.getText();
    consumeToken(); // consume string literal
  }

  if (IsHeaderImport) {
    // Expect ';'
    if (!Tok.is(TokenKind::semicolon)) {
      emitError(DiagID::err_expected_semi);
      return nullptr;
    }
    consumeToken(); // consume ';'

    // Create ImportDecl for header import
    return Context.create<ImportDecl>(ImportLoc, "", IsExported, "", HeaderName, true);
  }

  // Parse module name
  llvm::StringRef ModuleName = parseModuleName();

  // Parse module partition (optional)
  llvm::StringRef PartitionName;
  if (Tok.is(TokenKind::colon)) {
    PartitionName = parseModulePartition();
  }

  // Expect ';'
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }

  consumeToken(); // consume ';'

  return Context.create<ImportDecl>(ImportLoc, ModuleName, IsExported, PartitionName);
}

/// parseExportDeclaration - Parse an export declaration.
///
/// export-declaration ::= 'export' declaration
ExportDecl *Parser::parseExportDeclaration() {
  // Expect 'export' keyword
  if (!Tok.is(TokenKind::kw_export)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  SourceLocation ExportLoc = Tok.getLocation();
  consumeToken(); // consume 'export'

  // Parse the exported declaration
  Decl *ExportedDecl = parseDeclaration();
  if (!ExportedDecl) {
    return nullptr;
  }

  return Context.create<ExportDecl>(ExportLoc, ExportedDecl);
}

/// parseModuleName - Parse a module name.
///
/// module-name ::= identifier ('.' identifier)*
llvm::StringRef Parser::parseModuleName() {
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return "";
  }

  llvm::StringRef ModuleName = Tok.getText();
  SourceLocation NameLoc = Tok.getLocation();
  consumeToken();

  // Handle dotted module names (e.g., std.core)
  // Build the full module name by concatenating identifiers with dots
  std::string FullName = ModuleName.str();
  while (Tok.is(TokenKind::period)) {
    consumeToken(); // consume '.'

    if (!Tok.is(TokenKind::identifier)) {
      emitError(DiagID::err_expected_identifier);
      break;
    }

    FullName += ".";
    FullName += Tok.getText().str();
    consumeToken();
  }

  // Return the full module name (stored in the ASTContext)
  // Copy the full name to ASTContext for storage
  return Context.saveString(FullName);
}

/// parseModulePartition - Parse a module partition.
///
/// module-partition ::= ':' identifier
llvm::StringRef Parser::parseModulePartition() {
  // Expect ':'
  if (!Tok.is(TokenKind::colon)) {
    emitError(DiagID::err_expected_colon);
    return "";
  }

  consumeToken(); // consume ':'

  // Expect identifier
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return "";
  }

  llvm::StringRef PartitionName = Tok.getText();
  consumeToken();

  return PartitionName;
}

//===----------------------------------------------------------------------===//
// Enum Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseEnumDeclaration - Parse an enum declaration.
///
/// enum-specifier ::= 'enum' identifier? '{' enumerator-list? '}'
///                  | 'enum' identifier ':' type-specifier-seq '{' enumerator-list? '}'
///                  | 'enum' 'class' identifier? ':'? type-specifier-seq? '{' enumerator-list? '}'
EnumDecl *Parser::parseEnumDeclaration(SourceLocation EnumLoc) {
  // Check for enum class/struct (scoped enum)
  bool IsScoped = false;
  if (Tok.is(TokenKind::kw_class) || Tok.is(TokenKind::kw_struct)) {
    IsScoped = true;
    consumeToken();
  }

  // Parse enum name (optional)
  llvm::StringRef Name;
  SourceLocation NameLoc;

  if (Tok.is(TokenKind::identifier)) {
    Name = Tok.getText();
    NameLoc = Tok.getLocation();
    consumeToken();
  }

  // Create EnumDecl
  EnumDecl *Enum = Context.create<EnumDecl>(NameLoc, Name);
  Enum->setScoped(IsScoped);

  // Parse optional underlying type (enum : int)
  if (Tok.is(TokenKind::colon)) {
    consumeToken();
    QualType UnderlyingType = parseType();
    Enum->setUnderlyingType(UnderlyingType);
  }

  // Parse enum body
  if (!Tok.is(TokenKind::l_brace)) {
    emitError(DiagID::err_expected_lbrace);
    return Enum;
  }

  consumeToken(); // consume '{'
  parseEnumBody(Enum);

  if (!Tok.is(TokenKind::r_brace)) {
    emitError(DiagID::err_expected_rbrace);
    return Enum;
  }

  consumeToken(); // consume '}'

  // Expect optional semicolon
  if (Tok.is(TokenKind::semicolon)) {
    consumeToken();
  }

  return Enum;
}

/// parseEnumBody - Parse an enum body.
///
/// enumerator-list ::= enumerator (',' enumerator)*
void Parser::parseEnumBody(EnumDecl *Enum) {
  while (!Tok.is(TokenKind::r_brace) && !Tok.is(TokenKind::eof)) {
    parseEnumerator(Enum);

    if (Tok.is(TokenKind::comma)) {
      consumeToken();
    } else {
      break;
    }
  }
}

/// parseEnumerator - Parse an enumerator.
///
/// enumerator ::= identifier ('=' constant-expression)?
void Parser::parseEnumerator(EnumDecl *Enum) {
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return;
  }

  llvm::StringRef Name = Tok.getText();
  SourceLocation NameLoc = Tok.getLocation();
  consumeToken();

  // Parse optional initializer
  Expr *InitVal = nullptr;
  if (Tok.is(TokenKind::equal)) {
    consumeToken();
    InitVal = parseExpression();
  }

  // Create EnumConstantDecl
  // Note: We should determine the enum type properly
  EnumConstantDecl *Constant = Context.create<EnumConstantDecl>(
      NameLoc, Name, QualType(), InitVal);
  Enum->addEnumerator(Constant);
}

//===----------------------------------------------------------------------===//
// Union Declaration Parsing
//===----------------------------------------------------------------------===//

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
  CXXRecordDecl *Union = Context.create<CXXRecordDecl>(NameLoc, Name, TagDecl::TK_union);

  // Add union to current scope before parsing body
  if (CurrentScope) {
    CurrentScope->addDecl(Union);
  }

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
// Typedef Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseTypedefDeclaration - Parse a typedef declaration.
///
/// typedef-declaration ::= 'typedef' type-specifier-seq declarator ';'
TypedefDecl *Parser::parseTypedefDeclaration(SourceLocation TypedefLoc) {
  // Parse type
  QualType Type = parseType();
  if (Type.isNull()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }

  // Parse declarator (pointer/reference modifiers)
  Type = parseDeclarator(Type);

  // Parse identifier
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  llvm::StringRef Name = Tok.getText();
  SourceLocation NameLoc = Tok.getLocation();
  consumeToken();

  // Expect semicolon
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }
  consumeToken();

  // Create TypedefDecl
  return Context.create<TypedefDecl>(NameLoc, Name, Type);
}

//===----------------------------------------------------------------------===//
// Type Alias Declaration Parsing (C++11)
//===----------------------------------------------------------------------===//

/// parseTypeAliasDeclaration - Parse a type alias declaration.
///
/// type-alias-declaration ::= 'using' identifier '=' type-id ';'
TypeAliasDecl *Parser::parseTypeAliasDeclaration(SourceLocation UsingLoc) {
  // Expect 'using' keyword
  if (!Tok.is(TokenKind::kw_using)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume 'using'

  // Parse identifier
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  llvm::StringRef Name = Tok.getText();
  SourceLocation NameLoc = Tok.getLocation();
  consumeToken();

  // Expect '='
  if (!Tok.is(TokenKind::equal)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume '='

  // Parse underlying type
  QualType UnderlyingType = parseType();
  if (UnderlyingType.isNull()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }

  // Expect semicolon
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }
  consumeToken();

  // Create TypeAliasDecl
  return Context.create<TypeAliasDecl>(NameLoc, Name, UnderlyingType);
}

//===----------------------------------------------------------------------===//
// Static Assert Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseStaticAssertDeclaration - Parse a static_assert declaration.
///
/// static-assert-declaration ::= 'static_assert' '(' constant-expression ')' ';'
///                             | 'static_assert' '(' constant-expression ',' string-literal ')' ';'
StaticAssertDecl *Parser::parseStaticAssertDeclaration(SourceLocation Loc) {
  // Expect '('
  if (!Tok.is(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return nullptr;
  }

  consumeToken(); // consume '('

  // Parse condition expression
  Expr *CondExpr = parseExpression();
  if (!CondExpr) {
    emitError(DiagID::err_expected_expression);
    return nullptr;
  }

  // Parse optional message
  llvm::StringRef Message;
  if (Tok.is(TokenKind::comma)) {
    consumeToken(); // consume ','

    if (!Tok.is(TokenKind::string_literal)) {
      emitError(DiagID::err_expected_string_literal);
      return nullptr;
    }

    Message = Tok.getText();
    consumeToken();
  }

  // Expect ')'
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

  // Create StaticAssertDecl
  return Context.create<StaticAssertDecl>(Loc, CondExpr, Message);
}

//===----------------------------------------------------------------------===//
// Linkage Specification Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseLinkageSpecDeclaration - Parse a linkage specification.
///
/// linkage-specification ::= 'extern' string-literal '{' declaration-seq? '}'
///                        | 'extern' string-literal declaration
LinkageSpecDecl *Parser::parseLinkageSpecDeclaration(SourceLocation Loc) {
  // Parse linkage string
  if (!Tok.is(TokenKind::string_literal)) {
    emitError(DiagID::err_expected_string_literal);
    return nullptr;
  }

  llvm::StringRef LangStr = Tok.getText();
  consumeToken();

  // Determine language
  LinkageSpecDecl::Language Lang;
  if (LangStr == "\"C\"") {
    Lang = LinkageSpecDecl::C;
  } else if (LangStr == "\"C++\"") {
    Lang = LinkageSpecDecl::CXX;
  } else {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  // Check for brace or single declaration
  bool HasBraces = false;
  if (Tok.is(TokenKind::l_brace)) {
    HasBraces = true;
    consumeToken();
  }

  // Create LinkageSpecDecl
  LinkageSpecDecl *LinkageSpec = Context.create<LinkageSpecDecl>(Loc, Lang, HasBraces);

  // Parse declarations
  if (HasBraces) {
    while (!Tok.is(TokenKind::r_brace) && !Tok.is(TokenKind::eof)) {
      Decl *D = parseDeclaration();
      if (D) {
        LinkageSpec->addDecl(D);
      } else {
        // Error recovery
        skipUntil({TokenKind::semicolon, TokenKind::r_brace});
        if (Tok.is(TokenKind::semicolon)) {
          consumeToken();
        }
      }
    }

    if (!Tok.is(TokenKind::r_brace)) {
      emitError(DiagID::err_expected_rbrace);
      return LinkageSpec;
    }

    consumeToken(); // consume '}'
  } else {
    // Single declaration
    Decl *D = parseDeclaration();
    if (D) {
      LinkageSpec->addDecl(D);
    }
  }

  return LinkageSpec;
}

//===----------------------------------------------------------------------===//
// Asm Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseAsmDeclaration - Parse an asm declaration.
///
/// asm-declaration ::= 'asm' '(' string-literal ')' ';'
AsmDecl *Parser::parseAsmDeclaration(SourceLocation Loc) {
  // Expect '('
  if (!Tok.is(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return nullptr;
  }

  consumeToken(); // consume '('

  // Parse assembly string
  if (!Tok.is(TokenKind::string_literal)) {
    emitError(DiagID::err_expected_string_literal);
    return nullptr;
  }

  llvm::StringRef AsmString = Tok.getText();
  consumeToken();

  // Expect ')'
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

  // Create AsmDecl
  return Context.create<AsmDecl>(Loc, AsmString);
}

//===----------------------------------------------------------------------===//
// Deduction Guide Parsing (C++17)
//===----------------------------------------------------------------------===//

/// parseDeductionGuide - Parse a deduction guide declaration.
///
/// deduction-guide ::= 'explicit'? template-name '(' parameter-declaration-clause ')' '->' template-id ';'
CXXDeductionGuideDecl *Parser::parseDeductionGuide(SourceLocation Loc) {
  // Check for explicit specifier
  bool IsExplicit = false;
  if (Tok.is(TokenKind::kw_explicit)) {
    IsExplicit = true;
    consumeToken();
  }

  // Parse template name (class name)
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  llvm::StringRef TemplateName = Tok.getText();
  SourceLocation NameLoc = Tok.getLocation();
  consumeToken();

  // Parse '(' parameter-list ')'
  if (!Tok.is(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return nullptr;
  }

  consumeToken(); // consume '('

  // Parse parameters (simplified - just parse types)
  llvm::SmallVector<ParmVarDecl *, 8> Params;
  unsigned ParamIndex = 0;
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

  // Expect '->'
  if (!Tok.is(TokenKind::arrow)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume '->'

  // Parse template-id (simplified - just parse the type)
  QualType ReturnType = parseType();

  // Expect semicolon
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }
  consumeToken();

  // Create CXXDeductionGuideDecl
  return Context.create<CXXDeductionGuideDecl>(NameLoc, TemplateName, ReturnType,
                                                Params, IsExplicit);
}

//===----------------------------------------------------------------------===//
// Attribute Specifier Parsing (C++11)
//===----------------------------------------------------------------------===//

/// parseAttributeSpecifier - Parse an attribute specifier.
///
/// attribute-specifier ::= '[[' attribute-list ']]'
/// attribute-list ::= attribute (',' attribute)*
/// attribute ::= identifier ('(' argument-clause? ')')?
///             | identifier '::' identifier ('(' argument-clause? ')')?
AttributeListDecl *Parser::parseAttributeSpecifier(SourceLocation Loc) {
  // Expect '[['
  if (!Tok.is(TokenKind::l_square)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume '['

  if (!Tok.is(TokenKind::l_square)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume '['

  // Create AttributeListDecl to store all attributes
  AttributeListDecl *AttrList = Context.create<AttributeListDecl>(Loc);

  // Parse attribute(s) - we support multiple attributes separated by commas
  bool First = true;
  do {
    if (!First) {
      // Consume comma separator
      consumeToken();
    }
    First = false;

    llvm::StringRef Namespace;
    llvm::StringRef AttrName;
    Expr *ArgExpr = nullptr;

    // Parse attribute
    if (!Tok.is(TokenKind::identifier)) {
      emitError(DiagID::err_expected_identifier);
      return nullptr;
    }

    AttrName = Tok.getText();
    consumeToken();

    // Check for namespace::attribute syntax
    if (Tok.is(TokenKind::coloncolon)) {
      consumeToken();
      Namespace = AttrName;

      if (!Tok.is(TokenKind::identifier)) {
        emitError(DiagID::err_expected_identifier);
        return nullptr;
      }

      AttrName = Tok.getText();
      consumeToken();
    }

    // Parse optional attribute argument
    if (Tok.is(TokenKind::l_paren)) {
      consumeToken(); // consume '('

      // Parse argument expression
      if (Tok.is(TokenKind::string_literal)) {
        // Simple case: string literal
        llvm::StringRef StrValue = Tok.getText();
        consumeToken();
        // Create a StringLiteral expression
        ArgExpr = Context.create<StringLiteral>(
            SourceLocation(), StrValue);
      } else if (Tok.isNot(TokenKind::r_paren)) {
        // General case: parse as expression
        ArgExpr = parseExpression();
      }

      if (!Tok.is(TokenKind::r_paren)) {
        emitError(DiagID::err_expected_rparen);
        return nullptr;
      }

      consumeToken(); // consume ')'
    }

    // Create AttributeDecl and add to the list
    AttributeDecl *AttrDecl;
    if (Namespace.empty()) {
      AttrDecl = Context.create<AttributeDecl>(Loc, AttrName, ArgExpr);
    } else {
      AttrDecl = Context.create<AttributeDecl>(Loc, Namespace, AttrName, ArgExpr);
    }
    AttrList->addAttribute(AttrDecl);
  } while (Tok.is(TokenKind::comma));

  // Expect ']]'
  if (!Tok.is(TokenKind::r_square)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume ']'

  if (!Tok.is(TokenKind::r_square)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  consumeToken(); // consume ']'

  return AttrList;
}

//===----------------------------------------------------------------------===//
// Constructor and Destructor Parsing
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

  // Create CXXConstructorDecl
  CXXConstructorDecl *Ctor = Context.create<CXXConstructorDecl>(Loc, Class, Params, nullptr, false);

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

  // Add constructor to current scope
  if (CurrentScope) {
    CurrentScope->addDecl(Ctor);
  }

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

  // Create CXXDestructorDecl
  CXXDestructorDecl *Dtor = Context.create<CXXDestructorDecl>(Loc, Class, Body);

  // Add destructor to current scope
  if (CurrentScope) {
    CurrentScope->addDecl(Dtor);
  }

  return Dtor;
}

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

    // Look up or create the friend type
    QualType FriendType;
    if (CurrentScope) {
      if (NamedDecl *Found = CurrentScope->lookup(TypeName)) {
        if (auto *TD = llvm::dyn_cast<TypeDecl>(Found)) {
          FriendType = Context.getTypeDeclType(TD);
        }
      }
    }
    
    // If not found, create a forward declaration
    if (FriendType.isNull()) {
      // Create a forward declaration for the friend class
      RecordDecl *ForwardDecl = Context.create<RecordDecl>(
          TypeNameLoc, TypeName, TagDecl::TK_struct);
      FriendType = Context.getTypeDeclType(ForwardDecl);
    }
    
    FriendDecl *FD = Context.create<FriendDecl>(FriendLoc, nullptr, FriendType, true);

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

  // Create a FunctionDecl for the friend function
  FunctionDecl *FriendFunc = Context.create<FunctionDecl>(NameLoc, Name, Type, Params, nullptr);

  // Create FriendDecl
  return Context.create<FriendDecl>(FriendLoc, FriendFunc, QualType(), false);
}

//===----------------------------------------------------------------------===//
// Requires Clause and Constraint Expression Parsing (C++20)
//===----------------------------------------------------------------------===//

/// parseRequiresClause - Parse a requires-clause.
///
/// requires-clause ::= 'requires' constraint-expression
Expr *Parser::parseRequiresClause() {
  assert(Tok.is(TokenKind::kw_requires) && "Expected 'requires'");
  SourceLocation RequiresLoc = Tok.getLocation();
  consumeToken(); // consume 'requires'

  // Parse constraint expression
  return parseConstraintExpression();
}

/// parseConstraintExpression - Parse a constraint-expression.
///
/// constraint-expression ::= logical-or-expression
Expr *Parser::parseConstraintExpression() {
  // Parse the constraint as a logical-or expression
  // In C++20, constraints are primary expressions connected by &&
  // For simplicity, we parse it as a general expression
  return parseExpression();
}

/// parseConceptDefinition - Parse a concept definition (C++20).
///
/// concept-definition ::= 'concept' identifier '=' constraint-expression ';'
ConceptDecl *Parser::parseConceptDefinition(SourceLocation Loc,
                                            llvm::SmallVector<NamedDecl *, 8> &TemplateParams) {
  // Expect 'concept' keyword
  if (!Tok.is(TokenKind::kw_concept)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }
  consumeToken(); // consume 'concept'

  // Parse concept name
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  llvm::StringRef ConceptName = Tok.getText();
  SourceLocation ConceptNameLoc = Tok.getLocation();
  consumeToken();

  // Expect '='
  if (!Tok.is(TokenKind::equal)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }
  consumeToken(); // consume '='

  // Parse constraint expression
  Expr *Constraint = parseConstraintExpression();
  if (!Constraint) {
    emitError(DiagID::err_expected_expression);
    return nullptr;
  }

  // Expect ';'
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }
  consumeToken(); // consume ';'

  // Create TemplateDecl for the concept
  TemplateDecl *Template = Context.create<TemplateDecl>(Loc, ConceptName, nullptr);
  for (auto *Param : TemplateParams) {
    Template->addTemplateParameter(Param);
  }

  // Create ConceptDecl
  ConceptDecl *Concept = Context.create<ConceptDecl>(ConceptNameLoc, ConceptName, Constraint, Template);

  return Concept;
}

} // namespace blocktype
