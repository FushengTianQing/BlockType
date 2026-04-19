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
#include "blocktype/AST/Attr.h"
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
// Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseDeclarationStatement - Parse a declaration statement.
///
/// declaration-statement ::= declaration
///
Stmt *Parser::parseDeclarationStatement() {
  pushContext(ParsingContext::Declaration);
  
  // P7.4.3: Check for structured binding before parsing as regular declaration
  // Look ahead: if we see 'auto [', it's a structured binding
  if (Tok.is(TokenKind::kw_auto)) {
    Token NextTok = peekToken();
    if (NextTok.is(TokenKind::l_square)) {
      // This is a structured binding
      consumeToken(); // consume 'auto'
      SourceLocation AutoLoc = Tok.getLocation();
      bool IsReference = false;
      Stmt *SBDecl = parseStructuredBindingDeclaration(AutoLoc, IsReference);
      popContext();
      return SBDecl;  // Return DeclStmt directly
    }
  }
  
  Decl *D = parseDeclaration();
  
  popContext();
  
  if (!D)
    return nullptr;
  
  // Create DeclStmt
  return Actions.ActOnDeclStmtFromDecl(D).get();
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
Decl *Parser::parseDeclaration(
    llvm::SmallVector<TemplateArgument, 4> *ParsedTemplateArgs) {
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
        
        return Actions.ActOnTypeAliasDecl(UsingLoc, FirstName, TargetType).get();
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
        
        return Actions.ActOnUsingDecl(UsingLoc, "", FirstName, true).get();
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
    Decl *Result = parseClassDeclaration(ClassLoc, ParsedTemplateArgs);
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
    Decl *Result = parseStructDeclaration(StructLoc, ParsedTemplateArgs);
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

  // === General variable/function declaration path (DeclSpec + Declarator) ===

  // Parse declaration specifiers
  DeclSpec DS;
  parseDeclSpecifierSeq(DS);
  llvm::errs() << "DEBUG: parseDeclaration - DS.hasTypeSpecifier() = " 
               << (DS.hasTypeSpecifier() ? "true" : "false") << "\n";
  if (!DS.hasTypeSpecifier()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }

  // P7.4.3: Check for structured binding syntax: auto [x, y] = expr
  // After parsing 'auto', check if next token is '['
  if (Tok.is(TokenKind::l_square)) {
    // This is a structured binding declaration
    // Note: Structured bindings should be handled in parseDeclarationStatement,
    // not here. Return nullptr to indicate this is not a regular declaration.
    return nullptr;
  }

  // Parse declarator (name + pointer/reference/array/function chunks)
  Declarator D(DS, DeclaratorContext::FileContext);
  parseDeclarator(D);
  llvm::errs() << "DEBUG: parseDeclaration - D.hasName() = " 
               << (D.hasName() ? "true" : "false") << "\n";

  if (!D.hasName()) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  // Handle qualified name (e.g., ClassName::member) for out-of-class definitions
  if (Tok.is(TokenKind::coloncolon)) {
    consumeToken();
    if (!Tok.is(TokenKind::identifier)) {
      emitError(DiagID::err_expected_identifier);
      return nullptr;
    }
    // Replace the name with the qualified member name
    D.setName(Tok.getText(), Tok.getLocation());
    consumeToken();
  }

  // Determine if this is a function or variable declarator and build AST
  llvm::errs() << "DEBUG: parseDeclaration - D.isFunctionDeclarator() = " 
               << (D.isFunctionDeclarator() ? "true" : "false") << "\n";
  if (D.isFunctionDeclarator()) {
    llvm::errs() << "DEBUG: parseDeclaration - Calling buildFunctionDecl\n";
    return buildFunctionDecl(D);
  }
  llvm::errs() << "DEBUG: parseDeclaration - Calling buildVarDecl\n";
  return buildVarDecl(D);
}

//===----------------------------------------------------------------------===//
// Parameter Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseParameterDeclaration - Parse a parameter declaration.
///
/// parameter-declaration ::= decl-specifier-seq declarator?
///                         | decl-specifier-seq declarator '=' assignment-expression
///                         | 'this' decl-specifier-seq declarator   [P7.1.1 deducing this]
///
ParmVarDecl *Parser::parseParameterDeclaration(unsigned Index) {
  // P7.1.1: Detect explicit object parameter (deducing this).
  // Syntax: this Type&& name — `this` appears before the type specifier.
  bool IsExplicitObjParam = false;
  if (Index == 0 && Tok.is(TokenKind::kw_this)) {
    // Peek ahead: if the next token looks like a type specifier or declarator,
    // then this is an explicit object parameter.
    // In practice, `this` followed by any type-specifier token means deducing this.
    IsExplicitObjParam = true;
    consumeToken(); // consume 'this'
  }

  // Parse type specifier
  DeclSpec DS;
  parseDeclSpecifierSeq(DS);
  if (!DS.hasTypeSpecifier()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }

  // Parse declarator (name + pointer/reference modifiers)
  Declarator D(DS, DeclaratorContext::ParameterContext);
  parseDeclarator(D);

  QualType Type = D.buildType(Context);

  // Get name from Declarator (optional for unnamed parameters)
  llvm::StringRef Name = D.hasName() ? D.getName().getIdentifier() : llvm::StringRef();
  SourceLocation NameLoc = D.getNameLoc();

  // Explicit object parameter cannot have a default argument (P0847R7).
  if (IsExplicitObjParam && Tok.is(TokenKind::equal)) {
    emitError(DiagID::err_explicit_object_param_default_arg);
    // Still consume to avoid cascading errors.
    consumeToken();
    parseExpression();
  }

  // Parse default argument (only for non-explicit-object parameters)
  Expr *DefaultArg = nullptr;
  if (!IsExplicitObjParam && Tok.is(TokenKind::equal)) {
    consumeToken();
    DefaultArg = parseExpression();
  }

  // Create ParmVarDecl with the correct index
  auto *PVD = llvm::cast<ParmVarDecl>(
      Actions.ActOnParmVarDecl(NameLoc, Name, Type, Index, DefaultArg).get());

  // P7.1.1: Mark as explicit object parameter
  if (IsExplicitObjParam && PVD)
    PVD->setExplicitObjectParam(true);

  return PVD;
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
        NamespaceDecl *NS = llvm::cast<NamespaceDecl>(Actions.ActOnNamespaceDecl(It->second, It->first, IsInline && (It == Names.rend() - 1)).get());
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
  NamespaceDecl *NS = llvm::cast<NamespaceDecl>(Actions.ActOnNamespaceDecl(NameLoc, Name, IsInline).get());

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

    return Actions.ActOnUsingEnumDecl(EnumNameLoc, EnumName, NestedName, HasNested).get();
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

  return Actions.ActOnUsingDecl(NameLoc, Name, NestedName, HasNested).get();
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

  return llvm::cast<UsingDirectiveDecl>(Actions.ActOnUsingDirectiveDecl(NameLoc, Name, NestedName, HasNested).get());
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

  return llvm::cast<NamespaceAliasDecl>(Actions.ActOnNamespaceAliasDecl(AliasLoc, AliasName, TargetName, NestedName).get());
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
    return llvm::cast<ModuleDecl>(Actions.ActOnModuleDecl(ModuleLoc, "", IsExported, "", false, true, false).get());
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
    return llvm::cast<ModuleDecl>(Actions.ActOnModuleDecl(ModuleLoc, "", IsExported, "", false, false, true).get());
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
  ModuleDecl *MD = llvm::cast<ModuleDecl>(Actions.ActOnModuleDecl(ModuleLoc, FullModuleName, IsExported, PartitionName, IsModulePartition, false, false).get());

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
    return llvm::cast<ImportDecl>(Actions.ActOnImportDecl(ImportLoc, "", IsExported, "", HeaderName, true).get());
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

  return llvm::cast<ImportDecl>(Actions.ActOnImportDecl(ImportLoc, ModuleName, IsExported, PartitionName, "", false).get());
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

  return llvm::cast<ExportDecl>(Actions.ActOnExportDecl(ExportLoc, ExportedDecl).get());
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
  EnumDecl *Enum = llvm::cast<EnumDecl>(Actions.ActOnEnumDecl(NameLoc, Name).get());
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
  Enum->setCompleteDefinition();

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

  // Create EnumConstantDecl via Sema
  EnumConstantDecl *Constant = llvm::cast<EnumConstantDecl>(
      Actions.ActOnEnumConstantDeclFactory(NameLoc, Name, QualType(), InitVal).get());
  Enum->addEnumerator(Constant);
}

//===----------------------------------------------------------------------===//
// Typedef Declaration Parsing
//===----------------------------------------------------------------------===//

/// parseTypedefDeclaration - Parse a typedef declaration.
///
/// typedef-declaration ::= 'typedef' type-specifier-seq declarator ';'
TypedefDecl *Parser::parseTypedefDeclaration(SourceLocation TypedefLoc) {
  // Parse type specifier
  DeclSpec DS;
  parseDeclSpecifierSeq(DS);
  if (!DS.hasTypeSpecifier()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }

  // Parse declarator (name + pointer/reference/array modifiers)
  Declarator D(DS, DeclaratorContext::FileContext);
  parseDeclarator(D);

  if (!D.hasName()) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  // Expect semicolon
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }
  consumeToken();

  QualType T = D.buildType(Context);
  return llvm::cast<TypedefDecl>(Actions.ActOnTypedefDecl(D.getNameLoc(), D.getName().getIdentifier(), T).get());
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
  return llvm::cast<TypeAliasDecl>(Actions.ActOnTypeAliasDecl(NameLoc, Name, UnderlyingType).get());
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
  return llvm::cast<StaticAssertDecl>(Actions.ActOnStaticAssertDecl(Loc, CondExpr, Message).get());
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
  LinkageSpecDecl *LinkageSpec = llvm::cast<LinkageSpecDecl>(Actions.ActOnLinkageSpecDecl(Loc, Lang, HasBraces).get());

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
  return llvm::cast<AsmDecl>(Actions.ActOnAsmDecl(Loc, AsmString).get());
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
  return llvm::cast<CXXDeductionGuideDecl>(Actions.ActOnCXXDeductionGuideDecl(NameLoc, TemplateName, ReturnType,
                                             Params).get());
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
  AttributeListDecl *AttrList = llvm::cast<AttributeListDecl>(Actions.ActOnAttributeListDecl(Loc).get());

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

    // P7.3.1: Check for C++26 contract attribute syntax: [[pre: expr]], [[post: expr]], [[assert: expr]]
    // Contract attributes use ':' after the identifier instead of '('.
    if (Namespace.empty() && Tok.is(TokenKind::colon)) {
      ContractKind CK;
      if (AttrName == "pre")         CK = ContractKind::Pre;
      else if (AttrName == "post")   CK = ContractKind::Post;
      else if (AttrName == "assert") CK = ContractKind::Assert;
      else {
        // Not a contract — treat as regular attribute with ':' (error in standard C++)
        // Fall through to normal attribute handling.
        goto normal_attribute;
      }

      consumeToken(); // consume ':'

      // Parse the contract condition expression.
      Expr *CondExpr = parseExpression();
      if (!CondExpr) {
        emitError(DiagID::err_expected);
        return nullptr;
      }

      // Build ContractAttr via Sema.
      auto *CA = llvm::cast_or_null<ContractAttr>(
          Actions.ActOnContractAttr(Loc, static_cast<unsigned>(CK), CondExpr).get());

      // Store ContractAttr directly in the attribute list for CodeGen.
      // Also create an AttributeDecl for backward compatibility with attribute iteration.
      if (CA) {
        AttrList->addContract(CA);

        auto *AD = llvm::cast<AttributeDecl>(
            Actions.ActOnAttributeDecl(Loc,
                getContractKindName(CK).str(), CondExpr).get());
        AttrList->addAttribute(AD);
      }

      // Don't continue parsing — contract attributes don't support comma-separated list.
      // Jump to ']]' parsing.
      goto end_attributes;
    }

normal_attribute:

    // Parse optional attribute argument
    if (Tok.is(TokenKind::l_paren)) {
      consumeToken(); // consume '('

      // Parse argument expression
      if (Tok.is(TokenKind::string_literal)) {
        // Simple case: string literal
        llvm::StringRef StrValue = Tok.getText();
        consumeToken();
        // Create a StringLiteral expression
        ArgExpr = Actions.ActOnStringLiteral(SourceLocation(), StrValue.str()).get();
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
      AttrDecl = llvm::cast<AttributeDecl>(Actions.ActOnAttributeDecl(Loc, AttrName, ArgExpr).get());
    } else {
      AttrDecl = llvm::cast<AttributeDecl>(Actions.ActOnAttributeDeclWithNamespace(Loc, Namespace, AttrName, ArgExpr).get());
    }
    AttrList->addAttribute(AttrDecl);
  } while (Tok.is(TokenKind::comma));

end_attributes:
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
// Constructor and Destructor Parsing (see ParseClass.cpp)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Structured Binding Parsing (C++17/C++26 P0963R3, P1061R10)
//===----------------------------------------------------------------------===//

/// parseStructuredBindingDeclaration - Parse structured binding declaration.
///
/// Syntax: auto [name1, name2, ...] = initializer;
///         auto& [name1, name2, ...] = initializer;
///
/// Returns a DeclStmt containing multiple BindingDecls.
Stmt *Parser::parseStructuredBindingDeclaration(SourceLocation AutoLoc,
                                                 bool IsReference) {
  // Expect '['
  if (!Tok.is(TokenKind::l_square)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }
  consumeToken(); // consume '['
  
  // Parse binding names
  llvm::SmallVector<llvm::StringRef, 4> Names;
  llvm::SmallVector<SourceLocation, 4> NameLocs;
  
  do {
    if (!Tok.is(TokenKind::identifier)) {
      emitError(DiagID::err_expected_identifier);
      return nullptr;
    }
    
    Names.push_back(Tok.getText());
    NameLocs.push_back(Tok.getLocation());
    consumeToken(); // consume identifier
    
  } while (tryConsumeToken(TokenKind::comma));
  
  // Expect ']'
  if (!Tok.is(TokenKind::r_square)) {
    emitError(DiagID::err_expected_rbrace);  // Use existing diagnostic
    return nullptr;
  }
  consumeToken(); // consume ']'
  
  // Expect '='
  if (!Tok.is(TokenKind::equal)) {
    emitError(DiagID::err_expected);  // Generic error
    return nullptr;
  }
  consumeToken(); // consume '='
  
  // Parse initializer expression
  Expr *Init = parseExpression();
  if (!Init) {
    emitError(DiagID::err_expected_expression);
    return nullptr;
  }
  
  // Expect ';'
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }
  consumeToken(); // consume ';'
  
  // Get the type of the initializer
  QualType InitType = Init->getType();
  
  // Call Sema to create binding declarations
  auto Result = Actions.ActOnDecompositionDecl(AutoLoc, Names, InitType, Init);
  
  if (!Result.isUsable()) {
    return nullptr;
  }
  
  // Wrap all bindings in DeclStmt and return
  llvm::SmallVector<Decl *, 4> BindingDecls(Result.getDecls().begin(), 
                                             Result.getDecls().end());
  Stmt *DeclStmt = Actions.ActOnDeclStmtFromDecls(BindingDecls).get();
  return DeclStmt;  // Return as Stmt* (will be used directly in parseDeclarationStatement)
}

