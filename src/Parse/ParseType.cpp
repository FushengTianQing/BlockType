//===--- ParseType.cpp - Type Parsing -----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements type parsing for the BlockType parser.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Parse/Parser.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Lex/Token.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {

using namespace blocktype;

//===----------------------------------------------------------------------===//
// Type Parsing
//===----------------------------------------------------------------------===//

/// parseType - Parse a complete type.
///
/// type ::= type-specifier abstract-declarator?
///
QualType Parser::parseType() {
  // Parse type specifier
  QualType Result = parseTypeSpecifier();
  if (Result.isNull())
    return QualType();
  
  // Parse declarator (pointers, references, arrays, functions)
  Result = parseDeclarator(Result);
  
  return Result;
}

/// parseTypeSpecifier - Parse a type specifier.
///
/// type-specifier ::= builtin-type
///                  | named-type
///                  | 'decltype' '(' expression ')'
///                  | 'const' type-specifier
///                  | 'volatile' type-specifier
///
QualType Parser::parseTypeSpecifier() {
  QualType Result;
  Qualifier Quals = Qualifier::None;
  
  // Parse CVR qualifiers
  while (true) {
    if (Tok.is(TokenKind::kw_const)) {
      Quals = Quals | Qualifier::Const;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_volatile)) {
      Quals = Quals | Qualifier::Volatile;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_restrict)) {
      Quals = Quals | Qualifier::Restrict;
      consumeToken();
    } else {
      break;
    }
  }
  
  // Check for decltype type
  if (Tok.is(TokenKind::kw_decltype)) {
    Result = parseDecltypeType();
  } else {
    // Parse the base type
    Result = parseBuiltinType();
    if (Result.isNull()) {
      // Try to parse a named type
      if (Tok.is(TokenKind::identifier)) {
        llvm::errs() << "DEBUG parseTypeSpecifier: Before parseNestedNameSpecifier, token = '" 
                     << Tok.getText().str() << "'\n";
        // Check for nested-name-specifier (A::B::C)
        llvm::StringRef Qualifier = parseNestedNameSpecifier();
        llvm::errs() << "DEBUG parseTypeSpecifier: After parseNestedNameSpecifier, token = '" 
                     << Tok.getText().str() << "', kind = " << static_cast<int>(Tok.getKind()) << "\n";

        // Parse the final type name
        if (!Tok.is(TokenKind::identifier)) {
          emitError(DiagID::err_expected_identifier);
          return QualType();
        }

        llvm::StringRef TypeName = Tok.getText();
        SourceLocation TypeNameLoc = Tok.getLocation();
        consumeToken(); // consume identifier

        // Check for template argument list
        // Use three-layer disambiguation (similar to expression context)
        // to avoid misparsing comparisons as template specializations.
        if (Tok.is(TokenKind::less)) {
          llvm::errs() << "DEBUG parseTypeSpecifier: Found '<' after '" << TypeName.str() << "'\n";
          bool ShouldParseAsTemplate = false;

          // Layer 1: Check if next token is a type keyword (int, float, etc.)
          Token Lookahead = PP.peekToken(0);
          llvm::errs() << "DEBUG parseTypeSpecifier: Layer 1 - Next token kind = " << static_cast<int>(Lookahead.getKind()) << "\n";
          if (isTypeKeyword(Lookahead.getKind())) {
            ShouldParseAsTemplate = true;
            llvm::errs() << "DEBUG parseTypeSpecifier: Layer 1 matched (type keyword)\n";
          }

          // Layer 2: Check if the identifier is a known template in symbol table
          if (!ShouldParseAsTemplate) {
            NamedDecl *LookupResult = Actions.LookupName(TypeName);
            llvm::errs() << "DEBUG parseTypeSpecifier: Layer 2 - LookupName('" << TypeName.str() 
                         << "') returned " << (LookupResult ? LookupResult->getName().str() : "null") << "\n";
            if (LookupResult) {
              llvm::errs() << "DEBUG parseTypeSpecifier: Layer 2 - Decl kind = " 
                           << static_cast<int>(LookupResult->getKind()) << "\n";
              ShouldParseAsTemplate = llvm::isa<TemplateDecl>(LookupResult);
              llvm::errs() << "DEBUG parseTypeSpecifier: Layer 2 - IsTemplateDecl = " << ShouldParseAsTemplate << "\n";
              
              // Also check if it's a ClassTemplateDecl specifically
              if (auto *CTD = llvm::dyn_cast<ClassTemplateDecl>(LookupResult)) {
                llvm::errs() << "DEBUG parseTypeSpecifier: Layer 2 - Found ClassTemplateDecl!\n";
              }
            }
          }

          // Layer 3: Tentative parsing - check if we can match < ... >
          if (!ShouldParseAsTemplate) {
            // Save parser state (member variables Tok and NextTok)
            Token SavedTok = Tok;
            Token SavedNextTok = NextTok;
            size_t SavedBufferIndex = PP.saveTokenBufferState();

            consumeToken(); // consume '<'
            bool FoundMatch = false;

            if (Tok.is(TokenKind::identifier) || isTypeKeyword(Tok.getKind()) ||
                Tok.is(TokenKind::numeric_constant)) {
              int Depth = 1;
              while (Depth > 0 && !Tok.is(TokenKind::eof)) {
                if (Tok.is(TokenKind::less)) {
                  Depth++;
                } else if (Tok.is(TokenKind::greater)) {
                  Depth--;
                  if (Depth == 0) { FoundMatch = true; break; }
                } else if (Tok.is(TokenKind::greatergreater)) {
                  if (Depth >= 2) Depth -= 2;
                  else Depth--;
                  if (Depth == 0) { FoundMatch = true; break; }
                }
                consumeToken();
              }
            }

            // Restore parser state (member variables)
            Tok = SavedTok;
            NextTok = SavedNextTok;
            PP.restoreTokenBufferState(SavedBufferIndex);

            if (FoundMatch) {
              ShouldParseAsTemplate = true;
            }
          }

          if (ShouldParseAsTemplate) {
            llvm::errs() << "DEBUG parseTypeSpecifier: Parsing '" << TypeName.str() << "' as template\n";
            // Parse template arguments
            Result = parseTemplateSpecializationType(TypeName);
          } else {
            llvm::errs() << "DEBUG parseTypeSpecifier: NOT parsing '" << TypeName.str() << "' as template\n";
            // Not a template specialization - create unresolved type from the identifier
            // The '<' will be handled by the caller (e.g., as a comparison operator)
            UnresolvedType *Unresolved = Context.getUnresolvedType(TypeName);
            Result = QualType(Unresolved, Qualifier::None);
          }
        } else {
          // Try to look up the type name in the symbol table
          bool FoundInScope = false;
          if (NamedDecl *D = Actions.LookupName(TypeName)) {
              // Check if this is a type declaration
              if (auto *TD = llvm::dyn_cast<TypeDecl>(D)) {
                Result = Context.getTypeDeclType(TD);
                FoundInScope = true;
              } else {
                // Try casting to TemplateTypeParmDecl directly
                if (auto *TTPD = llvm::dyn_cast<class TemplateTypeParmDecl>(D)) {
                  Result = Context.getTypeDeclType(TTPD);
                  FoundInScope = true;
                }
              }
          }
        
          // If not found in scope, create an unresolved type
          if (!FoundInScope) {
            UnresolvedType *Unresolved = Context.getUnresolvedType(TypeName);
            Result = QualType(Unresolved, Qualifier::None);
          }
        }
        
        // If we have a qualifier, create an elaborated type
        if (!Qualifier.empty() && !Result.isNull()) {
          ElaboratedType *Elaborated = Context.getElaboratedType(Result.getTypePtr(), Qualifier);
          Result = QualType(Elaborated, Qualifier::None);
        }
      } else {
        return QualType();
      }
    }
  }
  
  // Apply qualifiers
  if (Quals != Qualifier::None) {
    Result = QualType(Result.getTypePtr(), Quals);
  }
  
  return Result;
}

