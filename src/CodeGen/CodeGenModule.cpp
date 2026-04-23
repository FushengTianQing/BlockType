//===--- CodeGenModule.cpp - Module-level CodeGen -------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenTypes.h"
#include "blocktype/CodeGen/CodeGenConstant.h"
#include "blocktype/CodeGen/CodeGenFunction.h"
#include "blocktype/CodeGen/CGCXX.h"
#include "blocktype/CodeGen/CGDebugInfo.h"
#include "blocktype/CodeGen/Mangler.h"
#include "blocktype/CodeGen/TargetInfo.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Expr.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/Compiler.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// 构造 / 析构
//===----------------------------------------------------------------------===//

CodeGenModule::CodeGenModule(ASTContext &Ctx, llvm::LLVMContext &LLVMCtx,
                             SourceManager &SMRef,  // P0 修复：SourceManager
                             llvm::StringRef ModuleName,
                             llvm::StringRef TargetTriple)
    : Context(Ctx), LLVMCtx(LLVMCtx), SM(SMRef),
      TheModule(std::make_unique<llvm::Module>(ModuleName, LLVMCtx)) {
  // 设置目标三元组和数据布局
  TheModule->setTargetTriple(TargetTriple);

  // 初始化子组件（顺序重要：TargetInfo 先于 Types）
  Target = std::make_unique<class TargetInfo>(TargetTriple);
  TheModule->setDataLayout(Target->getDataLayout());
  Types = std::make_unique<CodeGenTypes>(*this);
  Constants = std::make_unique<CodeGenConstant>(*this);
  CXX = std::make_unique<CGCXX>(*this);
  DebugInfo = std::make_unique<CGDebugInfo>(*this);
  Mangle = std::make_unique<Mangler>(*this);
}

CodeGenModule::~CodeGenModule() = default;

const llvm::DataLayout &CodeGenModule::getDataLayout() const {
  return TheModule->getDataLayout();
}

//===----------------------------------------------------------------------===//
// 代码生成入口
//===----------------------------------------------------------------------===//

void CodeGenModule::EmitTranslationUnit(TranslationUnitDecl *TU) {
  if (!TU) return;

  // 初始化调试信息
  // 使用 "input.cpp" 作为默认文件名（真实编译器从 SourceManager 获取）
  DebugInfo->Initialize("input.cpp", ".");

  // 第一遍：生成所有声明（前向引用）
  for (Decl *D : TU->decls()) {
    if (auto *FD = llvm::dyn_cast<FunctionDecl>(D)) {
      // 创建函数声明（不生成函数体）
      GetOrCreateFunctionDecl(FD);
      
      // P7.1.5: Process lambda closure classes in function scope
      for (Decl *SubDecl : FD->decls()) {
        if (auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(SubDecl)) {
          if (CXXRD->isLambda()) {
            // Create closure type and layout
            getTypes().GetRecordType(CXXRD);
            EmitClassLayout(CXXRD);
            
            // Create declarations for operator()
            for (CXXMethodDecl *Method : CXXRD->methods()) {
              if (Method->getBody()) {
                GetOrCreateFunctionDecl(Method);
              }
            }
          }
        }
      }
    } else if (auto *VD = llvm::dyn_cast<VarDecl>(D)) {
      // 全局变量加入延迟队列
      DeferredGlobalVars.push_back(VD);
    } else if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(D)) {
      // 前向声明 struct 类型 + 计算类布局
      getTypes().GetRecordType(RD);
      EmitClassLayout(RD);
      
      // P2: 为类内定义的方法创建声明
      for (CXXMethodDecl *Method : RD->methods()) {
        if (Method->getBody()) {
          GetOrCreateFunctionDecl(Method);
        }
      }
    } else if (auto *RD = llvm::dyn_cast<RecordDecl>(D)) {
      getTypes().GetRecordType(RD);
    } else if (auto *ED = llvm::dyn_cast<EnumDecl>(D)) {
      // 枚举类型 — ConvertType 会处理
      (void)ED;
    }
  }

  // 发射延迟定义（全局变量定义）
  EmitDeferred();

  // 发射所有需要的虚函数表
  EmitVTables();

  // 第二遍：生成函数体
  for (Decl *D : TU->decls()) {
    if (auto *FD = llvm::dyn_cast<FunctionDecl>(D)) {
      if (FD->getBody()) {
        EmitFunction(FD);
      }
      
      // P7.1.5: Generate lambda closure class methods
      for (Decl *SubDecl : FD->decls()) {
        if (auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(SubDecl)) {
          if (CXXRD->isLambda()) {
            // Generate operator() for lambda
            for (CXXMethodDecl *MD : CXXRD->methods()) {
              if (MD->getBody() && MD->getName() == "operator()") {
                EmitFunction(MD);
              }
            }
          }
        }
      }
    } else if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(D)) {
      // P2: 生成类内定义的方法体
      for (CXXMethodDecl *MD : RD->methods()) {
        if (MD->getBody()) {
          EmitFunction(MD);
        }
      }
    }
  }

  // 发射全局构造/析构
  EmitGlobalCtorDtors();

  // 完成调试信息
  DebugInfo->Finalize();
}