//===----------------------------------------------------------------------===//
// buildVarDecl / buildFunctionDecl — AST construction from Declarator
//===----------------------------------------------------------------------===//

/// buildVarDecl - Build a VarDecl from a parsed Declarator.
///
/// Handles: initializer parsing (= expr, (exprs), {list}), semicolon.
VarDecl *Parser::buildVarDecl(Declarator &D) {
  QualType T = D.buildType(Context);
  if (T.isNull())
    return nullptr;

  llvm::StringRef Name = D.getName().getIdentifier();
  SourceLocation NameLoc = D.getNameLoc();
  const DeclSpec &DS = D.getDeclSpec();

  // P7.4.2: Check for placeholder variable `_`
  if (Sema::isPlaceholderIdentifier(Name)) {
    // Parse initializer
    Expr *Init = nullptr;

    if (Tok.is(TokenKind::equal)) {
      consumeToken();
      Init = parseExpression();
    } else if (Tok.is(TokenKind::l_paren)) {
      // Direct initialization: int _(10);
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
      Init = Actions.ActOnCXXConstructExpr(NameLoc, T, Args).get();
    } else if (Tok.is(TokenKind::l_brace)) {
      Init = parseInitializerList(T);
      if (!Init)
        return nullptr;
    }

    // Expect semicolon
    if (!Tok.is(TokenKind::semicolon)) {
      emitError(DiagID::err_expected_semi);
      return nullptr;
    }
    consumeToken();

    // Create placeholder variable - not added to symbol table
    return llvm::cast<VarDecl>(Actions.ActOnPlaceholderVarDecl(NameLoc, T, Init).get());
  }

  // Parse initializer
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
    Init = Actions.ActOnCXXConstructExpr(NameLoc, T, Args).get();
  } else if (Tok.is(TokenKind::l_brace)) {
    Init = parseInitializerList(T);
    if (!Init)
      return nullptr;
  }

  // Expect semicolon
  if (!Tok.is(TokenKind::semicolon)) {
    emitError(DiagID::err_expected_semi);
    return nullptr;
  }
  consumeToken();

  bool IsStatic = (DS.SC == StorageClass::Static);
  return llvm::cast<VarDecl>(Actions.ActOnVarDeclFull(NameLoc, Name, T, Init, IsStatic).get());
}

