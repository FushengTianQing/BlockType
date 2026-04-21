//===--- ParseTemplate.cpp - Template and Concept Parsing -----------------===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements template declaration, template parameter, template
// argument, concept, and constraint parsing for the BlockType parser.
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
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "parse-template"

namespace blocktype {

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
    TemplateDecl *Template = llvm::cast<TemplateDecl>(
        Actions.ActOnTemplateDeclFactory(TemplateLoc, "", InstantiatedDecl).get());
    return Template;
  }

  consumeToken(); // consume '<'

  // Record the '<' location for TemplateParameterList
  SourceLocation LAngleLoc = Tok.getLocation();

  // Check for explicit specialization: template<> class Vector<int> {}
  if (Tok.is(TokenKind::greater)) {
    SourceLocation RAngleLoc = Tok.getLocation();
    consumeToken(); // consume '>'

    // Parse the specialized declaration, collecting template arguments
    llvm::SmallVector<TemplateArgument, 4> SpecTemplateArgs;
    Decl *SpecializedDecl = parseDeclaration(&SpecTemplateArgs);
    if (!SpecializedDecl) {
      LLVM_DEBUG(llvm::dbgs() << "parseTemplateDeclaration: failed\n");
      return nullptr;
    }

    // Create appropriate specialization declaration based on the specialized type
    TemplateDecl *Template = nullptr;
    
    if (auto *ClassDecl = llvm::dyn_cast<CXXRecordDecl>(SpecializedDecl)) {
      // Class template explicit specialization
      // Try to look up the primary template in the current scope
      ClassTemplateDecl *PrimaryTemplate = nullptr;
      if (NamedDecl *D = Actions.LookupName(ClassDecl->getName())) {
          PrimaryTemplate = llvm::dyn_cast<ClassTemplateDecl>(D);
      }
      
      if (PrimaryTemplate) {
        // Create a proper ClassTemplateSpecializationDecl via Sema
        auto *Spec = llvm::cast<ClassTemplateSpecializationDecl>(
            Actions.ActOnClassTemplateSpecDecl(ClassDecl->getLocation(),
                ClassDecl->getName(), PrimaryTemplate, SpecTemplateArgs, true).get());
        Template = llvm::cast<TemplateDecl>(
            Actions.ActOnTemplateDeclFactory(TemplateLoc, ClassDecl->getName(), Spec).get());
      } else {
        Template = llvm::cast<TemplateDecl>(
            Actions.ActOnTemplateDeclFactory(TemplateLoc, ClassDecl->getName(), SpecializedDecl).get());
      }
    } else if (auto *VD = llvm::dyn_cast<VarDecl>(SpecializedDecl)) {
      // Variable template specialization
      VarTemplateDecl *PrimaryTemplate = nullptr;
      if (NamedDecl *D = Actions.LookupName(VD->getName())) {
          // Could be wrapped in a VarTemplateDecl
          if (auto *VTD = llvm::dyn_cast<VarTemplateDecl>(D))
            PrimaryTemplate = VTD;
          else if (auto *TD = llvm::dyn_cast<TemplateDecl>(D)) {
            // Check if it's a variable template by looking at templated decl
          }
      }

      if (PrimaryTemplate) {
        auto *Spec = llvm::cast<VarTemplateSpecializationDecl>(
            Actions.ActOnVarTemplateSpecDecl(VD->getLocation(), VD->getName(), VD->getType(),
                PrimaryTemplate, llvm::ArrayRef<TemplateArgument>(), VD->getInit(), true).get());
        Template = llvm::cast<TemplateDecl>(
            Actions.ActOnTemplateDeclFactory(TemplateLoc, VD->getName(), Spec).get());
      } else {
        Template = llvm::cast<TemplateDecl>(
            Actions.ActOnTemplateDeclFactory(TemplateLoc, VD->getName(), SpecializedDecl).get());
      }
    } else if (auto *FuncDecl = llvm::dyn_cast<FunctionDecl>(SpecializedDecl)) {
      // Function template specialization
      FunctionTemplateDecl *PrimaryTemplate = nullptr;
      if (NamedDecl *D = Actions.LookupName(FuncDecl->getName())) {
          PrimaryTemplate = llvm::dyn_cast<FunctionTemplateDecl>(D);
      }
      
      if (PrimaryTemplate) {
        Template = llvm::cast<FunctionTemplateDecl>(
            Actions.ActOnFunctionTemplateDeclFactory(TemplateLoc, FuncDecl->getName(), SpecializedDecl).get());
      } else {
        Template = llvm::cast<TemplateDecl>(
            Actions.ActOnTemplateDeclFactory(TemplateLoc, FuncDecl->getName(), SpecializedDecl).get());
      }
    } else {
      // Fallback for other types
      Template = llvm::cast<TemplateDecl>(
          Actions.ActOnTemplateDeclFactory(TemplateLoc, "", SpecializedDecl).get());
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

  SourceLocation RAngleLoc = Tok.getLocation();
  consumeToken(); // consume '>'

  // Enter template parameter scope: template parameters are registered here
  Actions.PushScope(ScopeFlags::TemplateParamScope);
  for (auto *P : Params)
    Actions.RegisterTemplateParam(P);
  
  // Enter template body scope: the templated declaration is parsed in this scope
  Actions.PushScope(ScopeFlags::TemplateScope);

  // Check for concept definition (C++20)
  if (Tok.is(TokenKind::kw_concept)) {
    ConceptDecl *Concept = parseConceptDefinition(TemplateLoc, Params);
    if (!Concept) {
      LLVM_DEBUG(llvm::dbgs() << "parseConceptDefinition: failed\n");
      Actions.PopScope(); // Pop TemplateScope
      Actions.PopScope(); // Pop TemplateParamScope
      return nullptr;
    }
    // Return the concept's template
    Actions.PopScope(); // Pop TemplateScope
    Actions.PopScope(); // Pop TemplateParamScope
    return Concept->getTemplate();
  }

  // Check for requires-clause (C++20)
  Expr *RequiresClause = nullptr;
  if (Tok.is(TokenKind::kw_requires)) {
    RequiresClause = parseRequiresClause();
  }

  // For class templates, pre-register the ClassTemplateDecl before parsing the class body
  // This allows member functions to reference the template by name
  ClassTemplateDecl *PreRegisteredCTD = nullptr;
  if ((Tok.is(TokenKind::kw_class) || Tok.is(TokenKind::kw_struct)) && NextTok.is(TokenKind::identifier)) {
    LLVM_DEBUG(llvm::dbgs() << "Pre-registering class template '" << NextTok.getText().str() << "'\n";
               llvm::dbgs() << "Params size = " << Params.size() << "\n");
    llvm::StringRef ClassName = NextTok.getText();
    SourceLocation ClassLoc = NextTok.getLocation();
    
    // Create a forward-declared CXXRecordDecl
    auto *ForwardDecl = Context.create<CXXRecordDecl>(ClassLoc, ClassName, TagDecl::TK_class);
    ForwardDecl->setCompleteDefinition(false);
    
    // Create a minimal TemplateParameterList (will be replaced later)
    auto *TPL = new TemplateParameterList(TemplateLoc, LAngleLoc, RAngleLoc, Params, nullptr);
    
    // Create and register the ClassTemplateDecl
    auto *CTD = Context.create<ClassTemplateDecl>(TemplateLoc, ClassName, ForwardDecl);
    CTD->setTemplateParameterList(TPL);
    PreRegisteredCTD = CTD;  // Save for later use
    
    auto Result = Actions.ActOnClassTemplateDecl(CTD);
    if (Result.isUsable()) {
      LLVM_DEBUG(llvm::dbgs() << "Successfully registered class template '" << ClassName.str() << "'\n");
    } else {
      LLVM_DEBUG(llvm::dbgs() << "Failed to register class template '" << ClassName.str() << "'\n");
    }
  }

  // Parse the templated declaration
  Decl *TemplatedDecl = parseDeclaration();
  if (!TemplatedDecl) {
    LLVM_DEBUG(llvm::dbgs() << "parseTemplateDeclaration: failed\n");
    Actions.PopScope(); // Pop TemplateScope
    Actions.PopScope(); // Pop TemplateParamScope
    return nullptr;
  }

  // Create appropriate template type based on the templated declaration
  TemplateDecl *Template = nullptr;
  
  if (auto *FuncDecl = llvm::dyn_cast<FunctionDecl>(TemplatedDecl)) {
    // Function template
    llvm::errs() << "DEBUG [ParseTemplate L234]: Creating FunctionTemplateDecl for '" 
                 << FuncDecl->getName().str() << "', TemplatedDecl kind = " 
                 << static_cast<int>(TemplatedDecl->getKind()) << "\n";
    
    auto *FTD = llvm::cast<FunctionTemplateDecl>(
        Actions.ActOnFunctionTemplateDeclFactory(TemplateLoc, FuncDecl->getName(), TemplatedDecl).get());
    
    // Set the template parameter list
    auto *TPL = new TemplateParameterList(TemplateLoc, LAngleLoc, RAngleLoc, Params, RequiresClause);
    FTD->setTemplateParameterList(TPL);
    
    // Register the function template
    auto Result = Actions.ActOnFunctionTemplateDecl(FTD);
    if (Result.isUsable()) {
    LLVM_DEBUG(llvm::dbgs() << "parseTemplateDeclaration: failed\n");
      Template = FTD;
    } else {
      Actions.PopScope(); // Pop TemplateScope
      Actions.PopScope(); // Pop TemplateParamScope
      return nullptr;
    }
  } else if (auto *ClassDecl = llvm::dyn_cast<CXXRecordDecl>(TemplatedDecl)) {
    // Check if this is a partial specialization
    bool IsPartialSpec = false;
    ClassTemplateDecl *PrimaryTemplate = nullptr;
    
    if (NamedDecl *D = Actions.LookupName(ClassDecl->getName())) {
        if (auto *CTD = llvm::dyn_cast<ClassTemplateDecl>(D)) {
          PrimaryTemplate = CTD;
          IsPartialSpec = true;
        }
    }
    
    if (IsPartialSpec && PrimaryTemplate) {
      // Class template partial specialization
      auto *PartialSpec = llvm::cast<ClassTemplatePartialSpecializationDecl>(
          Actions.ActOnClassTemplatePartialSpecDecl(ClassDecl->getLocation(),
              ClassDecl->getName(), PrimaryTemplate,
              llvm::ArrayRef<TemplateArgument>()).get());
      
      auto *PartialTPL = new TemplateParameterList(
          TemplateLoc, LAngleLoc, RAngleLoc, Params, RequiresClause);
      PartialSpec->setTemplateParameterList(PartialTPL);
      
      Template = llvm::cast<ClassTemplateDecl>(
          Actions.ActOnClassTemplateDeclFactory(TemplateLoc, ClassDecl->getName(), PartialSpec).get());
      Actions.PopScope(); // Pop TemplateScope
      Actions.PopScope(); // Pop TemplateParamScope
      return Template;
    }
    
    // Regular class template
    if (PreRegisteredCTD) {
      // Use the pre-registered ClassTemplateDecl
      // Update the TemplatedDecl to point to the fully parsed class with fields
      PreRegisteredCTD->setTemplatedDecl(ClassDecl);
      Template = PreRegisteredCTD;
    } else {
      Template = llvm::cast<ClassTemplateDecl>(
          Actions.ActOnClassTemplateDeclFactory(TemplateLoc, ClassDecl->getName(), TemplatedDecl).get());
    }
  } else if (auto *VD = llvm::dyn_cast<VarDecl>(TemplatedDecl)) {
    // Check for variable template partial specialization
    bool IsPartialSpec = false;
    VarTemplateDecl *PrimaryTemplate = nullptr;
    
    if (NamedDecl *D = Actions.LookupName(VD->getName())) {
        if (auto *VTD = llvm::dyn_cast<VarTemplateDecl>(D)) {
          PrimaryTemplate = VTD;
          IsPartialSpec = true;
        }
    }
    
    if (IsPartialSpec && PrimaryTemplate) {
      auto *PartialSpec = llvm::cast<VarTemplatePartialSpecializationDecl>(
          Actions.ActOnVarTemplatePartialSpecDecl(VD->getLocation(), VD->getName(),
              VD->getType(), PrimaryTemplate,
              llvm::ArrayRef<TemplateArgument>(), VD->getInit()).get());
      
      auto *PartialTPL = new TemplateParameterList(
          TemplateLoc, LAngleLoc, RAngleLoc, Params, RequiresClause);
      PartialSpec->setTemplateParameterList(PartialTPL);
      
      Template = llvm::cast<VarTemplateDecl>(
          Actions.ActOnVarTemplateDeclFactory(TemplateLoc, VD->getName(), PartialSpec).get());
      Actions.PopScope(); // Pop TemplateScope
      Actions.PopScope(); // Pop TemplateParamScope
      return Template;
    }
    
    // Variable template
    Template = llvm::cast<VarTemplateDecl>(
        Actions.ActOnVarTemplateDeclFactory(TemplateLoc, VD->getName(), TemplatedDecl).get());
  } else if (auto *TAD = llvm::dyn_cast<TypeAliasDecl>(TemplatedDecl)) {
    // Type alias template
    Template = llvm::cast<TypeAliasTemplateDecl>(
        Actions.ActOnTypeAliasTemplateDeclFactory(TemplateLoc, TAD->getName(), TemplatedDecl).get());
  } else {
    // Fallback for other types
    Template = llvm::cast<TemplateDecl>(
        Actions.ActOnTemplateDeclFactory(TemplateLoc, "", TemplatedDecl).get());
  }

  // Create TemplateParameterList and assign it to the template
  auto *TPL = new TemplateParameterList(
      TemplateLoc, LAngleLoc, RAngleLoc, Params, RequiresClause);
  Template->setTemplateParameterList(TPL);

  Actions.PopScope(); // Pop TemplateScope
  Actions.PopScope(); // Pop TemplateParamScope
  return Template;
}

//===----------------------------------------------------------------------===//
// Template Parameter Parsing
//===----------------------------------------------------------------------===//

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

  // Check for constrained type parameter (C++20):
  // identifier identifier  -> first is concept name, second is parameter name
  // identifier<args> identifier -> concept<args> is constraint, then parameter name
  // identifier '...' identifier -> concept pack parameter
  if (Tok.is(TokenKind::identifier) && isNextToken(TokenKind::identifier)) {
    // This is a constrained template parameter: ConceptName ParamName
    SourceLocation ConstraintLoc = Tok.getLocation();
    llvm::StringRef ConstraintName = Tok.getText();
    consumeToken(); // consume constraint name (concept name)

    // Parse the parameter name
    llvm::StringRef Name;
    SourceLocation NameLoc;
    bool IsParameterPack = false;

    if (Tok.is(TokenKind::ellipsis)) {
      IsParameterPack = true;
      consumeToken(); // consume '...'
    }

    if (Tok.is(TokenKind::identifier)) {
      Name = Tok.getText();
      NameLoc = Tok.getLocation();
      consumeToken();
    }

    // Create a TemplateTypeParmDecl with the constraint info stored in the name
    // (In a full implementation, we'd store the constraint expression)
    TemplateTypeParmDecl *Param = llvm::cast<TemplateTypeParmDecl>(
        Actions.ActOnTemplateTypeParmDecl(NameLoc, Name, 0, 0, IsParameterPack, false).get());

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

  // Check for constrained template parameter with template args: ConceptName<Args...> ParamName
  if (Tok.is(TokenKind::identifier)) {
    // Tentatively parse to check if this is ConceptName<Args> ParamName
    TentativeParsingAction TPA(*this);
    SourceLocation ConstraintLoc = Tok.getLocation();
    llvm::StringRef ConstraintName = Tok.getText();
    consumeToken(); // consume potential concept name

    if (Tok.is(TokenKind::less)) {
 LLVM_DEBUG(llvm::dbgs() << "parseTemplateParameter: failed\n");
      // Could be a constrained parameter: Concept<Args> ParamName
      // Try to parse the template argument list
      TPA.abort(); // restore state

      // Parse the constraint as a type constraint expression
      Expr *Constraint = parseTypeConstraint();
      if (!Constraint) {
        return nullptr;
      }

      // Now parse the actual parameter
      bool IsParameterPack = false;
      if (Tok.is(TokenKind::ellipsis)) {
        IsParameterPack = true;
        consumeToken();
      }

      llvm::StringRef Name;
      SourceLocation NameLoc;
      if (Tok.is(TokenKind::identifier)) {
        Name = Tok.getText();
        NameLoc = Tok.getLocation();
        consumeToken();
      }

      TemplateTypeParmDecl *Param = llvm::cast<TemplateTypeParmDecl>(
          Actions.ActOnTemplateTypeParmDecl(NameLoc, Name, 0, 0, IsParameterPack, false).get());

      // Parse default argument (optional)
      if (Tok.is(TokenKind::equal)) {
        consumeToken();
        QualType DefaultType = parseType();
        if (!DefaultType.isNull()) {
          Param->setDefaultArgument(DefaultType);
        }
      }

      return Param;
    }

    TPA.abort(); // restore state - not a constrained parameter
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
  TemplateTypeParmDecl *Param = llvm::cast<TemplateTypeParmDecl>(
      Actions.ActOnTemplateTypeParmDecl(NameLoc, Name, 0, 0, IsParameterPack, IsTypename).get());

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
  // Parse type specifier
  DeclSpec DS;
  parseDeclSpecifierSeq(DS);
  if (!DS.hasTypeSpecifier()) {
    emitError(DiagID::err_expected_type);
    return nullptr;
  }

  // Parse declarator (handles pointers, references, arrays, etc.)
  LLVM_DEBUG(llvm::dbgs() << "parseNonTypeTemplateParameter: failed\n");
  Declarator D(DS, DeclaratorContext::TemplateParamContext);
  parseDeclarator(D);

  QualType Type = D.buildType(Context);
  if (Type.isNull()) {
    return nullptr;
  }

  // Check for parameter pack (can appear before or after the name)
  bool IsParameterPack = false;
  if (Tok.is(TokenKind::ellipsis)) {
    IsParameterPack = true;
    consumeToken(); // consume '...'
  }

  // Get name from Declarator (optional for unnamed parameters)
  llvm::StringRef Name = D.hasName() ? D.getName().getIdentifier() : llvm::StringRef();
  SourceLocation NameLoc = D.getNameLoc();

  if (Tok.is(TokenKind::identifier)) {
    // If Declarator didn't capture the name, take it from here
    if (Name.empty()) {
      Name = Tok.getText();
      NameLoc = Tok.getLocation();
    }
    consumeToken();

    // Check for parameter pack after name (alternative syntax)
    if (!IsParameterPack && Tok.is(TokenKind::ellipsis)) {
      IsParameterPack = true;
      consumeToken(); // consume '...'
    }
  }

  // Create NonTypeTemplateParmDecl
  // Use 0 for depth and index (will be set correctly later)
  NonTypeTemplateParmDecl *Param = llvm::cast<NonTypeTemplateParmDecl>(
      Actions.ActOnNonTypeTemplateParmDecl(NameLoc, Name, Type, 0, 0, IsParameterPack).get());

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

  // Record the '<' location
  SourceLocation InnerLAngleLoc = Tok.getLocation();

  // Parse template parameters
  llvm::SmallVector<NamedDecl *, 8> Params;
  parseTemplateParameters(Params);

  // Expect '>'
  if (!Tok.is(TokenKind::greater)) {
    emitError(DiagID::err_expected);
    return nullptr;
  }

  SourceLocation InnerRAngleLoc = Tok.getLocation();
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
  TemplateTemplateParmDecl *Param = llvm::cast<TemplateTemplateParmDecl>(
      Actions.ActOnTemplateTemplateParmDecl(NameLoc, Name, 0, 0, IsParameterPack).get());

  // Set constraint if present
  if (Constraint) {
    Param->setConstraint(Constraint);
  }

  // Create TemplateParameterList for the template template parameter
  auto *TPL = new TemplateParameterList(
      SourceLocation(), InnerLAngleLoc, InnerRAngleLoc, Params);
  Param->setTemplateParameterList(TPL);

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
      if (NamedDecl *Found = Actions.LookupName(TemplateName)) {
          DefaultTemplate = llvm::dyn_cast<TemplateDecl>(Found);
      }
      
      // If not found, create a placeholder TemplateDecl
      if (!DefaultTemplate) {
        DefaultTemplate = llvm::cast<TemplateDecl>(
            Actions.ActOnTemplateDeclFactory(TemplateNameLoc, TemplateName, nullptr).get());
      }
      
      Param->setDefaultArgument(DefaultTemplate);
    } else {
      emitError(DiagID::err_expected_identifier);
    }
  }

  return Param;
}