void CodeGenModule::EmitDeferred() {
  // 发射全局变量定义
  for (VarDecl *VD : DeferredGlobalVars) {
    EmitGlobalVar(VD);
  }
  DeferredGlobalVars.clear();

  // 发射需要动态初始化的全局变量
  for (VarDecl *VD : DynamicInitVars) {
    EmitDynamicGlobalInit(VD);
  }
  DynamicInitVars.clear();
}

//===----------------------------------------------------------------------===//
// 属性处理
//===----------------------------------------------------------------------===//

AttributeQuery CodeGenModule::QueryAttributes(const Decl *Decl) {
  AttributeQuery Result;
  
  if (!Decl) {
    return Result;
  }
  
  // 1. 检查 FunctionDecl 上的直接属性
  if (auto *FD = llvm::dyn_cast<FunctionDecl>(Decl)) {
    if (auto *AttrList = FD->getAttrs()) {
      for (auto *Attr : AttrList->getAttributes()) {
        auto Name = Attr->getAttributeName();
        
        // 链接相关属性
        if (Name == "weak") {
          Result.IsWeak = true;
        } else if (Name == "used") {
          Result.IsUsed = true;
        } else if (Name == "dllimport") {
          Result.IsDLLImport = true;
        } else if (Name == "dllexport") {
          Result.IsDLLExport = true;
        }
        // 可见性属性
        else if (Name == "visibility") {
          Result.IsHiddenVisibility = true;
          Result.IsDefaultVisibility = false;
          
          // 解析 visibility 参数
          if (auto *Arg = Attr->getArgumentExpr()) {
            if (auto *SL = llvm::dyn_cast<StringLiteral>(Arg)) {
              llvm::StringRef VisValue = SL->getValue();
              Result.VisibilityValue = VisValue;
              
              // 根据值设置标志
              if (VisValue == "hidden") {
                Result.IsHiddenVisibility = true;
                Result.IsDefaultVisibility = false;
              } else if (VisValue == "protected") {
                Result.IsHiddenVisibility = false;
                Result.IsDefaultVisibility = false;
              } else if (VisValue == "default") {
                Result.IsHiddenVisibility = false;
                Result.IsDefaultVisibility = true;
              }
            }
          }
        }
        // 优化相关属性
        else if (Name == "deprecated") {
          Result.IsDeprecated = true;
        } else if (Name == "noreturn") {
          Result.IsNoreturn = true;
        } else if (Name == "noinline" || Name == "noinline_") {
          Result.IsNoInline = true;
        } else if (Name == "always_inline" || Name == "forceinline") {
          Result.IsAlwaysInline = true;
        } else if (Name == "const") {
          Result.IsConst = true;
        } else if (Name == "pure") {
          Result.IsPure = true;
        }
      }
    }
  }
  // 2. 检查 VarDecl 上的属性（新增）
  else if (auto *VD = llvm::dyn_cast<VarDecl>(Decl)) {
    if (auto *AttrList = VD->getAttrs()) {
      for (auto *Attr : AttrList->getAttributes()) {
        auto Name = Attr->getAttributeName();
        
        if (Name == "weak") {
          Result.IsWeak = true;
        } else if (Name == "used") {
          Result.IsUsed = true;
        } else if (Name == "dllimport") {
          Result.IsDLLImport = true;
        } else if (Name == "dllexport") {
          Result.IsDLLExport = true;
        } else if (Name == "deprecated") {
          Result.IsDeprecated = true;
        } else if (Name == "visibility") {
          Result.IsHiddenVisibility = true;
          Result.IsDefaultVisibility = false;
          
          // 解析 visibility 参数
          if (auto *Arg = Attr->getArgumentExpr()) {
            if (auto *SL = llvm::dyn_cast<StringLiteral>(Arg)) {
              llvm::StringRef VisValue = SL->getValue();
              Result.VisibilityValue = VisValue;
              
              if (VisValue == "hidden") {
                Result.IsHiddenVisibility = true;
                Result.IsDefaultVisibility = false;
              } else if (VisValue == "protected") {
                Result.IsHiddenVisibility = false;
                Result.IsDefaultVisibility = false;
              } else if (VisValue == "default") {
                Result.IsHiddenVisibility = false;
                Result.IsDefaultVisibility = true;
              }
            }
          }
        }
      }
    }
  }
  // 3. 检查 FieldDecl 上的属性（新增）
  else if (auto *FD = llvm::dyn_cast<FieldDecl>(Decl)) {
    if (auto *AttrList = FD->getAttrs()) {
      for (auto *Attr : AttrList->getAttributes()) {
        auto Name = Attr->getAttributeName();
        
        if (Name == "deprecated") {
          Result.IsDeprecated = true;
        } else if (Name == "visibility") {
          Result.IsHiddenVisibility = true;
          Result.IsDefaultVisibility = false;
          
          // 解析 visibility 参数
          if (auto *Arg = Attr->getArgumentExpr()) {
            if (auto *SL = llvm::dyn_cast<StringLiteral>(Arg)) {
              llvm::StringRef VisValue = SL->getValue();
              Result.VisibilityValue = VisValue;
              
              if (VisValue == "hidden") {
                Result.IsHiddenVisibility = true;
                Result.IsDefaultVisibility = false;
              } else if (VisValue == "protected") {
                Result.IsHiddenVisibility = false;
                Result.IsDefaultVisibility = false;
              } else if (VisValue == "default") {
                Result.IsHiddenVisibility = false;
                Result.IsDefaultVisibility = true;
              }
            }
          }
        }
      }
    }
  }
  
  // 4. 检查 CXXRecordDecl 的成员属性（类级别的属性）
  if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(Decl)) {
    for (auto *Member : RD->members()) {
      if (auto *AttrList = llvm::dyn_cast<AttributeListDecl>(Member)) {
        for (auto *Attr : AttrList->getAttributes()) {
          auto Name = Attr->getAttributeName();
          
          if (Name == "weak") {
            Result.IsWeak = true;
          } else if (Name == "used") {
            Result.IsUsed = true;
          } else if (Name == "dllimport") {
            Result.IsDLLImport = true;
          } else if (Name == "dllexport") {
            Result.IsDLLExport = true;
          } else if (Name == "deprecated") {
            Result.IsDeprecated = true;
          } else if (Name == "visibility") {
            Result.IsHiddenVisibility = true;
            Result.IsDefaultVisibility = false;
            
            // 解析 visibility 参数
            if (auto *Arg = Attr->getArgumentExpr()) {
              if (auto *SL = llvm::dyn_cast<StringLiteral>(Arg)) {
                llvm::StringRef VisValue = SL->getValue();
                Result.VisibilityValue = VisValue;
                
                if (VisValue == "hidden") {
                  Result.IsHiddenVisibility = true;
                  Result.IsDefaultVisibility = false;
                } else if (VisValue == "protected") {
                  Result.IsHiddenVisibility = false;
                  Result.IsDefaultVisibility = false;
                } else if (VisValue == "default") {
                  Result.IsHiddenVisibility = false;
                  Result.IsDefaultVisibility = true;
                }
              }
            }
          }
        }
      }
    }
  }
  
  return Result;
}