/// buildFunctionDecl - Build a FunctionDecl from a parsed Declarator.
///
/// Parameters are already parsed in the Function chunk.
/// Handles: function body, = delete, = default, semicolon.
FunctionDecl *Parser::buildFunctionDecl(Declarator &D) {
  llvm::errs() << "DEBUG: buildFunctionDecl - Entry\n";
  QualType T = D.buildType(Context);
  if (T.isNull())
    return nullptr;

  llvm::StringRef Name = D.getName().getIdentifier();
  SourceLocation NameLoc = D.getNameLoc();
  const DeclSpec &DS = D.getDeclSpec();

  // Find the function chunk to get parameters
  llvm::SmallVector<ParmVarDecl *, 8> Params;
  for (const auto &C : D.chunks()) {
    if (C.getKind() == DeclaratorChunk::Function) {
      const auto &FI = C.getFunctionInfo();
      Params.assign(FI.Params.begin(), FI.Params.end());
      break;
    }
  }

  // Parse function body (if not already consumed as part of function chunk)
  Stmt *Body = nullptr;

  llvm::errs() << "DEBUG: buildFunctionDecl - Current token kind: " 
               << static_cast<int>(Tok.getKind()) << ", text: '" << Tok.getText() << "'\n";
  if (Tok.is(TokenKind::l_brace)) {
    llvm::errs() << "DEBUG: buildFunctionDecl - Parsing compound statement\n";
    Body = parseCompoundStatement();
  } else if (Tok.is(TokenKind::equal)) {
    consumeToken();
    if (Tok.is(TokenKind::kw_delete)) {
      consumeToken();
      // P7.4.1: Check for delete("reason") syntax
      if (Tok.is(TokenKind::l_paren)) {
        consumeToken(); // consume '('
        if (Tok.is(TokenKind::string_literal)) {
          StringLiteral *DeleteReason = llvm::cast<StringLiteral>(parsePrimaryExpression());
          // Note: DeleteReason will be set in Sema when FunctionDecl is created
          // For now, we just parse and discard - full implementation needs Sema changes
        }
        expectAndConsume(TokenKind::r_paren, "expected ')' after delete reason");
      }
    } else if (Tok.is(TokenKind::kw_default)) {
      consumeToken();
    }
    expectAndConsume(TokenKind::semicolon, "expected ';' after function definition");
  } else if (Tok.is(TokenKind::semicolon)) {
    consumeToken();
  }

  bool IsInline = DS.IsInline;
  bool IsConstexpr = DS.IsConstexpr;

  // P7.1.1: Extract explicit object parameter if present.
  // For free functions, explicit object parameter is not valid — but we still
  // remove it from Params to avoid cascading errors (diagnostic already emitted).
  if (!Params.empty() && Params.front()->isExplicitObjectParam()) {
    // Explicit object param on a free function — will be diagnosed by SemaCXX.
    // For now just remove it from params and store it.
    Params.erase(Params.begin());
  }

  llvm::errs() << "DEBUG: buildFunctionDecl - Calling ActOnFunctionDeclFull\n";
  auto Result = Actions.ActOnFunctionDeclFull(NameLoc, Name, T, Params, Body,
                                       IsInline, IsConstexpr, false);
  llvm::errs() << "DEBUG: buildFunctionDecl - ActOnFunctionDeclFull returned " 
               << (Result ? "non-null" : "null") << "\n";
  return llvm::cast<FunctionDecl>(Result.get());
}

} // namespace blocktype
