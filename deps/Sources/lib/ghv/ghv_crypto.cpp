/**
 * ghv_crypto.cpp - AEAD encryption implementation using OpenSSL 3.x EVP API
 *
 * AES-256-GCM with:
 *   - 12-byte nonce (counter-based, unique per packet)
 *   - 16-byte authentication tag
 *   - AAD = first 10 bytes of header (Magic + Length + SeqNo)
 *   - In-place decryption for zero-copy performance
 *
 * Uses OpenSSL 3.x Fetch API:
 *   EVP_CIPHER_fetch() + EVP_EncryptInit_ex2() (no deprecated legacy API)
 */
#include "ghv_crypto.h"

#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>

#include <cstring>
#include <cstdio>

// M2: EvpCtxGuard 已移除 — EVP_CIPHER_CTX 现在缓存在 CryptoProtocol 内部
//     enc_ctx_ / dec_ctx_ 构造时创建、析构时释放，每次使用前 reset

// ============================================================
// Construction / Destruction
// ============================================================

CryptoProtocol::CryptoProtocol() {
    memset(session_key_, 0, sizeof(session_key_));
    // Fetch the cipher once and cache it for the lifetime of this object.
    // EVP_CIPHER_fetch returns a reference-counted object, freed by EVP_CIPHER_free.
    cipher_ = EVP_CIPHER_fetch(nullptr, "AES-256-GCM", nullptr);
    if (!cipher_) {
        fprintf(stderr, "[ghv_crypto] FATAL: EVP_CIPHER_fetch(AES-256-GCM) failed!\n");
    }
    // M2: 预创建加解密上下文，避免每次 EncryptAndSeal/DecryptAndVerify 堆分配
    enc_ctx_ = EVP_CIPHER_CTX_new();
    dec_ctx_ = EVP_CIPHER_CTX_new();
    if (!enc_ctx_ || !dec_ctx_) {
        fprintf(stderr, "[ghv_crypto] FATAL: EVP_CIPHER_CTX_new() failed!\n");
    }
}

CryptoProtocol::~CryptoProtocol() {
    ClearKey();
    // M2: 释放缓存的加解密上下文
    if (enc_ctx_) { EVP_CIPHER_CTX_free(enc_ctx_); enc_ctx_ = nullptr; }
    if (dec_ctx_) { EVP_CIPHER_CTX_free(dec_ctx_); dec_ctx_ = nullptr; }
    if (cipher_) {
        EVP_CIPHER_free(cipher_);
        cipher_ = nullptr;
    }
}

void CryptoProtocol::ClearKey() {
    OPENSSL_cleanse(session_key_, sizeof(session_key_));
    key_set_ = false;
    nonce_counter_ = 0;
    // Bug-1 FIX: 重置缓存 CTX，清除旧密钥的 EVP 内部状态
    if (enc_ctx_) EVP_CIPHER_CTX_reset(enc_ctx_);
    if (dec_ctx_) EVP_CIPHER_CTX_reset(dec_ctx_);
}

void CryptoProtocol::SetSessionKey(const uint8_t* key, size_t len) {
    if (!cipher_) {
        fprintf(stderr, "[ghv_crypto] SetSessionKey: cipher not available (OpenSSL init failed)\n");
        return;
    }
    if (!key || len != GHV_KEY_SIZE) {
        fprintf(stderr, "[ghv_crypto] SetSessionKey: invalid key length %zu (expected %u)\n",
                len, static_cast<unsigned>(GHV_KEY_SIZE));
        return;
    }
    
    // 清理旧状态
    ClearKey();
    
    memcpy(session_key_, key, GHV_KEY_SIZE);
    key_set_ = true;
    
    // 不再做 M7 风格的一次性 Key Schedule 预加载。
    // 每次 EncryptAndSeal/DecryptAndVerify 使用 same-cipher reinit 模式：
    //   EVP_XxxInit_ex2(ctx, cipher_, session_key_, nonce, NULL)
    // OpenSSL 检测到 cipher == ctx->cipher 时复用 algctx 内存（无堆碎片化），
    // 同时完全重置 GCM 状态机（清除错误态、AAD 缓存、计数器）。
}

// ============================================================
// Nonce generation
// ============================================================

