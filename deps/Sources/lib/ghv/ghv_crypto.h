/**
 * ghv_crypto.h - AEAD encryption protocol for game communication
 *
 * Uses AES-256-GCM via OpenSSL EVP API.
 * Provides EncryptAndSeal / DecryptAndVerify with:
 *   - In-place decryption (zero-copy)
 *   - AAD authentication (header integrity)
 *   - Secure key memory management (OPENSSL_cleanse on destruction)
 */
#pragma once

#include "ghv_net_protocol.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>  // memset

// ============================================================
// GHV_NO_CRYPTO 兼容层
// 当未交叉编译 OpenSSL 时（如 iOS 首次构建），定义 GHV_NO_CRYPTO=1
// 提供 OPENSSL_cleanse 回退 + stub 类，使 tcp_client/server 可编译
// ============================================================
#ifdef GHV_NO_CRYPTO

// 安全内存清零的回退实现（volatile 防止编译器优化掉）
#ifndef OPENSSL_cleanse
inline void OPENSSL_cleanse(void* ptr, size_t len) {
    volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
    while (len--) *p++ = 0;
}
#endif

// Stub KeyExchange — 所有操作返回 false
class KeyExchange {
public:
    KeyExchange() = default;
    ~KeyExchange() = default;
    KeyExchange(const KeyExchange&) = delete;
    KeyExchange& operator=(const KeyExchange&) = delete;
    bool GenerateKeyPair(uint8_t[32]) { return false; }
    bool DeriveSessionKey(const uint8_t[32], uint8_t[32]) { return false; }
    bool HasKeyPair() const { return false; }

    static bool Ed25519Sign(const uint8_t*, size_t, const uint8_t[32], uint8_t[64]) { return false; }
    static bool Ed25519Verify(const uint8_t*, size_t, const uint8_t[64], const uint8_t[32]) { return false; }
};

// Stub CryptoProtocol — 所有加解密操作返回 false
class CryptoProtocol {
public:
    CryptoProtocol() { memset(session_key_, 0, sizeof(session_key_)); }
    ~CryptoProtocol() = default;
    CryptoProtocol(const CryptoProtocol&) = delete;
    CryptoProtocol& operator=(const CryptoProtocol&) = delete;
    CryptoProtocol(CryptoProtocol&&) = delete;
    CryptoProtocol& operator=(CryptoProtocol&&) = delete;

    void SetSessionKey(const uint8_t*, size_t) { key_set_ = true; }
    void ClearKey() { key_set_ = false; memset(session_key_, 0, sizeof(session_key_)); }
    bool IsKeySet() const { return key_set_; }

    bool EncryptAndSeal(const uint8_t*, size_t, uint32_t, std::vector<uint8_t>&) { return false; }

    struct DecryptResult {
        uint32_t       seq_no;
        const uint8_t* plaintext;
        size_t         plain_len;
    };
    bool DecryptAndVerify(uint8_t*, size_t, DecryptResult&) { return false; }

private:
    alignas(16) uint8_t session_key_[GHV_KEY_SIZE]{};
    bool key_set_ = false;
};

#else  // !GHV_NO_CRYPTO — 正常 OpenSSL 实现

// Forward declaration — avoid pulling in <openssl/evp.h> from the header
typedef struct evp_cipher_st EVP_CIPHER;
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;
typedef struct evp_pkey_st EVP_PKEY;

// ============================================================
// KeyExchange — X25519 ECDH + HKDF-SHA256 密钥协商
// ============================================================
class KeyExchange {
public:
    KeyExchange() = default;
    ~KeyExchange();

    KeyExchange(const KeyExchange&) = delete;
    KeyExchange& operator=(const KeyExchange&) = delete;

    /**
     * 生成 X25519 临时密钥对。
     * @param pub_out  输出 32 字节公钥（可明文传输给对方）
     * @return true on success
     */
    bool GenerateKeyPair(uint8_t pub_out[32]);

    /**
     * 用本地私钥 + 对方公钥派生 32 字节 AES-256 会话密钥。
     * 内部：X25519 ECDH → raw shared secret → HKDF-SHA256(salt, info) → session_key
     * raw shared secret 使用后立即 OPENSSL_cleanse。
     *
     * @param peer_pub          对方的 32 字节 X25519 公钥
     * @param session_key_out   输出 32 字节会话密钥
     * @return true on success
     */
    bool DeriveSessionKey(const uint8_t peer_pub[32],
                          uint8_t session_key_out[32]);

