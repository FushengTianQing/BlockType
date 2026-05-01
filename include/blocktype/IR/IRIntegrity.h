#ifndef BLOCKTYPE_IR_IRINTEGRITY_H
#define BLOCKTYPE_IR_IRINTEGRITY_H

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <utility>

namespace blocktype {
namespace ir {

class IRModule;

namespace security {

// ============================================================
// 可重现构建常量
// ============================================================

/// SOURCE_DATE_EPOCH 默认值（1970-01-01 00:00:00 UTC）
constexpr uint64_t ReproducibleTimestamp = 0;

/// 确定性随机种子
constexpr uint64_t DeterministicSeed = 0x42;

/// 确定性 ValueID/InstructionID 起始值
constexpr unsigned DeterministicValueIDStart = 1;

/// 确定性临时文件名前缀
constexpr const char* DeterministicTempPrefix = "bt_tmp_";

/// 固定 DWARF 生产者字符串
constexpr const char* FixedProducerString = "BlockType";

// ============================================================
// IRIntegrityChecksum — IR 完整性校验和
// ============================================================

/// IR 模块完整性校验和。对 IRModule 的各子系统分别计算哈希，
/// 最终组合为 CombinedHash。用于检测 IR 数据是否被意外修改。
struct IRIntegrityChecksum {
  uint64_t TypeSystemHash = 0;
  uint64_t InstructionHash = 0;
  uint64_t ConstantHash = 0;
  uint64_t GlobalHash = 0;
  uint64_t DebugInfoHash = 0;
  uint64_t CombinedHash = 0;

  /// 计算 IRModule 的完整性校验和
  static IRIntegrityChecksum compute(IRModule& M);

  /// 验证 IRModule 的校验和是否与当前值匹配
  bool verify(IRModule& M) const;

  /// 转为 hex 字符串（用于调试/日志）
  std::string toHex() const;

  bool operator==(const IRIntegrityChecksum& O) const {
    return CombinedHash == O.CombinedHash;
  }
  bool operator!=(const IRIntegrityChecksum& O) const { return !(*this == O); }
};

// ============================================================
// IRSigner — IR 签名（Ed25519 实现）
// ============================================================

using PrivateKey = std::array<uint8_t, 32>;
using PublicKey  = std::array<uint8_t, 32>;
using Signature  = std::array<uint8_t, 64>;

/// IR 模块签名器。使用 Ed25519 签名算法，支持密钥生成、签名和验证。
/// 签名附加到序列化输出末尾，验证在 IR 加载时执行。
class IRSigner {
public:
  /// 生成 Ed25519 密钥对。
  static std::pair<PrivateKey, PublicKey> generateKeyPair();

  /// 对 IRModule 签名（Ed25519 真实实现）。
  static Signature sign(const IRModule& M, const PrivateKey& Key);

  /// 验证 IRModule 签名（Ed25519 真实验证）。
  static bool verify(const IRModule& M, const Signature& Sig,
                     const PublicKey& Key);
};

} // namespace security
} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRINTEGRITY_H