void CryptoProtocol::BuildNonce(uint8_t nonce_out[GHV_NONCE_SIZE], uint32_t seq_no) {
    // Nonce layout (12 bytes):
    //   [counter_hi:4][counter_lo:4][seq_no:4]
    // This ensures uniqueness even if seq_no wraps, as long as
    // nonce_counter_ is monotonically increasing per session key.
    uint64_t counter = ++nonce_counter_;
    uint32_t hi = static_cast<uint32_t>(counter >> 32);
    uint32_t lo = static_cast<uint32_t>(counter & 0xFFFFFFFF);

    nonce_out[0]  = static_cast<uint8_t>(hi >> 24);
    nonce_out[1]  = static_cast<uint8_t>(hi >> 16);
    nonce_out[2]  = static_cast<uint8_t>(hi >> 8);
    nonce_out[3]  = static_cast<uint8_t>(hi);
    nonce_out[4]  = static_cast<uint8_t>(lo >> 24);
    nonce_out[5]  = static_cast<uint8_t>(lo >> 16);
    nonce_out[6]  = static_cast<uint8_t>(lo >> 8);
    nonce_out[7]  = static_cast<uint8_t>(lo);
    nonce_out[8]  = static_cast<uint8_t>(seq_no >> 24);
    nonce_out[9]  = static_cast<uint8_t>(seq_no >> 16);
    nonce_out[10] = static_cast<uint8_t>(seq_no >> 8);
    nonce_out[11] = static_cast<uint8_t>(seq_no);
}

// ============================================================
// Encrypt and Seal
// ============================================================

bool CryptoProtocol::EncryptAndSeal(const uint8_t* plaintext, size_t plain_len,
                                     uint32_t seq_no,
                                     std::vector<uint8_t>& out_frame) {
    if (!key_set_ || !cipher_ || !enc_ctx_) {
        fprintf(stderr, "[ghv_crypto] EncryptAndSeal: no session key or cipher/ctx not available\n");
        return false;
    }

    // Calculate total frame size
    size_t frame_size = GHV_HEADER_SIZE + plain_len + GHV_TAG_SIZE;
    if (frame_size > GHV_MAX_FRAME_SIZE) {
        fprintf(stderr, "[ghv_crypto] EncryptAndSeal: frame too large (%zu bytes)\n", frame_size);
        return false;
    }

    // Allocate output buffer
    out_frame.resize(frame_size);
    uint8_t* frame = out_frame.data();

    // Build header
    // Length 字段值 = body 大小（不含 Magic+Length 前 6 字节），与 Pack 协议 unpack (length_adjustment=0) 兼容
    NetPacketHeader* hdr = reinterpret_cast<NetPacketHeader*>(frame);
    hdr->SetMagic();
    hdr->SetLength(static_cast<uint32_t>(frame_size - 6));  // body-only length for unpack compat
    hdr->SetSeqNo(seq_no);

    // Generate nonce and write to header
    BuildNonce(hdr->nonce, seq_no);

    // Pointers into frame
    uint8_t* ciphertext_ptr = frame + GHV_HEADER_SIZE;
    uint8_t* tag_ptr        = frame + GHV_HEADER_SIZE + plain_len;

    // Same-cipher reinit: cipher_ == ctx->cipher 时 OpenSSL 复用 algctx 内存，
    // 但完全重置 GCM 状态机（含错误态），然后重做 Key Schedule + 设置 IV。
    // GHV_NONCE_SIZE == 12 == AES-GCM 默认 IV 长度，无需显式 SET_IVLEN。
    EVP_CIPHER_CTX* ctx = enc_ctx_;

    bool success = false;
    do {
        if (EVP_EncryptInit_ex2(ctx, cipher_, session_key_, hdr->nonce, nullptr) != 1)
            break;

        // Feed AAD (first 10 bytes of header: Magic + Length + SeqNo)
        int aad_len = 0;
        if (EVP_EncryptUpdate(ctx, nullptr, &aad_len, frame,
                              static_cast<int>(GHV_AAD_SIZE)) != 1)
            break;

        // Encrypt plaintext -> ciphertext
        int out_len = 0;
        if (plain_len > 0) {
            if (EVP_EncryptUpdate(ctx, ciphertext_ptr, &out_len,
                                  plaintext, static_cast<int>(plain_len)) != 1)
                break;
        }

        // Finalize (no additional output for GCM)
        int final_len = 0;
        if (EVP_EncryptFinal_ex(ctx, ciphertext_ptr + out_len, &final_len) != 1)
            break;

        // Get authentication tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
                                static_cast<int>(GHV_TAG_SIZE), tag_ptr) != 1)
            break;

        success = true;
    } while (false);

    // ctx 由 CryptoProtocol 析构函数统一释放（M2: 复用模式）

    if (!success) {
        fprintf(stderr, "[ghv_crypto] EncryptAndSeal: encryption failed\n");
        out_frame.clear();
    }

    return success;
}

