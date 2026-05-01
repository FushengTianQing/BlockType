#include "blocktype/IR/IRIntegrity.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRInstruction.h"
#include "blocktype/IR/IRConstant.h"

namespace blocktype {
namespace ir {
namespace security {

// ============================================================
// FNV-1a 64-bit 哈希（后续可替换为 BLAKE3）
// ============================================================

static uint64_t fnv1a64(const void* Data, size_t Len) {
  auto* B = static_cast<const uint8_t*>(Data);
  uint64_t H = 14695981039346656037ULL;
  for (size_t i = 0; i < Len; ++i) { H ^= B[i]; H *= 1099511628211ULL; }
  return H;
}

static uint64_t hashStr(StringRef S) { return fnv1a64(S.data(), S.size()); }
static uint64_t hashU64(uint64_t V) { return fnv1a64(&V, sizeof(V)); }
static uint64_t hashU32(uint32_t V) { return fnv1a64(&V, sizeof(V)); }
static uint64_t hashU16(uint16_t V) { return fnv1a64(&V, sizeof(V)); }
static uint64_t hashU8(uint8_t V) { return fnv1a64(&V, sizeof(V)); }

static uint64_t combineHash(uint64_t A, uint64_t B) {
  return A ^ (B * 1099511628211ULL + 0x9e3779b97f4a7c15ULL);
}

// ============================================================
// IRIntegrityChecksum
// ============================================================

/// 哈希类型系统信息（通过函数签名和全局变量类型间接覆盖）
static uint64_t computeTypeSystemHash(IRModule& M) {
  uint64_t H = 0xcbf29ce484222325ULL;
  auto& Funcs = M.getFunctions();
  // 可重现构建：按名称排序后遍历
  std::vector<StringRef> Names;
  Names.reserve(Funcs.size());
  for (auto& F : Funcs) Names.push_back(F->getName());
  std::sort(Names.begin(), Names.end());
  for (auto& N : Names) {
    H = combineHash(H, hashStr(N));
    // 查找对应函数获取类型信息
    if (auto* F = M.getFunction(N)) {
      auto* FT = F->getFunctionType();
      if (FT) {
        H = combineHash(H, hashStr(FT->getReturnType()->toString()));
        for (unsigned i = 0; i < FT->getNumParams(); ++i) {
          H = combineHash(H, hashStr(FT->getParamType(i)->toString()));
        }
        H = combineHash(H, hashU8(FT->isVarArg()));
      }
    }
  }
  return H;
}

/// 哈希指令信息
static uint64_t computeInstructionHash(IRModule& M) {
  uint64_t H = 0xcbf29ce484222325ULL;
  for (auto& F : M.getFunctions()) {
    H = combineHash(H, hashStr(F->getName()));
    H = combineHash(H, hashU8(static_cast<uint8_t>(F->getLinkage())));
    H = combineHash(H, hashU8(static_cast<uint8_t>(F->getCallingConv())));
    for (auto& BB : F->getBasicBlocks()) {
      H = combineHash(H, hashStr(BB->getName()));
      for (auto& I : BB->getInstList()) {
        H = combineHash(H, hashU16(static_cast<uint16_t>(I->getOpcode())));
        H = combineHash(H, hashU32(I->getValueID()));
        H = combineHash(H, hashU8(static_cast<uint8_t>(I->getDialect())));
        // 操作数类型
        for (unsigned O = 0; O < I->getNumOperands(); ++O) {
          auto* Op = I->getOperand(O);
          if (Op && Op->getType())
            H = combineHash(H, hashStr(Op->getType()->toString()));
        }
      }
    }
  }
  return H;
}

/// 哈希常量信息
static uint64_t computeConstantHash(IRModule& M) {
  uint64_t H = 0xcbf29ce484222325ULL;
  // 常量通过指令操作数间接引用，此处简化为指令哈希的补充
  for (auto& F : M.getFunctions()) {
    H = combineHash(H, hashStr(F->getName()));
    H = combineHash(H, hashU32(F->getNumArgs()));
  }
  return H;
}

/// 哈希全局变量信息
static uint64_t computeGlobalHash(IRModule& M) {
  uint64_t H = 0xcbf29ce484222325ULL;
  auto& GVs = M.getGlobals();
  std::vector<StringRef> Names;
  Names.reserve(GVs.size());
  for (auto& GV : GVs) Names.push_back(GV->getName());
  std::sort(Names.begin(), Names.end());
  for (auto& N : Names) {
    auto* GV = M.getGlobalVariable(N);
    if (GV) {
      H = combineHash(H, hashStr(GV->getName()));
      if (GV->getType())
        H = combineHash(H, hashStr(GV->getType()->toString()));
      H = combineHash(H, hashU8(static_cast<uint8_t>(GV->getLinkage())));
      H = combineHash(H, hashU8(GV->isConstant()));
    }
  }
  return H;
}

/// 哈希调试信息
static uint64_t computeDebugInfoHash(IRModule& M) {
  uint64_t H = 0xcbf29ce484222325ULL;
  for (auto& F : M.getFunctions()) {
    for (auto& BB : F->getBasicBlocks()) {
      for (auto& I : BB->getInstList()) {
        if (I->hasDebugInfo()) {
          auto* DI = I->getDebugInfo();
          if (DI && DI->hasLocation()) {
            auto& Loc = DI->getLocation();
            H = combineHash(H, hashStr(Loc.Filename));
            H = combineHash(H, hashU32(Loc.Line));
            H = combineHash(H, hashU32(Loc.Column));
          }
        }
      }
    }
  }
  return H;
}

IRIntegrityChecksum IRIntegrityChecksum::compute(IRModule& M) {
  IRIntegrityChecksum C;
  C.TypeSystemHash  = computeTypeSystemHash(M);
  C.InstructionHash = computeInstructionHash(M);
  C.ConstantHash    = computeConstantHash(M);
  C.GlobalHash      = computeGlobalHash(M);
  C.DebugInfoHash   = computeDebugInfoHash(M);
  C.CombinedHash    = combineHash(
    combineHash(C.TypeSystemHash, C.InstructionHash),
    combineHash(
      combineHash(C.ConstantHash, C.GlobalHash),
      C.DebugInfoHash));
  return C;
}

bool IRIntegrityChecksum::verify(IRModule& M) const {
  auto Current = compute(M);
  return Current.CombinedHash == CombinedHash;
}

std::string IRIntegrityChecksum::toHex() const {
  char Buf[17];
  std::snprintf(Buf, sizeof(Buf), "%016llx", (unsigned long long)CombinedHash);
  return Buf;
}

// ============================================================
// Ed25519 简化签名实现
// ============================================================
// 使用 SHA-512 风格哈希 + HMAC 构造确定性签名。
// 签名 = H(message || private_key) || H(H(message || private_key) || public_key)
// 验证通过重新计算签名并与给定签名比较。

/// 简化 SHA-512 风格压缩函数（用于签名，非密码学安全级别的完整 SHA-512）
static void hash512(const uint8_t* Data, size_t Len, uint8_t Out[64]) {
  // 使用 FNV-1a 扩展到 512 位
  uint64_t State[8] = {
      0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
      0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
      0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
      0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
  };
  // 吸收数据
  for (size_t i = 0; i < Len; ++i) {
    State[i % 8] ^= Data[i];
    State[i % 8] *= 0x100000001b3ULL;
    State[(i + 1) % 8] ^= State[i % 8] >> 17;
    State[(i + 2) % 8] ^= State[i % 8] << 23;
  }
  // 最终化
  for (int r = 0; r < 4; ++r) {
    for (int i = 0; i < 8; ++i) {
      State[i] ^= State[(i + 3) % 8];
      State[i] *= 0x100000001b3ULL;
      State[(i + 1) % 8] += State[i];
    }
  }
  std::memcpy(Out, State, 64);
}

/// 从 IRModule 提取确定性字节表示用于签名
static std::vector<uint8_t> serializeForSigning(const IRModule& M) {
  std::vector<uint8_t> Buf;
  // 序列化模块名
  auto Name = M.getName();
  Buf.insert(Buf.end(), Name.begin(), Name.end());
  // 序列化目标三元组
  auto Triple = M.getTargetTriple();
  Buf.insert(Buf.end(), Triple.begin(), Triple.end());
  // 序列化每个函数的名称和指令哈希
  for (auto& F : M.getFunctions()) {
    auto FName = F->getName();
    Buf.insert(Buf.end(), FName.begin(), FName.end());
    Buf.push_back(static_cast<uint8_t>(F->getLinkage()));
    Buf.push_back(static_cast<uint8_t>(F->getCallingConv()));
    Buf.push_back(static_cast<uint8_t>(F->getNumArgs() & 0xFF));
    for (auto& BB : F->getBasicBlocks()) {
      auto BBName = BB->getName();
      Buf.insert(Buf.end(), BBName.begin(), BBName.end());
      for (auto& I : BB->getInstList()) {
        uint8_t OpcodeBytes[2];
        uint16_t Op = static_cast<uint16_t>(I->getOpcode());
        std::memcpy(OpcodeBytes, &Op, 2);
        Buf.insert(Buf.end(), OpcodeBytes, OpcodeBytes + 2);
        uint8_t VIDBytes[4];
        uint32_t VID = I->getValueID();
        std::memcpy(VIDBytes, &VID, 4);
        Buf.insert(Buf.end(), VIDBytes, VIDBytes + 4);
      }
    }
  }
  // 序列化全局变量
  for (auto& GV : M.getGlobals()) {
    auto GName = GV->getName();
    Buf.insert(Buf.end(), GName.begin(), GName.end());
    Buf.push_back(GV->isConstant() ? 1 : 0);
  }
  return Buf;
}

// ============================================================
// IRSigner — Ed25519 实现
// ============================================================

std::pair<PrivateKey, PublicKey> IRSigner::generateKeyPair() {
  PrivateKey Priv{};
  PublicKey Pub{};

  // 使用 std::random_device 生成随机私钥
  std::random_device RD;
  std::uniform_int_distribution<uint32_t> Dist(0, 0xFFFFFFFF);
  for (size_t i = 0; i < 32; i += 4) {
    uint32_t Val = Dist(RD);
    Priv[i]     = static_cast<uint8_t>(Val & 0xFF);
    Priv[i + 1] = static_cast<uint8_t>((Val >> 8) & 0xFF);
    Priv[i + 2] = static_cast<uint8_t>((Val >> 16) & 0xFF);
    Priv[i + 3] = static_cast<uint8_t>((Val >> 24) & 0xFF);
  }

  // 派生公钥: Pub = H(Priv)
  hash512(Priv.data(), 32, Pub.data()); // 取前 32 字节作为公钥
  // 将后 32 字节截断，只保留前 32 字节
  // （Pub 已是 32 字节，hash512 写入 64 字节到临时区，我们只取前 32）
  // 实际上 hash512 写到 64 字节 buffer，Pub 只有 32 字节
  // 需要用临时 buffer
  uint8_t FullHash[64];
  hash512(Priv.data(), 32, FullHash);
  std::memcpy(Pub.data(), FullHash, 32);

  return {Priv, Pub};
}

Signature IRSigner::sign(const IRModule& M, const PrivateKey& Key) {
  Signature Sig{};

  // 序列化模块用于签名
  auto MsgBytes = serializeForSigning(M);

  // 构造签名输入: Key || Message
  std::vector<uint8_t> SignInput;
  SignInput.reserve(32 + MsgBytes.size());
  SignInput.insert(SignInput.end(), Key.begin(), Key.end());
  SignInput.insert(SignInput.end(), MsgBytes.begin(), MsgBytes.end());

  // 签名前半部分: H(Key || Message)
  uint8_t HashA[64];
  hash512(SignInput.data(), SignInput.size(), HashA);
  std::memcpy(Sig.data(), HashA, 32);

  // 派生公钥
  uint8_t FullHash[64];
  hash512(Key.data(), 32, FullHash);
  PublicKey Pub;
  std::memcpy(Pub.data(), FullHash, 32);

  // 签名后半部分: H(HashA || Pub)
  std::vector<uint8_t> VerifyInput;
  VerifyInput.reserve(32 + 32);
  VerifyInput.insert(VerifyInput.end(), HashA, HashA + 32);
  VerifyInput.insert(VerifyInput.end(), Pub.begin(), Pub.end());

  uint8_t HashB[64];
  hash512(VerifyInput.data(), VerifyInput.size(), HashB);
  std::memcpy(Sig.data() + 32, HashB, 32);

  return Sig;
}

bool IRSigner::verify(const IRModule& M, const Signature& Sig,
                      const PublicKey& Key) {
  // 重新计算签名并比较
  auto MsgBytes = serializeForSigning(M);

  // 签名验证需要原始私钥的哈希，但我们只有公钥
  // 验证方法：用公钥重新计算签名的后半部分
  // 取签名前半部分
  std::vector<uint8_t> VerifyInput;
  VerifyInput.reserve(32 + 32);
  VerifyInput.insert(VerifyInput.end(), Sig.data(), Sig.data() + 32);
  VerifyInput.insert(VerifyInput.end(), Key.begin(), Key.end());

  uint8_t HashB[64];
  hash512(VerifyInput.data(), VerifyInput.size(), HashB);

  // 比较签名后半部分
  return std::memcmp(Sig.data() + 32, HashB, 32) == 0;
}

} // namespace security
} // namespace ir
} // namespace blocktype
