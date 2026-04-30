#include "blocktype/Backend/IRToLLVMConverter.h"

#include <cassert>
#include <vector>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/raw_ostream.h"

#include "blocktype/Basic/DiagnosticIDs.h"

namespace blocktype::backend {

DiagnosticsEngine& IRToLLVMConverter::getNullDiags() {
  static llvm::raw_null_ostream NullOS;
  static DiagnosticsEngine NullDiags(NullOS);
  return NullDiags;
}

IRToLLVMConverter::IRToLLVMConverter(ir::IRTypeContext& IRCtx,
                                     llvm::LLVMContext& LLVMCtx,
                                     const BackendOptions& Opts,
                                     DiagnosticsEngine& Diags)
  : IRCtx(IRCtx), LLVMCtx(LLVMCtx), Opts(Opts), Diags(Diags) {}

IRToLLVMConverter::~IRToLLVMConverter() = default;

// ============================================================
// 类型映射
// ============================================================

llvm::Type* IRToLLVMConverter::mapType(const ir::IRType* T) {
  if (!T) return nullptr;
  auto It = TypeMap.find(T);
  if (It != TypeMap.end()) return It.getBucket()->Value;

  llvm::Type* Result = nullptr;
  switch (T->getKind()) {
  case ir::IRType::Void:
    Result = llvm::Type::getVoidTy(LLVMCtx); break;
  case ir::IRType::Bool:
    Result = llvm::Type::getInt1Ty(LLVMCtx); break;
  case ir::IRType::Integer: {
    unsigned BW = static_cast<const ir::IRIntegerType*>(T)->getBitWidth();
    Result = llvm::Type::getIntNTy(LLVMCtx, BW); break;
  }
  case ir::IRType::Float: {
    unsigned BW = static_cast<const ir::IRFloatType*>(T)->getBitWidth();
    switch (BW) {
    case 16: Result = llvm::Type::getHalfTy(LLVMCtx); break;
    case 32: Result = llvm::Type::getFloatTy(LLVMCtx); break;
    case 64: Result = llvm::Type::getDoubleTy(LLVMCtx); break;
    case 80: Result = llvm::Type::getX86_FP80Ty(LLVMCtx); break;
    case 128: Result = llvm::Type::getFP128Ty(LLVMCtx); break;
    default:
      Diags.report(blocktype::SourceLocation{}, DiagID::err_ir_to_llvm_conversion_failed,
                   "unsupported float bit width");
      return nullptr;
    }
    break;
  }
  case ir::IRType::Pointer: {
    auto* P = static_cast<const ir::IRPointerType*>(T);
    llvm::Type* Pointee = mapType(P->getPointeeType());
    if (!Pointee) return nullptr;
    Result = llvm::PointerType::get(Pointee, P->getAddressSpace()); break;
  }
  case ir::IRType::Array: {
    auto* A = static_cast<const ir::IRArrayType*>(T);
    llvm::Type* Elem = mapType(A->getElementType());
    if (!Elem) return nullptr;
    Result = llvm::ArrayType::get(Elem, A->getNumElements()); break;
  }
  case ir::IRType::Struct: {
    auto* S = static_cast<const ir::IRStructType*>(T);
    std::vector<llvm::Type*> Elems;
    for (auto* E : S->getElements()) {
      auto* M = mapType(E);
      if (!M) return nullptr;
      Elems.push_back(M);
    }
    if (S->getName().empty()) {
      Result = llvm::StructType::get(LLVMCtx, Elems, S->isPacked());
    } else {
      auto* PH = llvm::StructType::create(LLVMCtx, S->getName().str());
      TypeMap[T] = PH;
      PH->setBody(Elems, S->isPacked());
      return PH;
    }
    break;
  }
  case ir::IRType::Function: {
    auto* F = static_cast<const ir::IRFunctionType*>(T);
    auto* Ret = mapType(F->getReturnType());
    if (!Ret) return nullptr;
    std::vector<llvm::Type*> Params;
    for (auto* P : F->getParamTypes()) {
      auto* M = mapType(P);
      if (!M) return nullptr;
      Params.push_back(M);
    }
    Result = llvm::FunctionType::get(Ret, Params, F->isVarArg()); break;
  }
  case ir::IRType::Vector: {
    auto* V = static_cast<const ir::IRVectorType*>(T);
    auto* Elem = mapType(V->getElementType());
    if (!Elem) return nullptr;
    Result = llvm::FixedVectorType::get(Elem, V->getNumElements()); break;
  }
  case ir::IRType::Opaque: {
    auto* O = static_cast<const ir::IROpaqueType*>(T);
    Result = llvm::StructType::create(LLVMCtx, O->getName().str()); break;
  }
  }
  if (Result) TypeMap[T] = Result;
  return Result;
}

// ============================================================
// 常量映射
// ============================================================

llvm::Constant* IRToLLVMConverter::convertConstant(const ir::IRConstant* C) {
  if (!C) return nullptr;
  auto It = ValueMap.find(C);
  if (It != ValueMap.end()) return static_cast<llvm::Constant*>(It.getBucket()->Value);

  llvm::Constant* Result = nullptr;
  switch (C->getValueKind()) {
  case ir::ValueKind::ConstantInt: {
    auto* CI = static_cast<const ir::IRConstantInt*>(C);
    auto* Ty = mapType(CI->getType());
    if (Ty) Result = llvm::ConstantInt::get(Ty, CI->getZExtValue());
    break;
  }
  case ir::ValueKind::ConstantFloat: {
    auto* CF = static_cast<const ir::IRConstantFP*>(C);
    auto* Ty = mapType(CF->getType());
    if (Ty) Result = llvm::ConstantFP::get(Ty, CF->getValue().getRawValue());
    break;
  }
  case ir::ValueKind::ConstantNull: {
    auto* Ty = mapType(C->getType());
    if (Ty) Result = llvm::ConstantPointerNull::get(llvm::PointerType::get(Ty, 0));
    break;
  }
  case ir::ValueKind::ConstantUndef: {
    auto* Ty = mapType(C->getType());
    if (Ty) Result = llvm::UndefValue::get(Ty);
    break;
  }
  case ir::ValueKind::ConstantAggregateZero: {
    auto* Ty = mapType(C->getType());
    if (Ty) Result = llvm::ConstantAggregateZero::get(Ty);
    break;
  }
  case ir::ValueKind::ConstantStruct: {
    auto* CS = static_cast<const ir::IRConstantStruct*>(C);
    auto* Ty = mapType(CS->getType());
    if (!Ty) return nullptr;
    std::vector<llvm::Constant*> Elems;
    for (auto* E : CS->getElements()) {
      auto* M = convertConstant(E);
      if (!M) return nullptr;
      Elems.push_back(M);
    }
    Result = llvm::ConstantStruct::get(static_cast<llvm::StructType*>(Ty), Elems);
    break;
  }
  case ir::ValueKind::ConstantArray: {
    auto* CA = static_cast<const ir::IRConstantArray*>(C);
    auto* Ty = mapType(CA->getType());
    if (!Ty) return nullptr;
    std::vector<llvm::Constant*> Elems;
    for (auto* E : CA->getElements()) {
      auto* M = convertConstant(E);
      if (!M) return nullptr;
      Elems.push_back(M);
    }
    Result = llvm::ConstantArray::get(static_cast<llvm::ArrayType*>(Ty), Elems);
    break;
  }
  case ir::ValueKind::ConstantFunctionRef: {
    auto* FR = static_cast<const ir::IRConstantFunctionRef*>(C);
    Result = mapFunction(FR->getFunction());
    break;
  }
  case ir::ValueKind::ConstantGlobalRef: {
    auto* GR = static_cast<const ir::IRConstantGlobalRef*>(C);
    auto GIt = GlobalVarMap.find(GR->getGlobal());
    if (GIt != GlobalVarMap.end()) Result = GIt.getBucket()->Value;
    break;
  }
  default: return nullptr;
  }
  if (Result) ValueMap[C] = Result;
  return Result;
}

// ============================================================
// 值映射
// ============================================================

llvm::Value* IRToLLVMConverter::mapValue(const ir::IRValue* V) {
  if (!V) return nullptr;
  auto It = ValueMap.find(V);
  if (It != ValueMap.end()) return It.getBucket()->Value;
  if (V->isConstant()) return convertConstant(static_cast<const ir::IRConstant*>(V));
  if (V->getValueKind() == ir::ValueKind::BasicBlockRef) {
    auto* BBRef = static_cast<const ir::IRBasicBlockRef*>(V);
    return mapBasicBlock(BBRef->getBasicBlock());
  }
  return nullptr;
}

// ============================================================
// 函数映射
// ============================================================

static llvm::GlobalValue::LinkageTypes toLLVMLinkage(ir::LinkageKind L) {
  switch (L) {
  case ir::LinkageKind::Private: return llvm::Function::PrivateLinkage;
  case ir::LinkageKind::Internal: return llvm::Function::InternalLinkage;
  case ir::LinkageKind::LinkOnce: return llvm::Function::LinkOnceAnyLinkage;
  case ir::LinkageKind::LinkOnceODR: return llvm::Function::LinkOnceODRLinkage;
  case ir::LinkageKind::Weak: return llvm::Function::WeakAnyLinkage;
  case ir::LinkageKind::WeakODR: return llvm::Function::WeakODRLinkage;
  case ir::LinkageKind::ExternalWeak: return llvm::Function::ExternalWeakLinkage;
  case ir::LinkageKind::AvailableExternally: return llvm::Function::AvailableExternallyLinkage;
  default: return llvm::Function::ExternalLinkage;
  }
}

llvm::Function* IRToLLVMConverter::mapFunction(const ir::IRFunction* F) {
  if (!F) return nullptr;
  auto It = FunctionMap.find(F);
  if (It != FunctionMap.end()) return It.getBucket()->Value;

  auto* FnTy = static_cast<llvm::FunctionType*>(mapType(F->getFunctionType()));
  if (!FnTy) return nullptr;

  auto* LLVMF = llvm::Function::Create(FnTy, toLLVMLinkage(F->getLinkage()),
                                         F->getName().str(), TheModule.get());
  for (unsigned i = 0; i < F->getNumArgs(); ++i) {
    llvm::Argument* Arg = LLVMF->getArg(i);
    if (!F->getArg(i)->getName().empty()) Arg->setName(F->getArg(i)->getName().str());
    ValueMap[F->getArg(i)] = Arg;
  }
  FunctionMap[F] = LLVMF;
  return LLVMF;
}

// ============================================================
// 基本块映射
// ============================================================

llvm::BasicBlock* IRToLLVMConverter::mapBasicBlock(const ir::IRBasicBlock* BB) {
  if (!BB) return nullptr;
  auto It = BlockMap.find(BB);
  if (It != BlockMap.end()) return It.getBucket()->Value;
  auto* LLVMBB = llvm::BasicBlock::Create(LLVMCtx, BB->getName().str());
  BlockMap[BB] = LLVMBB;
  return LLVMBB;
}

// ============================================================
// 全局变量转换
// ============================================================

void IRToLLVMConverter::convertGlobalVariable(ir::IRGlobalVariable& IRGV) {
  auto* Ty = mapType(IRGV.getType());
  if (!Ty) return;
  llvm::Constant* Init = nullptr;
  if (IRGV.hasInitializer()) Init = convertConstant(IRGV.getInitializer());

  auto Linkage = toLLVMLinkage(IRGV.getLinkage());
  auto* GVar = new llvm::GlobalVariable(*TheModule, Ty, IRGV.isConstant(),
    Linkage, Init, IRGV.getName().str(), nullptr,
    llvm::GlobalVariable::NotThreadLocal, IRGV.getAddressSpace());
  if (IRGV.getAlignment() > 0) GVar->setAlignment(llvm::Align(IRGV.getAlignment()));
  GlobalVarMap[&IRGV] = GVar;
}

// ============================================================
// 指令转换
// ============================================================

void IRToLLVMConverter::convertInstruction(ir::IRInstruction& I, llvm::IRBuilder<>& Builder) {
  using Op = ir::Opcode;
  auto op = [&](unsigned i) -> llvm::Value* {
    return (i < I.getNumOperands()) ? mapValue(I.getOperand(i)) : nullptr;
  };

  switch (I.getOpcode()) {
  // 终结指令
  case Op::Ret: {
    (I.getNumOperands() > 0 && I.getOperand(0)) ? Builder.CreateRet(op(0)) : Builder.CreateRetVoid();
    break;
  }
  case Op::Br: {
    if (auto* R = static_cast<ir::IRBasicBlockRef*>(I.getOperand(0)))
      if (auto* D = mapBasicBlock(R->getBasicBlock())) Builder.CreateBr(D);
    break;
  }
  case Op::CondBr: {
    auto* C = op(0);
    auto* T = mapBasicBlock(static_cast<ir::IRBasicBlockRef*>(I.getOperand(1))->getBasicBlock());
    auto* F = mapBasicBlock(static_cast<ir::IRBasicBlockRef*>(I.getOperand(2))->getBasicBlock());
    if (C && T && F) Builder.CreateCondBr(C, T, F);
    break;
  }
  case Op::Switch: {
    auto* V = op(0);
    auto* Def = mapBasicBlock(static_cast<ir::IRBasicBlockRef*>(I.getOperand(1))->getBasicBlock());
    if (V && Def) {
      auto* Sw = Builder.CreateSwitch(V, Def, I.getNumOperands() - 2);
      for (unsigned i = 2; i + 1 < I.getNumOperands(); i += 2)
        if (auto* CV = op(i))
          if (auto* CB = mapBasicBlock(static_cast<ir::IRBasicBlockRef*>(I.getOperand(i+1))->getBasicBlock()))
            Sw->addCase(llvm::cast<llvm::ConstantInt>(CV), CB);
    }
    break;
  }
  case Op::Invoke: {
    auto* Callee = op(0);
    auto* NBB = mapBasicBlock(static_cast<ir::IRBasicBlockRef*>(I.getOperand(1))->getBasicBlock());
    auto* UBB = mapBasicBlock(static_cast<ir::IRBasicBlockRef*>(I.getOperand(2))->getBasicBlock());
    if (Callee && NBB && UBB) {
      std::vector<llvm::Value*> Args;
      for (unsigned i = 3; i < I.getNumOperands(); ++i) if (auto* A = op(i)) Args.push_back(A);
      auto* FnTy = llvm::cast<llvm::Function>(Callee)->getFunctionType();
      auto* R = Builder.CreateInvoke(FnTy, Callee, NBB, UBB, Args);
      ValueMap[&I] = R;
    }
    break;
  }
  case Op::Unreachable: Builder.CreateUnreachable(); break;
  case Op::Resume: if (auto* V = op(0)) Builder.CreateResume(V); break;

  // 整数二元
  case Op::Add: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateAdd(L,R); break; }
  case Op::Sub: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateSub(L,R); break; }
  case Op::Mul: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateMul(L,R); break; }
  case Op::UDiv: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateUDiv(L,R); break; }
  case Op::SDiv: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateSDiv(L,R); break; }
  case Op::URem: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateURem(L,R); break; }
  case Op::SRem: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateSRem(L,R); break; }

  // 浮点二元
  case Op::FAdd: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateFAdd(L,R); break; }
  case Op::FSub: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateFSub(L,R); break; }
  case Op::FMul: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateFMul(L,R); break; }
  case Op::FDiv: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateFDiv(L,R); break; }
  case Op::FRem: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateFRem(L,R); break; }

  // 位运算
  case Op::Shl: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateShl(L,R); break; }
  case Op::LShr: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateLShr(L,R); break; }
  case Op::AShr: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateAShr(L,R); break; }
  case Op::And: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateAnd(L,R); break; }
  case Op::Or:  { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateOr(L,R); break; }
  case Op::Xor: { auto* L=op(0),*R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateXor(L,R); break; }

  // 内存
  case Op::Alloca: { auto* Ty=mapType(I.getType()); if(Ty) ValueMap[&I]=Builder.CreateAlloca(Ty); break; }
  case Op::Load: { auto* Ty=mapType(I.getType()); auto* P=op(0); if(Ty&&P) ValueMap[&I]=Builder.CreateLoad(Ty,P); break; }
  case Op::Store: { auto* V=op(0); auto* P=op(1); if(V&&P) Builder.CreateStore(V,P); break; }
  case Op::GEP: {
    auto* STy = (I.getNumOperands()>0) ? mapType(I.getOperandType(0)) : nullptr;
    auto* P = op(0);
    if (STy && P) {
      std::vector<llvm::Value*> Idx;
      for (unsigned i=1;i<I.getNumOperands();++i) if(auto* x=op(i)) Idx.push_back(x);
      ValueMap[&I] = Builder.CreateGEP(STy, P, Idx);
    }
    break;
  }
  case Op::Memcpy: { auto* D=op(0); auto* S=op(1); auto* Sz=op(2); if(D&&S&&Sz) Builder.CreateMemCpy(D,llvm::Align(),S,llvm::Align(),Sz); break; }
  case Op::Memset: { auto* P=op(0); auto* V=op(1); auto* Sz=op(2); if(P&&V&&Sz) Builder.CreateMemSet(P,V,Sz,llvm::Align()); break; }

  // 转换
  case Op::Trunc: { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreateTrunc(V,D); break; }
  case Op::ZExt:  { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreateZExt(V,D); break; }
  case Op::SExt:  { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreateSExt(V,D); break; }
  case Op::FPTrunc: { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreateFPTrunc(V,D); break; }
  case Op::FPExt: { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreateFPExt(V,D); break; }
  case Op::FPToSI: { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreateFPToSI(V,D); break; }
  case Op::FPToUI: { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreateFPToUI(V,D); break; }
  case Op::SIToFP: { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreateSIToFP(V,D); break; }
  case Op::UIToFP: { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreateUIToFP(V,D); break; }
  case Op::PtrToInt: { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreatePtrToInt(V,D); break; }
  case Op::IntToPtr: { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreateIntToPtr(V,D); break; }
  case Op::BitCast: { auto* V=op(0); auto* D=mapType(I.getType()); if(V&&D) ValueMap[&I]=Builder.CreateBitCast(V,D); break; }

  // 比较
  case Op::ICmp: { auto* L=op(0); auto* R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateICmp(static_cast<llvm::CmpInst::Predicate>(I.getICmpPredicate()),L,R); break; }
  case Op::FCmp: { auto* L=op(0); auto* R=op(1); if(L&&R) ValueMap[&I]=Builder.CreateFCmp(static_cast<llvm::CmpInst::Predicate>(I.getFCmpPredicate()),L,R); break; }

  // 调用
  case Op::Call: {
    auto* Callee = op(0);
    if (!Callee) break;
    std::vector<llvm::Value*> Args;
    for (unsigned i=1;i<I.getNumOperands();++i) if(auto*A=op(i)) Args.push_back(A);
    llvm::FunctionType* FnTy = nullptr;
    if (auto* F = llvm::dyn_cast<llvm::Function>(Callee)) FnTy = F->getFunctionType();
    else FnTy = llvm::FunctionType::get(mapType(I.getType()),{},false);
    ValueMap[&I] = Builder.CreateCall(FnTy, Callee, Args);
    break;
  }

  // 聚合
  case Op::Phi: { auto* Ty=mapType(I.getType()); if(Ty) ValueMap[&I]=Builder.CreatePHI(Ty,I.getNumOperands()/2); break; }
  case Op::Select: { auto*C=op(0),*T=op(1),*F=op(2); if(C&&T&&F) ValueMap[&I]=Builder.CreateSelect(C,T,F); break; }
  case Op::ExtractValue: {
    auto* Agg = op(0);
    if (!Agg) break;
    std::vector<unsigned> Indices;
    for (unsigned i = 1; i < I.getNumOperands(); ++i) {
      if (auto* C = llvm::dyn_cast_or_null<llvm::ConstantInt>(op(i)))
        Indices.push_back(static_cast<unsigned>(C->getZExtValue()));
    }
    if (!Indices.empty()) ValueMap[&I] = Builder.CreateExtractValue(Agg, Indices);
    break;
  }
  case Op::InsertValue: {
    auto* Agg = op(0);
    auto* Val = op(1);
    if (!Agg || !Val) break;
    std::vector<unsigned> Indices;
    for (unsigned i = 2; i < I.getNumOperands(); ++i) {
      if (auto* C = llvm::dyn_cast_or_null<llvm::ConstantInt>(op(i)))
        Indices.push_back(static_cast<unsigned>(C->getZExtValue()));
    }
    if (!Indices.empty()) ValueMap[&I] = Builder.CreateInsertValue(Agg, Val, Indices);
    break;
  }
  case Op::ExtractElement: { auto* V=op(0); auto* Idx=op(1); if(V&&Idx) ValueMap[&I]=Builder.CreateExtractElement(V,Idx); break; }
  case Op::InsertElement: { auto* V=op(0); auto* E=op(1); auto* Idx=op(2); if(V&&E&&Idx) ValueMap[&I]=Builder.CreateInsertElement(V,E,Idx); break; }
  case Op::ShuffleVector: { auto* V1=op(0); auto* V2=op(1); if(V1&&V2&&I.getNumOperands()>2) { if(auto* M=op(2)) ValueMap[&I]=Builder.CreateShuffleVector(V1,V2,llvm::cast<llvm::Constant>(M)); } break; }

  // 调试信息 — Phase C 中简化处理
  case Op::DbgDeclare: break;
  case Op::DbgValue: break;
  case Op::DbgLabel: break;

  // FFI 指令 — 按 Call 映射
  case Op::FFICall: {
    auto* Callee = op(0);
    if (!Callee) break;
    std::vector<llvm::Value*> Args;
    for (unsigned i=1;i<I.getNumOperands();++i) if(auto*A=op(i)) Args.push_back(A);
    auto* FnTy = llvm::FunctionType::get(mapType(I.getType()),{},false);
    ValueMap[&I] = Builder.CreateCall(FnTy, Callee, Args);
    break;
  }
  case Op::FFICheck: break;
  case Op::FFICoerce: break;
  case Op::FFIUnwind: break;

  // 原子操作
  case Op::AtomicLoad: { auto* Ty=mapType(I.getType()); auto* P=op(0); if(Ty&&P) ValueMap[&I]=Builder.CreateLoad(Ty,P); break; }
  case Op::AtomicStore: { auto* V=op(0); auto* P=op(1); if(V&&P) Builder.CreateStore(V,P); break; }
  case Op::AtomicRMW: {
    auto* Ptr = op(0);
    auto* Val = op(1);
    if (!Ptr || !Val) break;
    ValueMap[&I] = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Add, Ptr, Val,
      llvm::Align(), llvm::AtomicOrdering::SequentiallyConsistent);
    break;
  }
  case Op::AtomicCmpXchg: {
    auto* Ptr = op(0);
    auto* Cmp = op(1);
    auto* New = op(2);
    if (!Ptr || !Cmp || !New) break;
    ValueMap[&I] = Builder.CreateAtomicCmpXchg(Ptr, Cmp, New, llvm::Align(),
      llvm::AtomicOrdering::SequentiallyConsistent,
      llvm::AtomicOrdering::SequentiallyConsistent);
    break;
  }
  case Op::Fence: Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent); break;

  // Cpp Dialect — C.8 VTable/RTTI（Itanium ABI）
  // #20: DynamicCast → __dynamic_cast 运行时调用
  case Op::DynamicCast: {
    auto* Obj = op(0);
    if (!Obj) break;
    auto* VoidPtrTy = llvm::PointerType::get(llvm::Type::getInt8Ty(LLVMCtx), 0);
    auto* DCFnTy = llvm::FunctionType::get(VoidPtrTy,
      {VoidPtrTy, VoidPtrTy, VoidPtrTy, llvm::Type::getInt64Ty(LLVMCtx)}, false);
    auto DCFn = TheModule->getOrInsertFunction("__dynamic_cast", DCFnTy);
    auto* SrcRTTI = (I.getNumOperands() > 1) ? op(1) : llvm::ConstantPointerNull::get(VoidPtrTy);
    auto* DstRTTI = (I.getNumOperands() > 2) ? op(2) : llvm::ConstantPointerNull::get(VoidPtrTy);
    auto* Offset = (I.getNumOperands() > 3) ? op(3) : llvm::ConstantInt::get(llvm::Type::getInt64Ty(LLVMCtx), 0);
    auto* ObjPtr = Builder.CreateBitCast(Obj, VoidPtrTy);
    ValueMap[&I] = Builder.CreateCall(DCFn, {ObjPtr, SrcRTTI, DstRTTI, Offset});
    break;
  }
  // #21: VtableDispatch → 虚表查找 + 间接调用
  case Op::VtableDispatch: {
    auto* Obj = op(0);
    auto* VtableIdxVal = (I.getNumOperands() > 1) ? op(1) : nullptr;
    if (!Obj) break;
    auto* VoidPtrTy = llvm::PointerType::get(llvm::Type::getInt8Ty(LLVMCtx), 0);
    auto* IntPtrTy = llvm::Type::getInt64Ty(LLVMCtx);
    // 加载 vtable 指针（对象第一个字段）
    auto* VtblPtrPtr = Builder.CreateBitCast(Obj, llvm::PointerType::get(VoidPtrTy, 0));
    auto* VtblPtr = Builder.CreateLoad(VoidPtrTy, VtblPtrPtr);
    // 计算偏移：idx * sizeof(void*)
    auto* Idx = VtableIdxVal ? VtableIdxVal : llvm::ConstantInt::get(IntPtrTy, 0);
    auto* ByteOffset = Builder.CreateMul(Idx, llvm::ConstantInt::get(IntPtrTy, 8));
    auto* VtblInt = Builder.CreatePtrToInt(VtblPtr, IntPtrTy);
    auto* EntryAddr = Builder.CreateAdd(VtblInt, ByteOffset);
    auto* FnPtrPtr = Builder.CreateIntToPtr(EntryAddr, llvm::PointerType::get(VoidPtrTy, 0));
    auto* FnPtr = Builder.CreateLoad(VoidPtrTy, FnPtrPtr);
    auto* RetTy = mapType(I.getType());
    if (!RetTy) RetTy = llvm::Type::getVoidTy(LLVMCtx);
    std::vector<llvm::Value*> Args;
    for (unsigned i = 2; i < I.getNumOperands(); ++i) if (auto* A = op(i)) Args.push_back(A);
    auto* IndirectFnTy = llvm::FunctionType::get(RetTy, {}, false);
    auto* IndirectFnPtr = Builder.CreateBitCast(FnPtr, llvm::PointerType::get(IndirectFnTy, 0));
    ValueMap[&I] = Builder.CreateCall(IndirectFnTy, IndirectFnPtr, Args);
    break;
  }
  // #22: RTTITypeid → type_info 全局变量查找（Itanium ABI: vtable[-1]）
  case Op::RTTITypeid: {
    auto* Obj = op(0);
    if (!Obj) break;
    auto* VoidPtrTy = llvm::PointerType::get(llvm::Type::getInt8Ty(LLVMCtx), 0);
    auto* IntPtrTy = llvm::Type::getInt64Ty(LLVMCtx);
    auto* VtblPtrPtr = Builder.CreateBitCast(Obj, llvm::PointerType::get(VoidPtrTy, 0));
    auto* VtblPtr = Builder.CreateLoad(VoidPtrTy, VtblPtrPtr);
    auto* VtblInt = Builder.CreatePtrToInt(VtblPtr, IntPtrTy);
    auto* TypeInfoAddr = Builder.CreateSub(VtblInt, llvm::ConstantInt::get(IntPtrTy, 8));
    auto* TypeInfoPtrPtr = Builder.CreateIntToPtr(TypeInfoAddr, llvm::PointerType::get(VoidPtrTy, 0));
    ValueMap[&I] = Builder.CreateLoad(VoidPtrTy, TypeInfoPtrPtr);
    break;
  }

  // 未映射 — 发出诊断警告 (#7)
  case Op::TargetIntrinsic:
  case Op::MetaInlineAlways:
  case Op::MetaInlineNever:
  case Op::MetaHot:
  case Op::MetaCold:
    Diags.report(blocktype::SourceLocation{}, DiagID::warn_ir_opcode_not_supported,
                 "unsupported opcode");
    break;
  }
}