/// parseBuiltinType - Parse a builtin type.
///
/// builtin-type ::= 'void'
///               | 'bool'
///               | 'char' ('signed' | 'unsigned')?
///               | 'short' 'int'?
///               | 'int' ('signed' | 'unsigned')?
///               | 'long' 'int'?
///               | 'float'
///               | 'double'
///               | 'auto'
///
QualType Parser::parseBuiltinType() {
  BuiltinKind Kind = BuiltinKind::NumBuiltinTypes;
  SourceLocation Loc = Tok.getLocation();
  
  // DEBUG: Print current token
  llvm::errs() << "DEBUG: parseBuiltinType - Current token kind: " 
               << static_cast<int>(Tok.getKind()) 
               << ", kw_int value: " << static_cast<int>(TokenKind::kw_int)
               << ", text: '" << Tok.getText() << "'\n";
  
  switch (Tok.getKind()) {
  case TokenKind::kw_void:
    Kind = BuiltinKind::Void;
    consumeToken();
    break;
    
  case TokenKind::kw_bool:
    Kind = BuiltinKind::Bool;
    consumeToken();
    break;
    
  case TokenKind::kw_char:
    Kind = BuiltinKind::Char;
    consumeToken();
    // Check for signed/unsigned
    if (Tok.is(TokenKind::kw_signed)) {
      consumeToken();
    } else if (Tok.is(TokenKind::kw_unsigned)) {
      Kind = BuiltinKind::UnsignedChar;
      consumeToken();
    }
    break;
    
  case TokenKind::kw_short:
    consumeToken();
    // Optional 'int'
    if (Tok.is(TokenKind::kw_int))
      consumeToken();
    Kind = BuiltinKind::Short;
    break;
    
  case TokenKind::kw_int:
    llvm::errs() << "DEBUG: parseBuiltinType - Handling kw_int\n";
    Kind = BuiltinKind::Int;
    consumeToken();
    break;
    
  case TokenKind::kw_long:
    consumeToken();
    // Check for 'long long'
    if (Tok.is(TokenKind::kw_long)) {
      consumeToken();
      Kind = BuiltinKind::LongLong;
    } else {
      Kind = BuiltinKind::Long;
    }
    // Optional 'int'
    if (Tok.is(TokenKind::kw_int))
      consumeToken();
    break;
    
  case TokenKind::kw_float:
    Kind = BuiltinKind::Float;
    consumeToken();
    break;
    
  case TokenKind::kw_double:
    Kind = BuiltinKind::Double;
    consumeToken();
    break;
    
  case TokenKind::kw_auto:
    // Create auto type (will be deduced later)
    consumeToken();
    return Context.getAutoType();
    
  case TokenKind::kw_signed:
    consumeToken();
    if (Tok.is(TokenKind::kw_char)) {
      Kind = BuiltinKind::Char; // signed char
      consumeToken();
    } else if (Tok.is(TokenKind::kw_short)) {
      consumeToken();
      if (Tok.is(TokenKind::kw_int))
        consumeToken();
      Kind = BuiltinKind::Short;
    } else if (Tok.is(TokenKind::kw_int)) {
      Kind = BuiltinKind::Int;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_long)) {
      consumeToken();
      if (Tok.is(TokenKind::kw_long)) {
        consumeToken();
        Kind = BuiltinKind::LongLong;
      } else {
        Kind = BuiltinKind::Long;
      }
      if (Tok.is(TokenKind::kw_int))
        consumeToken();
    } else {
      // 'signed' alone means 'signed int'
      Kind = BuiltinKind::Int;
    }
    break;
    
  case TokenKind::kw_unsigned:
    consumeToken();
    if (Tok.is(TokenKind::kw_char)) {
      Kind = BuiltinKind::UnsignedChar;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_short)) {
      consumeToken();
      if (Tok.is(TokenKind::kw_int))
        consumeToken();
      Kind = BuiltinKind::UnsignedShort;
    } else if (Tok.is(TokenKind::kw_int)) {
      Kind = BuiltinKind::UnsignedInt;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_long)) {
      consumeToken();
      if (Tok.is(TokenKind::kw_long)) {
        consumeToken();
        Kind = BuiltinKind::UnsignedLongLong;
      } else {
        Kind = BuiltinKind::UnsignedLong;
      }
      if (Tok.is(TokenKind::kw_int))
        consumeToken();
    } else {
      // 'unsigned' alone means 'unsigned int'
      Kind = BuiltinKind::UnsignedInt;
    }
    break;
    
  default:
    // Not a builtin type
    return QualType();
  }
  
  // Create the builtin type
  BuiltinType *BT = Context.getBuiltinType(Kind);
  return QualType(BT, Qualifier::None);
}