bool CodeGenModule::HasAttribute(const Decl *Decl, llvm::StringRef AttrName) {
  if (!Decl) {
    return false;
  }
  
  // 检查 FunctionDecl 上的属性
  if (auto *FD = llvm::dyn_cast<FunctionDecl>(Decl)) {
    if (auto *AttrList = FD->getAttrs()) {
      for (auto *Attr : AttrList->getAttributes()) {
        if (Attr->getAttributeName() == AttrName) {
          return true;
        }
      }
    }
  }
  // 检查 VarDecl 上的属性（新增）
  else if (auto *VD = llvm::dyn_cast<VarDecl>(Decl)) {
    if (auto *AttrList = VD->getAttrs()) {
      for (auto *Attr : AttrList->getAttributes()) {
        if (Attr->getAttributeName() == AttrName) {
          return true;
        }
      }
    }
  }
  // 检查 FieldDecl 上的属性（新增）
  else if (auto *FD = llvm::dyn_cast<FieldDecl>(Decl)) {
    if (auto *AttrList = FD->getAttrs()) {
      for (auto *Attr : AttrList->getAttributes()) {
        if (Attr->getAttributeName() == AttrName) {
          return true;
        }
      }
    }
  }
  
  // 检查 CXXRecordDecl 的成员属性
  if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(Decl)) {
    for (auto *Member : RD->members()) {
      if (auto *AttrList = llvm::dyn_cast<AttributeListDecl>(Member)) {
        for (auto *Attr : AttrList->getAttributes()) {
          if (Attr->getAttributeName() == AttrName) {
            return true;
          }
        }
      }
    }
  }
  
  return false;
}

