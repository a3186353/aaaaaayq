/**
 * ghv_net_protocol.h - Secure game communication protocol definitions
 *
 * Binary frame format (Big-Endian / Network Byte Order):
 * ┌──────────┬──────────┬──────────┬──────────────┬────────────────────────┐
 * │ Magic(2) │ Length(4)│ SeqNo(4) │  Nonce(12)   │  Ciphertext + Tag(16)  │
 * │  0x4747  │  uint32  │  uint32  │   12 bytes   │   variable + 16 bytes  │
 * └──────────┴──────────┴──────────┴──────────────┴────────────────────────┘
 *   Header (22 bytes, plaintext)        Encrypted Body
 *
 * AAD = Magic(2) + Length(4) + SeqNo(4) = first 10 bytes
 */
#pragma once
// Windows headers MUST come before <cstdint> to avoid uint32_t redefinition
// conflicts with MSVC's C++17 STL internal headers (type_traits, xutility)
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <cstdint>
#include <cstring>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

// ============================================================
// Constants
// ============================================================

constexpr uint8_t  GHV_MAGIC_BYTE_0   = 0x47;  // 'G'
constexpr uint8_t  GHV_MAGIC_BYTE_1   = 0x47;  // 'G'
constexpr uint32_t GHV_HEADER_SIZE    = 22;     // Magic(2) + Length(4) + SeqNo(4) + Nonce(12)
constexpr uint32_t GHV_AAD_SIZE       = 10;     // Magic(2) + Length(4) + SeqNo(4)
constexpr uint32_t GHV_TAG_SIZE       = 16;     // AES-GCM authentication tag
constexpr uint32_t GHV_NONCE_SIZE     = 12;     // AES-GCM nonce/IV
constexpr uint32_t GHV_KEY_SIZE       = 32;     // AES-256 key size
constexpr uint32_t GHV_MIN_FRAME_SIZE = GHV_HEADER_SIZE + GHV_TAG_SIZE; // 38 bytes minimum
constexpr uint32_t GHV_MAX_FRAME_SIZE = 1024 * 1024; // 1MB max frame

// ============================================================
// Packed binary header (no padding, cross-platform)
// ============================================================

#pragma pack(push, 1)
struct NetPacketHeader {
    uint8_t  magic[2];       // {0x47, 0x47}
    uint8_t  length_le[4];   // Little-Endian: total frame bytes (用字节数组避免 ARM64 未对齐访问)
    uint8_t  seq_no_le[4];   // Little-Endian: monotonically increasing sequence number (同上)
    uint8_t  nonce[12];      // AEAD nonce (unique per packet)

    void SetMagic() {
        magic[0] = GHV_MAGIC_BYTE_0;
        magic[1] = GHV_MAGIC_BYTE_1;
    }

    bool ValidateMagic() const {
        return magic[0] == GHV_MAGIC_BYTE_0 && magic[1] == GHV_MAGIC_BYTE_1;
    }

    // 使用 memcpy 做安全的非对齐读写（Little-Endian 本地序，与 Pack 协议 unpack 一致）
    void SetLength(uint32_t len) {
        memcpy(length_le, &len, 4);
    }

    uint32_t GetLength() const {
        uint32_t val;
        memcpy(&val, length_le, 4);
        return val;
    }

    void SetSeqNo(uint32_t seq) {
        memcpy(seq_no_le, &seq, 4);
    }

    uint32_t GetSeqNo() const {
        uint32_t val;
        memcpy(&val, seq_no_le, 4);
        return val;
    }
};
#pragma pack(pop)

static_assert(sizeof(NetPacketHeader) == GHV_HEADER_SIZE,
              "NetPacketHeader must be exactly 22 bytes with no padding");
static_assert(alignof(NetPacketHeader) == 1,
              "NetPacketHeader must have alignment of 1 for safe reinterpret_cast from byte buffer");

// ============================================================
// Anti-Replay Sliding Window (64-bit bitmap, window size 64)
//
// Tracks received sequence numbers. Accepts a SeqNo if:
//   1. It is ahead of the window (new territory)
//   2. It is within the window and hasn't been seen before
// Rejects if:
//   1. It is behind the window (too old)
//   2. It is within the window but already seen (duplicate)
// ============================================================

class AntiReplayWindow {
public:
    // Returns true if seq_no is valid (not a replay), false otherwise.
    bool Accept(uint32_t seq_no) {
        if (seq_no == 0) {
            // SeqNo 0 is reserved / invalid
            return false;
        }

        if (max_seq_ == 0) {
            // First packet ever received
            max_seq_ = seq_no;
            bitmap_ = 1;
            return true;
        }

        if (seq_no > max_seq_) {
            // Ahead of window: shift bitmap and accept
            uint32_t diff = seq_no - max_seq_;
            if (diff >= 64) {
                bitmap_ = 1; // entire window is new
            } else {
                bitmap_ <<= diff;
                bitmap_ |= 1;
            }
            max_seq_ = seq_no;
            return true;
        }

        // seq_no <= max_seq_: check if within window
        uint32_t diff = max_seq_ - seq_no;
        if (diff >= 64) {
            // Too old, behind the window
            return false;
        }

        uint64_t bit = uint64_t(1) << diff;
        if (bitmap_ & bit) {
            // Already seen (duplicate / replay)
            return false;
        }

        // Within window, not seen before
        bitmap_ |= bit;
        return true;
    }

    void Reset() {
        max_seq_ = 0;
        bitmap_ = 0;
    }

private:
    uint32_t max_seq_ = 0;
    uint64_t bitmap_  = 0;
};

// ============================================================
// Connection Security State Machine (anti-downgrade)
//
// Prevents plaintext injection after encryption is negotiated.
//   Plaintext   → legacy mode, no encryption expected
//   Handshaking → key exchange in progress, only allow handshake msgs
//   Encrypted   → fully encrypted, reject all non-encrypted frames
// ============================================================

enum class ConnectionSecurityState : uint8_t {
    Plaintext,     // Legacy plaintext mode (explicit opt-in)
    Handshaking,   // Key exchange in progress, only handshake messages allowed
    Encrypted,     // Encryption active, reject all non-encrypted frames
};