/// parseDeclarator - Parse a declarator.
///
/// declarator ::= ptr-operator* direct-declarator
/// ptr-operator ::= '*' cvr-qualifiers?
///                | '&'
///                | '&&'
///                | nested-name-specifier '*' cvr-qualifiers?  (member pointer)
/// direct-declarator ::= identifier
///                     | '(' declarator ')'
///                     | direct-declarator '[' expr? ']'
///                     | direct-declarator '(' parameter-list? ')' cvr-qualifiers? ref-qualifier?
///
QualType Parser::parseDeclarator(QualType Base) {
  if (Base.isNull())
    return Base;
  
  // Parse pointer operators
  while (true) {
    if (Tok.is(TokenKind::star)) {
      consumeToken();
      
      // Parse CVR qualifiers for pointer
      Qualifier Quals = Qualifier::None;
      while (true) {
        if (Tok.is(TokenKind::kw_const)) {
          Quals = Quals | Qualifier::Const;
          consumeToken();
        } else if (Tok.is(TokenKind::kw_volatile)) {
          Quals = Quals | Qualifier::Volatile;
          consumeToken();
        } else if (Tok.is(TokenKind::kw_restrict)) {
          Quals = Quals | Qualifier::Restrict;
          consumeToken();
        } else {
          break;
        }
      }
      
      // Create pointer type
      PointerType *PT = Context.getPointerType(Base.getTypePtr());
      Base = QualType(PT, Quals);
      
    } else if (Tok.is(TokenKind::amp)) {
      consumeToken();
      
      // Create lvalue reference type
      LValueReferenceType *RT = Context.getLValueReferenceType(Base.getTypePtr());
      Base = QualType(RT, Qualifier::None);
      
    } else if (Tok.is(TokenKind::ampamp)) {
      consumeToken();
      
      // Create rvalue reference type
      RValueReferenceType *RT = Context.getRValueReferenceType(Base.getTypePtr());
      Base = QualType(RT, Qualifier::None);
      
    } else {
      break;
    }
  }
  
  // Parse direct declarator (arrays, functions)
  while (true) {
    if (Tok.is(TokenKind::l_square)) {
      // Array declarator
      Base = parseArrayDimension(Base);
    } else if (Tok.is(TokenKind::l_paren)) {
      // Function declarator
      Base = parseFunctionDeclarator(Base);
    } else {
      break;
    }
  }
  
  return Base;
}

