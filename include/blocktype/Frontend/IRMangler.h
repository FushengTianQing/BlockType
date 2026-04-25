//===--- IRMangler.h - Backend-Independent Itanium Name Mangling -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the IRMangler class for generating Itanium C++ ABI mangled
// names in a backend-independent manner. Unlike CodeGen::Mangler which depends
// on CodeGenModule, IRMangler only depends on TargetLayout.
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_IRMANGLER_H
#define BLOCKTYPE_FRONTEND_IRMANGLER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "blocktype/AST/Type.h"
#include <string>
#include <optional>

namespace blocktype {

class FunctionDecl;
class CXXMethodDecl;
class CXXConstructorDecl;
class CXXDestructorDecl;
class CXXRecordDecl;
class VarDecl;
class NamedDecl;
class StringLiteral;
class ParmVarDecl;

namespace ir {
class TargetLayout;
}

namespace frontend {

/// DtorVariant — Itanium ABI destructor variants.
enum class DtorVariant {
  Complete,  ///< D1: complete object destructor
  Deleting   ///< D0: deleting destructor
};

/// IRMangler — Backend-independent Itanium C++ ABI name mangler.
///
/// Unlike CodeGen::Mangler (which depends on CodeGenModule), IRMangler
/// only depends on ir::TargetLayout for platform information. All AST
/// information is obtained directly from AST node getters.
///
/// This allows any backend to use the same mangling logic without
/// pulling in CodeGen dependencies.
class IRMangler {
  const ir::TargetLayout& Layout_;

  //===------------------------------------------------------------------===//
  // Substitution compression (Itanium ABI §5.3.5)
  //===------------------------------------------------------------------===//
  llvm::SmallVector<const void*, 16> Substitutions;
  unsigned SubstSeqNo = 0;

  bool shouldAddSubstitution(llvm::StringRef Name) const {
    return Name.size() > 1;
  }
  unsigned addSubstitution(const void* Entity) {
    unsigned Idx = SubstSeqNo++;
    Substitutions.push_back(Entity);
    return Idx;
  }
  std::string getSubstitutionEncoding(unsigned Idx) const {
    if (Idx == 0) return "S_";
    std::string Enc = "S";
    unsigned V = Idx - 1;
    if (V < 36) {
      Enc += (V < 10) ? ('0' + V) : ('A' + V - 10);
    } else {
      llvm::SmallString<8> Buf;
      while (V > 0) {
        unsigned Rem = V % 36;
        Buf.push_back(Rem < 10 ? ('0' + Rem) : ('A' + Rem - 10));
        V /= 36;
      }
      Enc.append(Buf.rbegin(), Buf.rend());
    }
    Enc += '_';
    return Enc;
  }
  std::optional<std::string> trySubstitution(const void* Entity) const {
    for (unsigned I = 0; I < Substitutions.size(); ++I) {
      if (Substitutions[I] == Entity)
        return getSubstitutionEncoding(I);
    }
    return std::nullopt;
  }
  void resetSubstitutions() {
    Substitutions.clear();
    SubstSeqNo = 0;
  }

public:
  explicit IRMangler(const ir::TargetLayout& L);

  //===------------------------------------------------------------------===//
  // Primary entry points
  //===------------------------------------------------------------------===//

  /// Mangle a function name (accepts FunctionDecl/VarDecl/CXXMethodDecl etc.)
  std::string mangleFunctionName(const NamedDecl* ND);

  /// Mangle a VTable name (_ZTV...)
  std::string mangleVTable(const CXXRecordDecl* RD);

  /// Mangle an RTTI typeinfo name (_ZTI...)
  std::string mangleTypeInfo(const CXXRecordDecl* RD);

  /// Mangle a thunk name (_ZThn<offset>_<name> or _ZTv<offset>_<name>)
  std::string mangleThunk(const CXXMethodDecl* MD);

  /// Mangle a guard variable name (_ZGV...)
  std::string mangleGuardVariable(const VarDecl* VD);

  /// Mangle a string literal name (_ZL<length><encoded>)
  std::string mangleStringLiteral(const StringLiteral* SL);

  //===------------------------------------------------------------------===//
  // Auxiliary entry points
  //===------------------------------------------------------------------===//

  /// Mangle a typeinfo name (_ZTS...)
  std::string mangleTypeInfoName(const CXXRecordDecl* RD);

  /// Mangle a destructor variant name
  std::string mangleDtorName(const CXXRecordDecl* RD, DtorVariant Variant);

  /// Mangle a QualType
  std::string mangleType(QualType T);

  /// Mangle a raw Type*
  std::string mangleType(const Type* T);

private:
  //===------------------------------------------------------------------===//
  // Internal encoding methods (ported from CodeGen::Mangler)
  //===------------------------------------------------------------------===//
  void mangleBuiltinType(const BuiltinType* T, std::string& Out);
  void manglePointerType(const PointerType* T, std::string& Out);
  void mangleReferenceType(const ReferenceType* T, std::string& Out);
  void mangleArrayType(const ArrayType* T, std::string& Out);
  void mangleFunctionType(const FunctionType* T, std::string& Out);
  void mangleRecordType(const RecordType* T, std::string& Out);
  void mangleEnumType(const EnumType* T, std::string& Out);
  void mangleTypedefType(const TypedefType* T, std::string& Out);
  void mangleElaboratedType(const ElaboratedType* T, std::string& Out);
  void mangleTemplateSpecializationType(const TemplateSpecializationType* T,
                                         std::string& Out);
  void mangleQualType(QualType QT, std::string& Out);
  void mangleFunctionParamTypes(llvm::ArrayRef<ParmVarDecl*> Params,
                                 std::string& Out);
  void mangleNestedName(const CXXRecordDecl* RD, std::string& Out);
  static bool hasNamespaceParent(const CXXRecordDecl* RD);
  static void mangleSourceName(llvm::StringRef Name, std::string& Out);
  void mangleCtorName(const CXXConstructorDecl* Ctor, std::string& Out);
  void mangleDtorNameInternal(const CXXDestructorDecl* Dtor, std::string& Out,
                       DtorVariant Variant = DtorVariant::Complete);
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_IRMANGLER_H