    bool HasKeyPair() const { return local_key_ != nullptr; }

    // ============================================================
    // Ed25519 Identity Signature (防 MITM 中间人攻击)
    // ============================================================
    
    // 生成一套 32 字节私钥与 32 字节公钥
    static bool Ed25519GenerateKeyPair(uint8_t priv_key_out[32], uint8_t pub_key_out[32]);

    // 使用 32 字节私钥对消息生成 64 字节签名
    static bool Ed25519Sign(const uint8_t* msg, size_t msg_len,
                            const uint8_t priv_key[32], uint8_t signature_out[64]);

    // 使用 32 字节公钥验证 64 字节签名
    static bool Ed25519Verify(const uint8_t* msg, size_t msg_len,
                              const uint8_t signature[64], const uint8_t pub_key[32]);

private:
    EVP_PKEY* local_key_ = nullptr;
};

class CryptoProtocol {
public:
    CryptoProtocol();
    ~CryptoProtocol();

    // Non-copyable, non-movable (contains sensitive key material)
    CryptoProtocol(const CryptoProtocol&) = delete;
    CryptoProtocol& operator=(const CryptoProtocol&) = delete;
    CryptoProtocol(CryptoProtocol&&) = delete;
    CryptoProtocol& operator=(CryptoProtocol&&) = delete;

    /**
     * Set the symmetric session key (32 bytes for AES-256).
     * Copies key into secure internal storage.
     */
    void SetSessionKey(const uint8_t* key, size_t len);

    /**
     * Clear the session key (secure zeroing).
     */
    void ClearKey();

    bool IsKeySet() const { return key_set_; }

    /**
     * Encrypt plaintext and build a complete network frame.
     *
     * @param plaintext     Input plaintext data
     * @param plain_len     Length of plaintext
     * @param seq_no        Sequence number (host byte order)
     * @param out_frame     Output buffer (resized to fit: header + ciphertext + tag)
     * @return true on success
     *
     * Frame layout in out_frame:
     *   [Header 22B] [Ciphertext plain_len B] [Tag 16B]
     */
    bool EncryptAndSeal(const uint8_t* plaintext, size_t plain_len,
                        uint32_t seq_no,
                        std::vector<uint8_t>& out_frame);

    /**
     * 就地解密结果（C++17 结构化绑定友好）
     * plaintext 指向帧内就地解密后的明文（零拷贝），生命周期 = frame buffer
     */
    struct DecryptResult {
        uint32_t       seq_no;      // 已还原的序列号（主机字节序）
        const uint8_t* plaintext;   // 指向帧内就地解密后的明文
        size_t         plain_len;   // 明文长度
    };

    /**
     * Decrypt and verify a complete network frame IN-PLACE.
     *
     * The ciphertext region of the frame is overwritten with plaintext.
     * On success, result is populated with the decrypted data.
     *
     * @param frame      Pointer to complete frame (must be writable for in-place decrypt)
     * @param frame_len  Total frame length
     * @param result     [out] Filled on success (seq_no, pointer to decrypted data, length)
     * @return true if MAC verification passed and decryption succeeded
     */
    bool DecryptAndVerify(uint8_t* frame, size_t frame_len, DecryptResult& result);

private:
    // Build nonce from counter + seq_no (ensures uniqueness)
    void BuildNonce(uint8_t nonce_out[GHV_NONCE_SIZE], uint32_t seq_no);

    alignas(16) uint8_t session_key_[GHV_KEY_SIZE];
    EVP_CIPHER*  cipher_        = nullptr;  // Cached cipher (fetched once, freed in dtor)
    bool         key_set_       = false;
    uint64_t     nonce_counter_ = 0;  // 发送方专用单调递增 Nonce 计数器
                                       // 接收方不使用此字段（从帧头读取 nonce）
                                       // 每次 SetSessionKey() 重置为 0
    EVP_CIPHER_CTX* enc_ctx_    = nullptr;  // M2: 复用的加密上下文（构造时创建，析构时释放）
    EVP_CIPHER_CTX* dec_ctx_    = nullptr;  // M2: 复用的解密上下文（同上）
};

#endif  // GHV_NO_CRYPTO