/// parseTemplateSpecializationType - Parse a template specialization type.
///
/// template-specialization-type ::= template-name '<' template-arg-list '>'
/// template-arg-list ::= template-arg (',' template-arg)*
/// template-arg ::= type
///
QualType Parser::parseTemplateSpecializationType(llvm::StringRef TemplateName) {
  llvm::errs() << "DEBUG parseTemplateSpecializationType: TemplateName = '" << TemplateName.str() << "'\n";
  assert(Tok.is(TokenKind::less) && "Expected '<'");
  consumeToken(); // consume '<'
  
  // Create template specialization type
  TemplateSpecializationType *TemplateSpec = Context.getTemplateSpecializationType(TemplateName);
  
  // Parse template arguments
  bool First = true;
  while (!Tok.is(TokenKind::greater) && !Tok.is(TokenKind::eof)) {
    if (!First) {
      if (!Tok.is(TokenKind::comma)) {
        emitError(DiagID::err_expected);
        break;
      }
      consumeToken(); // consume ','
    }
    First = false;
    
    // Parse a type argument
    QualType ArgType = parseType();
    if (!ArgType.isNull()) {
      TemplateSpec->addTemplateArg(ArgType);
    } else {
      // Error recovery: skip to next comma or '>'
      while (!Tok.is(TokenKind::comma) && !Tok.is(TokenKind::greater) && 
             !Tok.is(TokenKind::eof)) {
        consumeToken();
      }
    }
  }
  
  // Expect '>'
  if (!Tok.is(TokenKind::greater)) {
    emitError(DiagID::err_expected);
    return QualType();
  }
  consumeToken(); // consume '>'
  
  // Try to instantiate the class template
  QualType Result = Actions.InstantiateClassTemplate(TemplateName, TemplateSpec);
  if (!Result.isNull()) {
    return Result;
  }
  
  // If instantiation failed, return the TemplateSpecializationType as-is
  return QualType(TemplateSpec, Qualifier::None);
}

