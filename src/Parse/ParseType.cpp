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
  
  // Parse the base type
  Result = parseBuiltinType();
  if (Result.isNull()) {
    // Try to parse a named type
    if (Tok.is(TokenKind::identifier)) {
      // Check for nested-name-specifier (A::B::C)
      llvm::StringRef Qualifier = parseNestedNameSpecifier();
      
      // Parse the final type name
      if (!Tok.is(TokenKind::identifier)) {
        emitError(DiagID::err_expected_identifier);
        return QualType();
      }
      
      llvm::StringRef TypeName = Tok.getText();
      SourceLocation TypeNameLoc = Tok.getLocation();
      consumeToken();
      
      // Check for template argument list
      if (Tok.is(TokenKind::less)) {
        // Parse template arguments
        Result = parseTemplateSpecializationType(TypeName);
      } else {
        // Try to look up the type name in the symbol table
        bool FoundInScope = false;
        if (CurrentScope) {
          if (NamedDecl *D = CurrentScope->lookup(TypeName)) {
            // Check if this is a type declaration
            if (auto *TD = llvm::dyn_cast<TypeDecl>(D)) {
              Result = Context.getTypeDeclType(TD);
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
    {
      AutoType *AutoTy = Context.getAutoType();
      return QualType(AutoTy, Qualifier::None);
    }
    
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
  
  return Base;
}

/// parseTemplateSpecializationType - Parse a template specialization type.
///
/// template-specialization-type ::= template-name '<' template-arg-list '>'
/// template-arg-list ::= template-arg (',' template-arg)*
/// template-arg ::= type
///
QualType Parser::parseTemplateSpecializationType(llvm::StringRef TemplateName) {
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

} // namespace blocktype