//===----------------------------------------------------------------------===//
// Template Argument Parsing
//===----------------------------------------------------------------------===//

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
    // Mark this as a pack expansion
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

/// parseTypeConstraint - Parse a type-constraint (C++20).
///
/// type-constraint ::= concept-name
///                   | concept-name '<' template-argument-list? '>'
///
/// This is used in constrained template parameters like:
///   template<std::integral T>        // std::integral is the type constraint
///   template<Sortable<T> Container>  // Sortable<T> is the type constraint
Expr *Parser::parseTypeConstraint() {
  // The concept name should already be parsed as an identifier.
  // We expect the current token to be an identifier (concept name).
  if (!Tok.is(TokenKind::identifier)) {
    emitError(DiagID::err_expected_identifier);
    return nullptr;
  }

  SourceLocation Loc = Tok.getLocation();
  llvm::StringRef ConceptName = Tok.getText();
  consumeToken();

  // Check for template argument list: ConceptName<Args...>
  if (Tok.is(TokenKind::less)) {
    // Parse as a template specialization expression
    return parseTemplateSpecializationExpr(Loc, ConceptName);
  }

  // Simple concept name (no args) - create a DeclRefExpr
  // In a full implementation, this would be resolved to the ConceptDecl
  ValueDecl *ConceptValueDecl = nullptr;
  if (NamedDecl *D = Actions.LookupName(ConceptName)) {
      ConceptValueDecl = dyn_cast<ValueDecl>(D);
  }
  return Actions.ActOnDeclRefExpr(Loc, ConceptValueDecl).get();
}

/// parseConstraintExpression - Parse a constraint-expression.
///
/// constraint-expression ::= logical-or-expression (C++20 [temp.constr])
Expr *Parser::parseConstraintExpression() {
  // Use LogicalOr precedence to allow || and && but reject
  // comma expressions, assignments, and other operators that are
  // not valid in C++20 constraint expressions.
  return parseExpressionWithPrecedence(PrecedenceLevel::LogicalOr);
}

//===----------------------------------------------------------------------===//
// Concept Definition Parsing (C++20)
//===----------------------------------------------------------------------===//

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

  // Create ConceptDecl via Sema (creates TemplateDecl + ConceptDecl internally)
  ConceptDecl *Concept = llvm::cast<ConceptDecl>(
      Actions.ActOnConceptDeclFactory(ConceptNameLoc, ConceptName, Constraint, Loc, TemplateParams).get());
  Actions.ActOnConceptDecl(Concept);

  return Concept;
}

} // namespace blocktype