/// parseArrayDimension - Parse an array dimension.
///
/// array-dimension ::= '[' expr? ']'
///
QualType Parser::parseArrayDimension(QualType Base) {
  if (Base.isNull())
    return Base;
  
  assert(Tok.is(TokenKind::l_square) && "Expected '['");
  consumeToken();
  
  // Parse size expression (optional)
  Expr *Size = nullptr;
  if (!Tok.is(TokenKind::r_square)) {
    Size = parseExpression();
  }
  
  // Expect ']'
  if (!Tok.is(TokenKind::r_square)) {
    emitError(DiagID::err_expected);
    return Base;
  }
  consumeToken();
  
  // Create array type
  ArrayType *AT = Context.getArrayType(Base.getTypePtr(), Size);
  return QualType(AT, Qualifier::None);
}

/// parseNestedNameSpecifier - Parse a nested-name-specifier.
///
/// nested-name-specifier ::= '::'? identifier '::' (identifier '::')*
///
llvm::StringRef Parser::parseNestedNameSpecifier() {
  llvm::SmallString<64> Qualifier;
  
  // Check for leading '::' (global scope)
  if (Tok.is(TokenKind::coloncolon)) {
    Qualifier = "::";
    consumeToken();
  }
  
  // Parse identifier:: sequences
  while (Tok.is(TokenKind::identifier)) {
    llvm::StringRef Name = Tok.getText();
    
    // Peek ahead to check for '::'
    Token NextTok = peekToken();
    if (!NextTok.is(TokenKind::coloncolon)) {
      // This identifier is the final type name, not part of the qualifier
      break;
    }
    
    // Add to qualifier
    if (!Qualifier.empty() && Qualifier.back() != ':') {
      Qualifier += "::";
    }
    Qualifier += Name;
    
    consumeToken(); // consume identifier
    consumeToken(); // consume '::'
    
    Qualifier += "::";
  }
  
  // Return empty if no qualifier
  if (Qualifier.empty()) {
    return "";
  }
  
  // Allocate string in AST context
  char *Mem = reinterpret_cast<char *>(
      Context.getAllocator().Allocate(Qualifier.size() + 1, alignof(char)));
  std::memcpy(Mem, Qualifier.c_str(), Qualifier.size() + 1);
  return llvm::StringRef(Mem, Qualifier.size());
}