Expr *CodeGenModule::GetAttributeArgument(const Decl *Decl, llvm::StringRef AttrName) {
  if (!Decl) {
    return nullptr;
  }
  
  // 检查 FunctionDecl 上的属性
  if (auto *FD = llvm::dyn_cast<FunctionDecl>(Decl)) {
    if (auto *AttrList = FD->getAttrs()) {
      for (auto *Attr : AttrList->getAttributes()) {
        if (Attr->getAttributeName() == AttrName) {
          return Attr->getArgumentExpr();
        }
      }
    }
  }
  // 检查 VarDecl 上的属性（新增）
  else if (auto *VD = llvm::dyn_cast<VarDecl>(Decl)) {
    if (auto *AttrList = VD->getAttrs()) {
      for (auto *Attr : AttrList->getAttributes()) {
        if (Attr->getAttributeName() == AttrName) {
          return Attr->getArgumentExpr();
        }
      }
    }
  }
  // 检查 FieldDecl 上的属性（新增）
  else if (auto *FD = llvm::dyn_cast<FieldDecl>(Decl)) {
    if (auto *AttrList = FD->getAttrs()) {
      for (auto *Attr : AttrList->getAttributes()) {
        if (Attr->getAttributeName() == AttrName) {
          return Attr->getArgumentExpr();
        }
      }
    }
  }
  
  // 检查 CXXRecordDecl 的成员属性
  if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(Decl)) {
    for (auto *Member : RD->members()) {
      if (auto *AttrList = llvm::dyn_cast<AttributeListDecl>(Member)) {
        for (auto *Attr : AttrList->getAttributes()) {
          if (Attr->getAttributeName() == AttrName) {
            return Attr->getArgumentExpr();
          }
        }
      }
    }
  }
  
  return nullptr;
}

GlobalDeclAttributes CodeGenModule::GetGlobalDeclAttributes(const Decl *D) {
  GlobalDeclAttributes Attrs;

  if (!D) return Attrs;

  // 检查 Decl 是否具有 attribute 列表
  // 搜索 Decl 所属的 CXXRecordDecl 或 TranslationUnitDecl 中的属性
  // 当前 BlockType AST 中属性通过 AttributeDecl 节点表示，
  // 它们作为 DeclContext 的成员出现。我们检查父上下文中的属性。

  // 遍历 CXXRecordDecl 的成员寻找 AttributeDecl
  if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(D)) {
    for (Decl *Member : RD->members()) {
      if (auto *AttrList = llvm::dyn_cast<AttributeListDecl>(Member)) {
        for (auto *Attr : AttrList->getAttributes()) {
          auto Name = Attr->getAttributeName();
          if (Name == "weak") {
            Attrs.IsWeak = true;
          } else if (Name == "dllimport") {
            Attrs.IsDLLImport = true;
          } else if (Name == "dllexport") {
            Attrs.IsDLLExport = true;
          } else if (Name == "used") {
            Attrs.IsUsed = true;
          } else if (Name == "deprecated") {
            Attrs.IsDeprecated = true;
          } else if (Name == "visibility") {
            // visibility 属性带参数，通过 ArgumentExpr 解析
            // 简化处理：如果存在 visibility 参数表达式，检查其值
            // 当前 AST 中参数是 Expr*，不方便直接解析字符串
            // 未来扩展时通过 Sema 传递解析后的 visibility 值
          }
        }
      }
    }
  }

  // 注意：GetGlobalDeclAttributes 是旧API，建议使用新的 QueryAttributes()
  // QueryAttributes() 支持 FunctionDecl, VarDecl, FieldDecl, CXXRecordDecl

  return Attrs;
}

void CodeGenModule::ApplyGlobalValueAttributes(llvm::GlobalValue *GV,
                                                const GlobalDeclAttributes &Attrs) {
  if (!GV) return;

  // Weak 属性
  if (Attrs.IsWeak) {
    GV->setLinkage(llvm::GlobalValue::WeakAnyLinkage);
  }

  // DLL 属性
  if (Attrs.IsDLLExport) {
    GV->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
  }
  if (Attrs.IsDLLImport) {
    GV->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
  }

  // Used 属性（即使未被引用也保留）— 通过 metadata 实现
  if (Attrs.IsUsed) {
    // llvm.used 是一个全局数组，包含必须保留的符号
    // 简化：通过设置 linkage 为 External 确保不被优化掉
    // TODO: 完整实现需要维护 llvm.used 全局变量
  }

  // Deprecated 属性 — 通过 metadata 标记
  if (Attrs.IsDeprecated) {
    // 简化：不阻止优化，仅标记
  }

  // Visibility
  GV->setVisibility(GetVisibility(Attrs));
}