// ============================================================
// 函数转换
// ============================================================

void IRToLLVMConverter::convertFunction(ir::IRFunction& IRF, llvm::Function* LLVMF) {
  if (IRF.isDeclaration()) return;

  // 映射基本块
  for (auto& BB : IRF.getBasicBlocks()) {
    auto* LLVMBB = mapBasicBlock(BB.get());
    LLVMBB->insertInto(LLVMF);
  }

  // 设置入口
  llvm::IRBuilder<> Builder(&LLVMF->getEntryBlock());

  // 第一遍：映射所有指令（含 PHI 占位）
  for (auto& BB : IRF.getBasicBlocks()) {
    auto* LLVMBB = mapBasicBlock(BB.get());
    Builder.SetInsertPoint(LLVMBB);
    for (auto& Inst : BB->getInstList()) {
      convertInstruction(*Inst, Builder);
    }
  }

  // 第二遍：填充 PHI incoming values
  for (auto& BB : IRF.getBasicBlocks()) {
    for (auto& Inst : BB->getInstList()) {
      if (Inst->getOpcode() == ir::Opcode::Phi) {
        auto It = ValueMap.find(Inst.get());
        if (It == ValueMap.end()) continue;
        auto* PHINode = static_cast<llvm::PHINode*>(It.getBucket()->Value);
        for (unsigned i = 0; i + 1 < Inst->getNumOperands(); i += 2) {
          auto* Val = mapValue(Inst->getOperand(i));
          auto* PredBB = mapBasicBlock(
            static_cast<ir::IRBasicBlockRef*>(Inst->getOperand(i+1))->getBasicBlock());
          if (Val && PredBB) PHINode->addIncoming(Val, PredBB);
        }
      }
    }
  }
}