/// parseDecltypeType - Parse a decltype type.
///
/// decltype-type ::= 'decltype' '(' expression ')'
///
QualType Parser::parseDecltypeType() {
  assert(Tok.is(TokenKind::kw_decltype) && "Expected 'decltype'");
  SourceLocation DecltypeLoc = Tok.getLocation();
  consumeToken();
  
  // Expect '('
  if (!Tok.is(TokenKind::l_paren)) {
    emitError(DiagID::err_expected_lparen);
    return QualType();
  }
  consumeToken();
  
  // Parse expression
  Expr *E = parseExpression();
  if (!E) {
    // Error recovery
    skipUntil({TokenKind::r_paren});
    if (Tok.is(TokenKind::r_paren)) {
      consumeToken();
    }
    return QualType();
  }
  
  // Expect ')'
  if (!Tok.is(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
    return QualType();
  }
  consumeToken();
  
  // Create decltype type
  DecltypeType *DT = Context.getDecltypeType(E);
  return QualType(DT, Qualifier::None);
}

/// parseMemberPointerType - Parse a member pointer type.
///
/// member-pointer-type ::= type nested-name-specifier '*' cvr-qualifiers?
///
QualType Parser::parseMemberPointerType(QualType Base) {
  if (Base.isNull())
    return Base;
  
  // We expect a nested-name-specifier followed by '*'
  // This is called when we see something like: int (Class::*) or int Class::*
  
  // Parse the class type (nested-name-specifier)
  llvm::StringRef ClassQualifier = parseNestedNameSpecifier();
  if (ClassQualifier.empty()) {
    emitError(DiagID::err_expected);
    return Base;
  }
  
  // Expect '*'
  if (!Tok.is(TokenKind::star)) {
    emitError(DiagID::err_expected);
    return Base;
  }
  consumeToken();
  
  // Parse CVR qualifiers for member pointer
  Qualifier Quals = Qualifier::None;
  while (true) {
    if (Tok.is(TokenKind::kw_const)) {
      Quals = Quals | Qualifier::Const;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_volatile)) {
      Quals = Quals | Qualifier::Volatile;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_restrict)) {
      Quals = Quals | Qualifier::Restrict;
      consumeToken();
    } else {
      break;
    }
  }
  
  // Create unresolved type for the class
  UnresolvedType *ClassType = Context.getUnresolvedType(ClassQualifier);
  
  // Create member pointer type
  MemberPointerType *MPT = Context.getMemberPointerType(ClassType, Base.getTypePtr());
  return QualType(MPT, Quals);
}

/// parseFunctionDeclarator - Parse a function declarator.
///
/// function-declarator ::= '(' parameter-list? ')' cvr-qualifiers? ref-qualifier?
/// parameter-list ::= parameter (',' parameter)*
/// ref-qualifier ::= '&' | '&&'
///
QualType Parser::parseFunctionDeclarator(QualType ReturnType) {
  if (ReturnType.isNull())
    return ReturnType;
  
  assert(Tok.is(TokenKind::l_paren) && "Expected '('");
  consumeToken();
  
  // Parse parameter types
  llvm::SmallVector<const Type *, 8> ParamTypes;
  bool IsVariadic = false;
  
  while (!Tok.is(TokenKind::r_paren) && !Tok.is(TokenKind::eof)) {
    // Check for variadic '...'
    if (Tok.is(TokenKind::ellipsis)) {
      IsVariadic = true;
      consumeToken();
      break;
    }
    
    // Parse parameter type
    QualType ParamType = parseType();
    if (ParamType.isNull()) {
      // Error recovery
      skipUntil({TokenKind::comma, TokenKind::r_paren});
    } else {
      ParamTypes.push_back(ParamType.getTypePtr());
    }
    
    // Check for comma
    if (Tok.is(TokenKind::comma)) {
      consumeToken();
    } else if (!Tok.is(TokenKind::r_paren)) {
      emitError(DiagID::err_expected);
      break;
    }
  }
  
  // Expect ')'
  if (!Tok.is(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
    return ReturnType;
  }
  consumeToken();
  
  // Parse CVR qualifiers (member function qualifiers)
  Qualifier Quals = Qualifier::None;
  while (true) {
    if (Tok.is(TokenKind::kw_const)) {
      Quals = Quals | Qualifier::Const;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_volatile)) {
      Quals = Quals | Qualifier::Volatile;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_restrict)) {
      Quals = Quals | Qualifier::Restrict;
      consumeToken();
    } else {
      break;
    }
  }
  
  // Parse ref-qualifier (member function ref-qualifier)
  bool IsRefQualified = false;
  bool IsRValueRef = false;
  if (Tok.is(TokenKind::amp)) {
    IsRefQualified = true;
    IsRValueRef = false;
    consumeToken();
  } else if (Tok.is(TokenKind::ampamp)) {
    IsRefQualified = true;
    IsRValueRef = true;
    consumeToken();
  }
  
  // Create function type — const/volatile method qualifiers go into FunctionType
  bool IsConst = hasQualifier(Quals, Qualifier::Const);
  bool IsVolatile = hasQualifier(Quals, Qualifier::Volatile);
  FunctionType *FT = Context.getFunctionType(ReturnType.getTypePtr(), ParamTypes,
                                              IsVariadic, IsConst, IsVolatile);
  // Strip const/volatile from QualType — they're now in FunctionType
  unsigned CVMask = static_cast<unsigned>(Qualifier::Const) |
                    static_cast<unsigned>(Qualifier::Volatile);
  Qualifier RemainingQuals = static_cast<Qualifier>(
      static_cast<unsigned>(Quals) & ~CVMask);
  return QualType(FT, RemainingQuals);
}