//===----------------------------------------------------------------------===//
// Linkage / Visibility
//===----------------------------------------------------------------------===//

llvm::GlobalValue::LinkageTypes
CodeGenModule::GetFunctionLinkage(const FunctionDecl *FD) {
  if (!FD) return llvm::Function::ExternalLinkage;

  // 构造/析构函数：ExternalLinkage
  if (llvm::isa<CXXConstructorDecl>(FD) || llvm::isa<CXXDestructorDecl>(FD)) {
    return llvm::Function::ExternalLinkage;
  }

  // 静态成员函数 — ExternalLinkage（类成员没有 TU 级别的 static）
  // 但普通 static 自由函数使用 InternalLinkage
  if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(FD)) {
    if (MD->isStatic()) {
      // 静态成员函数可以有 InternalLinkage（如果需要）
      return llvm::Function::ExternalLinkage;
    }
    return llvm::Function::ExternalLinkage;
  }

  // inline 函数：LinkOnceODR（允许跨 TU 合并，保持 ODR 语义）
  if (FD->isInline()) {
    return llvm::Function::LinkOnceODRLinkage;
  }

  // Weak 函数（通过属性检查）
  auto Attrs = GetGlobalDeclAttributes(FD);
  if (Attrs.IsWeak) {
    return llvm::Function::WeakAnyLinkage;
  }

  // 默认：ExternalLinkage
  // static 函数使用 InternalLinkage
  if (FD->isStatic()) {
    return llvm::Function::InternalLinkage;
  }
  return llvm::Function::ExternalLinkage;
}

llvm::GlobalValue::LinkageTypes
CodeGenModule::GetVariableLinkage(const VarDecl *VD) {
  if (!VD) return llvm::GlobalValue::ExternalLinkage;

  // static 局部/全局变量使用 InternalLinkage
  if (VD->isStatic()) {
    return llvm::GlobalValue::InternalLinkage;
  }

  // constexpr 变量：LinkOnceODR（如果全局可见的话）
  if (VD->isConstexpr()) {
    // constexpr 全局变量可以作为常量合并
    return llvm::GlobalValue::LinkOnceODRLinkage;
  }

  // Weak 变量
  auto Attrs = GetGlobalDeclAttributes(VD);
  if (Attrs.IsWeak) {
    return llvm::GlobalValue::WeakAnyLinkage;
  }

  return llvm::GlobalValue::ExternalLinkage;
}

llvm::GlobalValue::VisibilityTypes
CodeGenModule::GetVisibility(const GlobalDeclAttributes &Attrs) {
  if (Attrs.IsHiddenVisibility) {
    return llvm::GlobalValue::HiddenVisibility;
  }
  return llvm::GlobalValue::DefaultVisibility;
}

// 新增：基于 AttributeQuery 的 visibility 计算
llvm::GlobalValue::VisibilityTypes
CodeGenModule::GetVisibilityFromQuery(const AttributeQuery &Query) {
  llvm::StringRef VisValue = Query.getVisibilityString();
  
  if (VisValue == "hidden") {
    return llvm::GlobalValue::HiddenVisibility;
  } else if (VisValue == "protected") {
    // LLVM 不直接支持 protected，降级为 default
    // 在 ELF 平台上可以通过其他方式实现
    return llvm::GlobalValue::DefaultVisibility;
  } else if (VisValue == "default") {
    return llvm::GlobalValue::DefaultVisibility;
  }
  
  // 默认行为
  return Query.IsHiddenVisibility ? 
         llvm::GlobalValue::HiddenVisibility : 
         llvm::GlobalValue::DefaultVisibility;
}

//===----------------------------------------------------------------------===//
// 初始化分类
//===----------------------------------------------------------------------===//