// ============================================================
// Decrypt and Verify (in-place) — C++17: DecryptResult 结构化绑定友好
// ============================================================

bool CryptoProtocol::DecryptAndVerify(uint8_t* frame, size_t frame_len,
                                        DecryptResult& result) {
    if (!key_set_ || !cipher_ || !dec_ctx_) {
        fprintf(stderr, "[ghv_crypto] DecryptAndVerify: no session key or cipher/ctx not available\n");
        return false;
    }

    // Validate minimum frame size
    if (frame_len < GHV_MIN_FRAME_SIZE) {
        fprintf(stderr, "[ghv_crypto] DecryptAndVerify: frame too small (%zu bytes)\n", frame_len);
        return false;
    }

    // Validate max frame size
    if (frame_len > GHV_MAX_FRAME_SIZE) {
        fprintf(stderr, "[ghv_crypto] DecryptAndVerify: frame too large (%zu bytes)\n", frame_len);
        return false;
    }

    // Parse header
    NetPacketHeader* hdr = reinterpret_cast<NetPacketHeader*>(frame);

    // Validate magic
    if (!hdr->ValidateMagic()) {
        fprintf(stderr, "[ghv_crypto] DecryptAndVerify: invalid magic bytes\n");
        return false;
    }

    // Validate length consistency
    // Length 字段值 = body 大小（不含 Magic+Length 前 6 字节）
    uint32_t declared_body_len = hdr->GetLength();
    if (declared_body_len + 6 != frame_len) {
        fprintf(stderr, "[ghv_crypto] DecryptAndVerify: length mismatch (header_body=%u+6, actual=%zu)\n",
                declared_body_len, frame_len);
        return false;
    }

    uint32_t seq_no = hdr->GetSeqNo();

    // Calculate ciphertext length (frame - header - tag)
    size_t ciphertext_len = frame_len - GHV_HEADER_SIZE - GHV_TAG_SIZE;

    // Pointers
    uint8_t* ciphertext_ptr = frame + GHV_HEADER_SIZE;
    uint8_t* tag_ptr        = frame + frame_len - GHV_TAG_SIZE;

    // Copy tag to stack buffer — EVP_CTRL_GCM_SET_TAG may modify the buffer
    // on some platforms, and tag_ptr overlaps with the frame we're decrypting.
    uint8_t tag_copy[GHV_TAG_SIZE];
    memcpy(tag_copy, tag_ptr, GHV_TAG_SIZE);

    // Same-cipher reinit: cipher_ == ctx->cipher 时 OpenSSL 复用 algctx 内存，
    // 但完全重置 GCM 状态机（含错误态），然后重做 Key Schedule + 设置 IV。
    EVP_CIPHER_CTX* ctx = dec_ctx_;

    bool success = false;
    do {
        if (EVP_DecryptInit_ex2(ctx, cipher_, session_key_, hdr->nonce, nullptr) != 1)
            break;

        // Feed AAD
        int aad_len = 0;
        if (EVP_DecryptUpdate(ctx, nullptr, &aad_len, frame,
                              static_cast<int>(GHV_AAD_SIZE)) != 1)
            break;

        // Decrypt ciphertext in-place (overwrite ciphertext with plaintext)
        int out_len = 0;
        if (ciphertext_len > 0) {
            if (EVP_DecryptUpdate(ctx, ciphertext_ptr, &out_len,
                                  ciphertext_ptr, static_cast<int>(ciphertext_len)) != 1)
                break;
        }

        // Set expected tag for verification (use stack copy to avoid aliasing)
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
                                static_cast<int>(GHV_TAG_SIZE), tag_copy) != 1)
            break;

        // Finalize and verify MAC
        // If the tag doesn't match, EVP_DecryptFinal_ex returns 0.
        int final_len = 0;
        if (EVP_DecryptFinal_ex(ctx, ciphertext_ptr + out_len, &final_len) != 1) {
            // MAC verification FAILED — packet was tampered with
            fprintf(stderr, "[ghv_crypto] DecryptAndVerify: MAC verification FAILED (tampered packet)\n");
            break;
        }

        success = true;
    } while (false);

    // ctx 由 CryptoProtocol 析构函数统一释放（M2: 复用模式）

    // Secure-erase the tag copy from stack
    OPENSSL_cleanse(tag_copy, sizeof(tag_copy));

    if (!success) {
        return false;
    }

    // 返回就地解密结果（plaintext 指向 frame 内部，零拷贝）
    result.seq_no = seq_no;
    result.plaintext = ciphertext_ptr;
    result.plain_len = ciphertext_len;
    return true;
}