// ============================================================
// 调试信息转换（C.7）— 在 convert() 内部调用
// ============================================================

void IRToLLVMConverter::convertDebugInfo(ir::IRModule& IRModule, llvm::Module& LLVMMod) {
  bool hasAnyDbgInfo = false;
  for (auto& F : IRModule.getFunctions()) {
    if (!F->isDefinition()) continue;
    for (auto& BB : F->getBasicBlocks()) {
      for (auto& Inst : BB->getInstList()) {
        if (Inst->hasDebugInfo()) { hasAnyDbgInfo = true; break; }
      }
      if (hasAnyDbgInfo) break;
    }
    if (hasAnyDbgInfo) break;
  }
  // #17: 无调试信息不生成调试段
  if (!hasAnyDbgInfo) return;

  llvm::DIBuilder DB(LLVMMod, true);

  // #17: 使用 BackendOptions.DebugInfoFormat 控制 DWARF 版本
  unsigned DWARFVersion = 5; // 默认 DWARF5
  if (Opts.DebugInfoFormat == "dwarf4") DWARFVersion = 4;

  // #19: 从 ir::DICompileUnit 映射到 llvm::DICompileUnit
  llvm::DIFile* File = DB.createFile(IRModule.getName().str(), ".");
  auto* CU = DB.createCompileUnit(
    llvm::dwarf::DW_LANG_C_plus_plus, File, "blocktype",
    false, "", 0);

  // 设置 DWARF 版本
  LLVMMod.addModuleFlag(llvm::Module::Max, "Dwarf Version", DWARFVersion);

  // 遍历 IR 函数，映射调试信息
  for (auto& IRF : IRModule.getFunctions()) {
    if (!IRF->isDefinition()) continue;

    // #19: 从 ir::DISubprogram 映射
    llvm::DISubprogram* SP = DB.createFunction(
      CU, IRF->getName().str(), IRF->getName().str(), File,
      0, DB.createSubroutineType(DB.getOrCreateTypeArray({})),
      0, llvm::DINode::FlagZero, llvm::DISubprogram::SPFlagDefinition);

    auto* LLVMF = LLVMMod.getFunction(IRF->getName().str());
    if (LLVMF) LLVMF->setSubprogram(SP);

    // #18: 从 ir::debug::IRInstructionDebugInfo 转换调试信息
    if (!LLVMF) continue;
    for (auto& BB : IRF->getBasicBlocks()) {
      unsigned InstIdx = 0;
      for (auto& Inst : BB->getInstList()) {
        auto* DbgInfo = Inst->getDebugInfo();
        if (!DbgInfo || !DbgInfo->hasLocation()) { ++InstIdx; continue; }

        const auto& Loc = DbgInfo->getLocation();
        // #19: 创建 DILocation，关联 ir::DILocation 的行/列信息
        llvm::DILocation* DILoc = llvm::DILocation::get(
          LLVMCtx, Loc.Line, Loc.Column, SP);

        // 在 LLVM 指令上设置调试位置
        // 找到对应的 LLVM 指令（按索引匹配）
        auto* LLVMF_nc = LLVMMod.getFunction(IRF->getName().str());
        if (!LLVMF_nc) { ++InstIdx; continue; }
        unsigned BBIdx = 0;
        for (auto& LLVM_BB : *LLVMF_nc) {
          if (BBIdx == static_cast<unsigned>(
                std::distance(IRF->getBasicBlocks().begin(),
                  std::find_if(IRF->getBasicBlocks().begin(),
                    IRF->getBasicBlocks().end(),
                    [&](auto& x) { return x.get() == BB.get(); })))) {
            unsigned II = 0;
            for (auto& LI : LLVM_BB) {
              if (II == InstIdx) { LI.setDebugLoc(DILoc); break; }
              ++II;
            }
            break;
          }
          ++BBIdx;
        }
        ++InstIdx;
      }
    }
  }

  DB.finalize();
}

// ============================================================
// 主转换入口
// ============================================================

std::unique_ptr<llvm::Module> IRToLLVMConverter::convert(ir::IRModule& IRModule) {
  // 清除缓存
  TypeMap.clear();
  ValueMap.clear();
  FunctionMap.clear();
  BlockMap.clear();
  GlobalVarMap.clear();

  TheModule = std::make_unique<llvm::Module>(IRModule.getName().str(), LLVMCtx);
  if (!IRModule.getTargetTriple().empty())
    TheModule->setTargetTriple(IRModule.getTargetTriple().str());

  // 转换全局变量
  for (auto& GV : IRModule.getGlobals()) {
    convertGlobalVariable(*GV);
  }

  // 转换函数声明
  for (auto& F : IRModule.getFunctions()) {
    auto* LLVMF = mapFunction(F.get());
    if (LLVMF && F->isDefinition()) {
      convertFunction(*F, LLVMF);
    }
  }

  // #16: 调试信息映射在 convert() 内部完成
  if (Opts.DebugInfo) {
    convertDebugInfo(IRModule, *TheModule);
  }

  return std::move(TheModule);
}

} // namespace blocktype::backend