InitKind CodeGenModule::ClassifyGlobalInit(const VarDecl *VD) {
  if (!VD) return InitKind::ZeroInitialization;

  // constexpr 变量总是常量初始化
  if (VD->isConstexpr()) {
    return InitKind::ConstantInitialization;
  }

  Expr *Init = VD->getInit();
  if (!Init) {
    // 无初始化器 → 零初始化
    return InitKind::ZeroInitialization;
  }

  // 尝试用 CodeGenConstant 求值
  // 如果能成功生成 llvm::Constant，则是常量初始化
  llvm::Constant *ConstVal = getConstants().EmitConstantForType(Init, VD->getType());
  if (ConstVal) {
    return InitKind::ConstantInitialization;
  }

  // 其他情况 → 动态初始化（需要运行时求值）
  return InitKind::DynamicInitialization;
}

void CodeGenModule::EmitDynamicGlobalInit(VarDecl *VD) {
  if (!VD) return;

  // 获取全局变量（应已由 EmitGlobalVar 创建，初始值为零）
  llvm::GlobalVariable *GV = GetGlobalVar(VD);
  if (!GV) return;

  Expr *Init = VD->getInit();
  if (!Init) return;

  // 创建动态初始化函数
  std::string InitFuncName = "__cxx_global_var_init.";
  InitFuncName += VD->getName().str();

  llvm::FunctionType *InitFTy = llvm::FunctionType::get(
      llvm::Type::getVoidTy(LLVMCtx), false);
  llvm::Function *InitFn = llvm::Function::Create(
      InitFTy, llvm::Function::InternalLinkage, InitFuncName, TheModule.get());

  llvm::BasicBlock *Entry = llvm::BasicBlock::Create(LLVMCtx, "entry", InitFn);
  CodeGenFunction CGF(*this);
  CGF.getBuilder().SetInsertPoint(Entry);
  CGF.setCurrentFunction(InitFn);

  // 计算初始值并存储
  llvm::Value *InitVal = CGF.EmitExpr(Init);
  if (InitVal) {
    CGF.getBuilder().CreateStore(InitVal, GV);
  }

  CGF.getBuilder().CreateRetVoid();

  // 注册为全局构造函数（默认优先级）
  AddGlobalCtor(nullptr, 65535);  // 使用 nullptr 占位，实际用 InitFn

  // 直接添加初始化函数到 llvm.global_ctors
  // （AddGlobalCtor 需要 FunctionDecl，这里直接添加 llvm::Function）
  // 简化：将 InitFn 作为全局构造函数
  // 由于 AddGlobalCtor 需要 FunctionDecl，这里绕过它直接处理
  GlobalCtorsDirect.push_back(InitFn);
}

//===----------------------------------------------------------------------===//
// 全局变量生成
//===----------------------------------------------------------------------===//

llvm::GlobalVariable *CodeGenModule::EmitGlobalVar(VarDecl *VD) {
  if (!VD) return nullptr;

  // 检查是否已生成
  if (auto *Existing = GetGlobalVar(VD))
    return Existing;

  llvm::Type *Ty = getTypes().ConvertType(VD->getType());
  if (!Ty) return nullptr;

  // 分类初始化类型
  InitKind Init = ClassifyGlobalInit(VD);

  // 计算初始值
  llvm::Constant *InitVal = nullptr;
  bool IsConstant = VD->isConstexpr() ||
                    VD->getType().isConstQualified();

  if (Init == InitKind::ConstantInitialization) {
    // 常量初始化：直接计算初始值
    if (Expr *InitExpr = VD->getInit()) {
      InitVal = getConstants().EmitConstantForType(InitExpr, VD->getType());
    }
  }
  // 动态初始化和零初始化：使用零值作为初始值
  // 动态初始化的变量稍后通过 EmitDynamicGlobalInit 处理
  if (!InitVal) {
    InitVal = getConstants().EmitZeroValue(VD->getType());
  }

  // 计算 Linkage（使用新的 GetVariableLinkage）
  auto Linkage = GetVariableLinkage(VD);

  auto *GV = new llvm::GlobalVariable(
      *TheModule, Ty, IsConstant, Linkage, InitVal,
      Mangle->getMangledName(VD));

  // 设置对齐
  GV->setAlignment(llvm::Align(getTarget().getTypeAlign(VD->getType())));

  // 应用属性（visibility, weak, dll 等）
  auto Attrs = GetGlobalDeclAttributes(VD);
  ApplyGlobalValueAttributes(GV, Attrs);

  // 注册映射
  GlobalValues[VD] = GV;

  // 如果是动态初始化，加入延迟队列
  if (Init == InitKind::DynamicInitialization) {
    DynamicInitVars.push_back(VD);
  }

  // 生成全局变量调试信息
  if (DebugInfo->isInitialized()) {
    DebugInfo->EmitGlobalVarDI(VD, GV);
  }

  return GV;
}