// ============================================================
// KeyExchange — X25519 ECDH + HKDF-SHA256
// ============================================================

KeyExchange::~KeyExchange() {
    if (local_key_) {
        EVP_PKEY_free(local_key_);
        local_key_ = nullptr;
    }
}

bool KeyExchange::GenerateKeyPair(uint8_t pub_out[32]) {
    // 释放旧密钥对（支持多次调用）
    if (local_key_) {
        EVP_PKEY_free(local_key_);
        local_key_ = nullptr;
    }

    // 生成 X25519 密钥对
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (!ctx) {
        fprintf(stderr, "[ghv_crypto] KeyExchange: EVP_PKEY_CTX_new_id failed\n");
        return false;
    }

    bool ok = false;
    do {
        if (EVP_PKEY_keygen_init(ctx) <= 0) break;
        if (EVP_PKEY_keygen(ctx, &local_key_) <= 0) break;

        // 导出公钥（32 字节）
        size_t pub_len = 32;
        if (EVP_PKEY_get_raw_public_key(local_key_, pub_out, &pub_len) <= 0) break;
        if (pub_len != 32) break;

        ok = true;
    } while (false);

    EVP_PKEY_CTX_free(ctx);

    if (!ok) {
        fprintf(stderr, "[ghv_crypto] KeyExchange: GenerateKeyPair failed\n");
        if (local_key_) { EVP_PKEY_free(local_key_); local_key_ = nullptr; }
    }
    return ok;
}