/// parseTrailingReturnType - Parse a trailing return type.
///
/// trailing-return-type ::= '->' type
///
QualType Parser::parseTrailingReturnType() {
  // Expect '->'
  if (!Tok.is(TokenKind::arrow)) {
    emitError(DiagID::err_expected);
    return QualType();
  }
  consumeToken();
  
  // Parse the return type
  return parseType();
}

//===----------------------------------------------------------------------===//
// Structured Declarator Parsing (new API)
//===----------------------------------------------------------------------===//

/// parseDeclarator(Declarator &) - Parse a declarator into structured form.
///
/// This is the new API that replaces the old parseDeclarator(QualType).
///
/// declarator ::= ptr-operator* direct-declarator
/// ptr-operator ::= '*' cvr-qualifiers?
///                | '&'
///                | '&&'
///                | nested-name-specifier '*' cvr-qualifiers?  (member pointer)
/// direct-declarator ::= identifier
///                     | '(' declarator ')'
///                     | direct-declarator '[' expr? ']'
///                     | direct-declarator '(' parameter-list? ')' cvr-qualifiers? ref-qualifier?
///
/// Chunk ordering (for correct type building via forward application):
///   `int *ap[10]`   → [Pointer, Array]      → Array(Pointer(int))  ✓
///   `int (*arr)[10]` → [Array, Pointer]      → Pointer(Array(int))  ✓
///   `int (*pf)(int)` → [Function, Pointer]   → Pointer(Function(int)) ✓
///
/// When paren-changed binding occurs, outer chunks (Array/Function) are
/// inserted at the BEGINNING so forward application produces correct types.
void Parser::parseDeclarator(Declarator &D) {
  // 1. Parse leading pointer/reference operators.
  //    These go at the END of the chunk list (closest to name).
  parsePointerOperators(D);

  // 2. Parse the direct-declarator.
  parseDirectDeclarator(D);
}

/// Parse pointer/reference operators, adding chunks to D.
void Parser::parsePointerOperators(Declarator &D) {
  while (true) {
    if (Tok.is(TokenKind::star)) {
      SourceLocation Loc = Tok.getLocation();
      consumeToken();

      Qualifier CVR = Qualifier::None;
      while (true) {
        if (Tok.is(TokenKind::kw_const)) {
          CVR = CVR | Qualifier::Const;
          consumeToken();
        } else if (Tok.is(TokenKind::kw_volatile)) {
          CVR = CVR | Qualifier::Volatile;
          consumeToken();
        } else if (Tok.is(TokenKind::kw_restrict)) {
          CVR = CVR | Qualifier::Restrict;
          consumeToken();
        } else {
          break;
        }
      }

      D.addChunk(DeclaratorChunk::getPointer(CVR, Loc));

    } else if (Tok.is(TokenKind::amp)) {
      SourceLocation Loc = Tok.getLocation();
      consumeToken();
      D.addChunk(DeclaratorChunk::getReference(/*IsRValue=*/false, Loc));

    } else if (Tok.is(TokenKind::ampamp)) {
      SourceLocation Loc = Tok.getLocation();
      consumeToken();
      D.addChunk(DeclaratorChunk::getReference(/*IsRValue=*/true, Loc));

    } else {
      break;
    }
  }
}