llvm::GlobalVariable *CodeGenModule::GetGlobalVar(VarDecl *VD) {
  auto It = GlobalValues.find(VD);
  if (It != GlobalValues.end())
    return llvm::dyn_cast_or_null<llvm::GlobalVariable>(It->second);
  return nullptr;
}

//===----------------------------------------------------------------------===//
// 函数生成
//===----------------------------------------------------------------------===//

llvm::Function *CodeGenModule::EmitFunction(FunctionDecl *FD) {
  if (!FD) return nullptr;

  llvm::Function *Fn = GetOrCreateFunctionDecl(FD);
  if (!Fn) return nullptr;

  // 如果没有函数体，只生成声明
  if (!FD->getBody()) return Fn;

  // 如果函数体已经生成过（检查是否有基本块）
  if (!Fn->empty()) return Fn;

  // 构造函数分派到 CGCXX::EmitConstructor
  if (auto *Ctor = llvm::dyn_cast<CXXConstructorDecl>(FD)) {
    getCXX().EmitConstructor(Ctor, Fn);
    return Fn;
  }

  // 析构函数分派到 CGCXX::EmitDestructor
  if (auto *Dtor = llvm::dyn_cast<CXXDestructorDecl>(FD)) {
    getCXX().EmitDestructor(Dtor, Fn);
    return Fn;
  }

  // 使用 CodeGenFunction 生成函数体
  CodeGenFunction CGF(*this);

  // 生成函数调试信息
  if (DebugInfo->isInitialized()) {
    DebugInfo->setFunctionLocation(Fn, FD);
  }

  CGF.EmitFunctionBody(FD, Fn);

  return Fn;
}

llvm::Function *CodeGenModule::GetFunction(FunctionDecl *FD) {
  auto It = GlobalValues.find(FD);
  if (It != GlobalValues.end())
    return llvm::dyn_cast_or_null<llvm::Function>(It->second);
  return nullptr;
}

llvm::Function *CodeGenModule::GetOrCreateFunctionDecl(FunctionDecl *FD) {
  if (!FD) return nullptr;

  // 检查缓存
  if (auto *Existing = GetFunction(FD))
    return Existing;

  // 获取函数类型
  llvm::FunctionType *FTy = getTypes().GetFunctionTypeForDecl(FD);
  if (!FTy) return nullptr;

  // 计算 Linkage（使用新的 GetFunctionLinkage）
  auto Linkage = GetFunctionLinkage(FD);

  llvm::Function *Fn = llvm::Function::Create(
      FTy, Linkage, Mangle->getMangledName(FD), TheModule.get());

  // 设置参数名和 ABI 属性（sret/inreg）
  const FunctionABITy *ABI = getTypes().GetFunctionABI(FD);
  unsigned ArgIdx = 0;
  for (auto &Arg : Fn->args()) {
    // 设置 ABI 属性
    if (ABI && ArgIdx < ABI->ParamInfos.size()) {
      const auto &Info = ABI->ParamInfos[ArgIdx];
      if (Info.isSRet()) {
        Arg.addAttr(llvm::Attribute::getWithStructRetType(
            LLVMCtx, Info.SRetType));
        Arg.addAttr(llvm::Attribute::NoAlias);
      }
      if (Info.isInReg()) {
        Arg.addAttr(llvm::Attribute::InReg);
      }
    }

    // 设置参数名（跳过隐式参数）
    // 隐式参数计数：sret(1) + this(1)
    unsigned ImplicitArgs = 0;
    if (ABI && !ABI->ParamInfos.empty() && ABI->ParamInfos[0].isSRet())
      ++ImplicitArgs;
    if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(FD)) {
      if (!MD->isStatic()) ++ImplicitArgs;
    }

    if (ArgIdx >= ImplicitArgs) {
      unsigned ParamIdx = ArgIdx - ImplicitArgs;
      if (ParamIdx < FD->getNumParams()) {
        ParmVarDecl *PVD = FD->getParamDecl(ParamIdx);
        Arg.setName(PVD->getName());
      }
    }
    ++ArgIdx;
  }

  // 设置函数属性
  if (FD->isInline()) {
    Fn->addFnAttr(llvm::Attribute::AlwaysInline);
  }
  if (FD->hasNoexceptSpec() && FD->getNoexceptValue()) {
    Fn->setDoesNotThrow();
  }
  if (FD->hasAttr("noreturn")) {
    Fn->setDoesNotReturn();
  }

  // 应用全局属性（visibility, weak, dll 等）
  auto Attrs = GetGlobalDeclAttributes(FD);
  ApplyGlobalValueAttributes(Fn, Attrs);

  // 注册映射
  GlobalValues[FD] = Fn;

  return Fn;
}