bool KeyExchange::DeriveSessionKey(const uint8_t peer_pub[32],
                                    uint8_t session_key_out[32]) {
    if (!local_key_) {
        fprintf(stderr, "[ghv_crypto] KeyExchange: no local key pair (call GenerateKeyPair first)\n");
        return false;
    }

    // 1. 从 raw 公钥构建对方 EVP_PKEY
    EVP_PKEY* peer_key = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_X25519, nullptr, peer_pub, 32);
    if (!peer_key) {
        fprintf(stderr, "[ghv_crypto] KeyExchange: invalid peer public key\n");
        return false;
    }

    // 2. X25519 ECDH → 32 字节 raw shared secret
    uint8_t shared_secret[32];
    size_t shared_len = sizeof(shared_secret);
    bool ok = false;

    EVP_PKEY_CTX* derive_ctx = EVP_PKEY_CTX_new(local_key_, nullptr);
    do {
        if (!derive_ctx) break;
        if (EVP_PKEY_derive_init(derive_ctx) <= 0) break;
        if (EVP_PKEY_derive_set_peer(derive_ctx, peer_key) <= 0) break;
        if (EVP_PKEY_derive(derive_ctx, shared_secret, &shared_len) <= 0) break;
        if (shared_len != 32) break;
        ok = true;
    } while (false);

    if (derive_ctx) EVP_PKEY_CTX_free(derive_ctx);
    EVP_PKEY_free(peer_key);

    if (!ok) {
        OPENSSL_cleanse(shared_secret, sizeof(shared_secret));
        fprintf(stderr, "[ghv_crypto] KeyExchange: ECDH derive failed\n");
        return false;
    }

    // 3. HKDF-SHA256: shared_secret → session_key (32 bytes)
    static const uint8_t hkdf_salt[] = "GHV_GAME_2026";
    static const uint8_t hkdf_info[] = "aes256gcm_session";

    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (!kdf) {
        OPENSSL_cleanse(shared_secret, sizeof(shared_secret));
        fprintf(stderr, "[ghv_crypto] KeyExchange: EVP_KDF_fetch(HKDF) failed\n");
        return false;
    }

    EVP_KDF_CTX* kdf_ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kdf_ctx) {
        OPENSSL_cleanse(shared_secret, sizeof(shared_secret));
        return false;
    }

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(
            OSSL_KDF_PARAM_DIGEST, const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_octet_string(
            OSSL_KDF_PARAM_KEY, shared_secret, shared_len),
        OSSL_PARAM_construct_octet_string(
            OSSL_KDF_PARAM_SALT,
            const_cast<uint8_t*>(hkdf_salt), sizeof(hkdf_salt) - 1),
        OSSL_PARAM_construct_octet_string(
            OSSL_KDF_PARAM_INFO,
            const_cast<uint8_t*>(hkdf_info), sizeof(hkdf_info) - 1),
        OSSL_PARAM_construct_end()
    };

    ok = (EVP_KDF_derive(kdf_ctx, session_key_out, 32, params) > 0);

    EVP_KDF_CTX_free(kdf_ctx);
    // 安全擦除 raw shared secret
    OPENSSL_cleanse(shared_secret, sizeof(shared_secret));

    if (!ok) {
        fprintf(stderr, "[ghv_crypto] KeyExchange: HKDF derive failed\n");
    }
    return ok;
}

// ============================================================
// Ed25519 Sign / Verify (防 MITM)
// ============================================================

bool KeyExchange::Ed25519GenerateKeyPair(uint8_t priv_key_out[32], uint8_t pub_key_out[32]) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!ctx) return false;
    EVP_PKEY* pkey = nullptr;
    bool ok = false;
    do {
        if (EVP_PKEY_keygen_init(ctx) <= 0) break;
        if (EVP_PKEY_keygen(ctx, &pkey) <= 0) break;

        size_t priv_len = 32, pub_len = 32;
        if (EVP_PKEY_get_raw_private_key(pkey, priv_key_out, &priv_len) <= 0) break;
        if (EVP_PKEY_get_raw_public_key(pkey, pub_key_out, &pub_len) <= 0) break;

        if (priv_len == 32 && pub_len == 32) ok = true;
    } while (false);

    if (pkey) EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return ok;
}

bool KeyExchange::Ed25519Sign(const uint8_t* msg, size_t msg_len,
                              const uint8_t priv_key[32], uint8_t signature_out[64]) {
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, priv_key, 32);
    if (!pkey) {
        fprintf(stderr, "[ghv_crypto] Ed25519Sign: invaild private key\n");
        return false;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    bool success = false;
    do {
        if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey) <= 0) break;
        size_t siglen = 64;
        // Ed25519 uses EVP_DigestSign as a one-shot without update/final
        if (EVP_DigestSign(ctx, signature_out, &siglen, msg, msg_len) <= 0) break;
        if (siglen != 64) break;
        success = true;
    } while (false);

    if (!success) {
        fprintf(stderr, "[ghv_crypto] Ed25519Sign: signature failed\n");
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return success;
}

bool KeyExchange::Ed25519Verify(const uint8_t* msg, size_t msg_len,
                                const uint8_t signature[64], const uint8_t pub_key[32]) {
    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, pub_key, 32);
    if (!pkey) {
        fprintf(stderr, "[ghv_crypto] Ed25519Verify: invalid public key format\n");
        return false;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    bool success = false;
    do {
        if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) <= 0) break;
        if (EVP_DigestVerify(ctx, signature, 64, msg, msg_len) <= 0) break;
        success = true;
    } while (false);

    if (!success) {
        fprintf(stderr, "[ghv_crypto] Ed25519Verify: signature verification failed\n");
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return success;
}