/// Parse the direct-declarator part (after pointer operators).
void Parser::parseDirectDeclarator(Declarator &D) {
  // Case 1: '(' declarator ')' — paren-changed binding
  if (Tok.is(TokenKind::l_paren)) {
    // Peek ahead to check if this is a paren-declarator or a function call.
    // If we're in a context that expects a name (not TypeNameContext), '(' means
    // a grouped declarator.
    SourceLocation LParenLoc = Tok.getLocation();
    consumeToken(); // consume '('

    // Parse the inner declarator recursively
    parseDeclarator(D);

    if (!Tok.is(TokenKind::r_paren)) {
      emitError(DiagID::err_expected_rparen);
      return;
    }
    consumeToken(); // consume ')'

    // Now parse OUTER suffixes (arrays, functions) that bind to the
    // paren group. These must go BEFORE inner chunks for correct type building.
    parseArrayAndFunctionSuffixes(D);
    return;
  }

  // Case 2: identifier — the name
  if (Tok.is(TokenKind::identifier)) {
    D.setName(Tok.getText(), Tok.getLocation());
    consumeToken();
  }
  // If no identifier, this is an abstract declarator (no name).
  // That's valid for parameter types, new-type-id, etc.

  // Case 3: Parse trailing suffixes (arrays, functions)
  parseArrayAndFunctionSuffixes(D);
}

/// Parse array and function suffixes, adding chunks to D.
void Parser::parseArrayAndFunctionSuffixes(Declarator &D) {
  while (true) {
    if (Tok.is(TokenKind::l_square)) {
      SourceLocation Loc = Tok.getLocation();
      consumeToken();

      Expr *Size = nullptr;
      if (!Tok.is(TokenKind::r_square)) {
        Size = parseExpression();
      }

      if (!Tok.is(TokenKind::r_square)) {
        emitError(DiagID::err_expected);
        return;
      }
      consumeToken();

      D.addChunk(DeclaratorChunk::getArray(Size, Loc));

    } else if (Tok.is(TokenKind::l_paren)) {
      // Function declarator
      SourceLocation Loc = Tok.getLocation();
      DeclaratorChunk::FunctionInfo FI = parseFunctionDeclaratorInfo();

      D.addChunk(DeclaratorChunk::getFunction(std::move(FI), Loc));

    } else {
      break;
    }
  }
}

/// Parse function parameter list and qualifiers, return FunctionInfo.
DeclaratorChunk::FunctionInfo Parser::parseFunctionDeclaratorInfo() {
  DeclaratorChunk::FunctionInfo FI;

  assert(Tok.is(TokenKind::l_paren) && "Expected '('");
  consumeToken();

  // Parse parameters
  unsigned ParamIndex = 0;
  while (!Tok.is(TokenKind::r_paren) && !Tok.is(TokenKind::eof)) {
    if (Tok.is(TokenKind::ellipsis)) {
      FI.IsVariadic = true;
      consumeToken();
      break;
    }

    // Parse parameter using the old parseParameterDeclaration
    // (will be migrated later)
    ParmVarDecl *Param = parseParameterDeclaration(ParamIndex);
    if (Param) {
      FI.Params.push_back(Param);
      ++ParamIndex;
    }

    if (Tok.is(TokenKind::comma)) {
      consumeToken();
    } else if (!Tok.is(TokenKind::r_paren)) {
      emitError(DiagID::err_expected);
      break;
    }
  }

  // Expect ')'
  if (!Tok.is(TokenKind::r_paren)) {
    emitError(DiagID::err_expected_rparen);
    return FI;
  }
  consumeToken();

  // Parse CVR qualifiers (member function qualifiers)
  while (true) {
    if (Tok.is(TokenKind::kw_const)) {
      FI.MethodQuals = FI.MethodQuals | Qualifier::Const;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_volatile)) {
      FI.MethodQuals = FI.MethodQuals | Qualifier::Volatile;
      consumeToken();
    } else if (Tok.is(TokenKind::kw_restrict)) {
      FI.MethodQuals = FI.MethodQuals | Qualifier::Restrict;
      consumeToken();
    } else {
      break;
    }
  }

  // Parse ref-qualifier
  if (Tok.is(TokenKind::amp)) {
    FI.HasRefQualifier = true;
    FI.IsRValueRef = false;
    consumeToken();
  } else if (Tok.is(TokenKind::ampamp)) {
    FI.HasRefQualifier = true;
    FI.IsRValueRef = true;
    consumeToken();
  }

  return FI;
}

} // namespace blocktype