//===----------------------------------------------------------------------===//
// C++ 特有生成
//===----------------------------------------------------------------------===//

void CodeGenModule::EmitVTable(CXXRecordDecl *RD) {
  if (!RD) return;
  getCXX().EmitVTable(RD);
}

void CodeGenModule::EmitClassLayout(CXXRecordDecl *RD) {
  if (!RD) return;
  getCXX().ComputeClassLayout(RD);

  // 如果类有虚函数，记录需要生成 vtable
  bool HasVirtual = false;
  for (CXXMethodDecl *MD : RD->methods()) {
    if (MD->isVirtual()) { HasVirtual = true; break; }
  }
  if (HasVirtual) {
    VTableClasses.push_back(RD);
  }
}

void CodeGenModule::EmitVTables() {
  for (CXXRecordDecl *RD : VTableClasses) {
    getCXX().EmitVTable(RD);
  }
  VTableClasses.clear();
}

//===----------------------------------------------------------------------===//
// 全局构造/析构
//===----------------------------------------------------------------------===//

void CodeGenModule::AddGlobalCtor(FunctionDecl *FD, int Priority) {
  GlobalCtors.emplace_back(FD, Priority);
}

void CodeGenModule::AddGlobalDtor(FunctionDecl *FD, int Priority) {
  GlobalDtors.emplace_back(FD, Priority);
}

void CodeGenModule::EmitGlobalCtorDtors() {
  // 合并 GlobalCtors 和 GlobalCtorsDirect
  llvm::SmallVector<std::pair<llvm::Function *, int>, 8> AllCtors;

  // 从 FunctionDecl 的构造函数
  for (auto &[FD, Priority] : GlobalCtors) {
    if (FD) {
      llvm::Function *Fn = GetOrCreateFunctionDecl(FD);
      if (Fn) AllCtors.emplace_back(Fn, Priority);
    }
  }

  // 从直接 llvm::Function 的构造函数（动态初始化）
  for (auto *Fn : GlobalCtorsDirect) {
    AllCtors.emplace_back(Fn, 65535);
  }

  // 生成 llvm.global_ctors
  if (!AllCtors.empty()) {
    llvm::SmallVector<llvm::Constant *, 8> Ctors;
    llvm::StructType *EntryTy = llvm::StructType::get(
        LLVMCtx,
        {llvm::Type::getInt32Ty(LLVMCtx),
         llvm::PointerType::get(LLVMCtx, 0),
         llvm::PointerType::get(LLVMCtx, 0)});

    for (auto &[Fn, Priority] : AllCtors) {
      Ctors.push_back(llvm::ConstantStruct::get(
          EntryTy,
          {llvm::ConstantInt::get(llvm::Type::getInt32Ty(LLVMCtx), Priority),
           Fn,
           llvm::ConstantPointerNull::get(llvm::PointerType::get(LLVMCtx, 0))}));
    }

    if (!Ctors.empty()) {
      auto *AT = llvm::ArrayType::get(EntryTy, Ctors.size());
      new llvm::GlobalVariable(*TheModule, AT, true,
                               llvm::GlobalValue::AppendingLinkage,
                               llvm::ConstantArray::get(AT, Ctors),
                               "llvm.global_ctors");
    }
  }

  // 生成 llvm.global_dtors
  if (!GlobalDtors.empty()) {
    llvm::SmallVector<llvm::Constant *, 8> Dtors;
    llvm::StructType *EntryTy = llvm::StructType::get(
        LLVMCtx,
        {llvm::Type::getInt32Ty(LLVMCtx),
         llvm::PointerType::get(LLVMCtx, 0),
         llvm::PointerType::get(LLVMCtx, 0)});

    for (auto &[FD, Priority] : GlobalDtors) {
      llvm::Function *Fn = GetOrCreateFunctionDecl(FD);
      if (!Fn) continue;

      Dtors.push_back(llvm::ConstantStruct::get(
          EntryTy,
          {llvm::ConstantInt::get(llvm::Type::getInt32Ty(LLVMCtx), Priority),
           Fn,
           llvm::ConstantPointerNull::get(llvm::PointerType::get(LLVMCtx, 0))}));
    }

    if (!Dtors.empty()) {
      auto *AT = llvm::ArrayType::get(EntryTy, Dtors.size());
      new llvm::GlobalVariable(*TheModule, AT, true,
                               llvm::GlobalValue::AppendingLinkage,
                               llvm::ConstantArray::get(AT, Dtors),
                               "llvm.global_dtors");
    }
  }
}


