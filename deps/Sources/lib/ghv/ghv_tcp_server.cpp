/**
 * ghv_tcp_server.cpp - libhv TcpServer Lua binding (single-thread refactor)
 * Exports: luaopen_ghv_TcpServer
 *
 * Key changes:
 * - Uses TcpServerEventLoopTmpl (no separate IO thread, 0 worker threads)
 * - Shares global hloop_t, driven by ghv:run()
 * - Callbacks fire directly in main-thread Lua VM
 * - Per-connection AEAD encryption (AES-256-GCM) with anti-replay
 */
#include "ghv_common.h"
#include "ghv_crypto.h"
#include "ghv_net_protocol.h"
#ifndef GHV_NO_CRYPTO
#include <openssl/crypto.h>  // OPENSSL_cleanse
#endif
#include "TcpServer.h"

#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h>
#endif

#define GHV_TCP_SERVER_META "GHV_TcpServer"

// Per-connection encryption state, stored in channel->context()
struct SessionCrypto {
    CryptoProtocol   crypto;
    AntiReplayWindow replay_window;
    KeyExchange      kex;           // ECDH 密钥协商（逐连接独立）
    uint32_t         send_seq = 0;
    ConnectionSecurityState state = ConnectionSecurityState::Plaintext;
    std::vector<uint8_t> send_buf;
    std::vector<uint8_t> recv_buf;  // 加密帧接收缓冲（手动循环拆帧）
};

// 安全事件枚举 — 用于通知 Lua 状态机具体的安全威胁类型 (M1)
enum GHV_SecurityEvent : int {
    GHV_SEC_MAC_FAILED    = 1,  // MAC 验证失败（数据被篡改）
    GHV_SEC_REPLAY        = 2,  // 重放攻击检测
    GHV_SEC_FRAME_INVALID = 3,  // 帧格式非法
};

struct LuaTcpServer {
    hv::TcpServerEventLoopTmpl<>* server;
    lua_State*     L;
    int            self_ref;

    std::unique_ptr<unpack_setting_t> unpack;
    bool           global_encryption_ = false; // If true, new connections auto-enable encryption

    LuaTcpServer(lua_State* _L)
        : L(_L), self_ref(LUA_NOREF)
    {
        auto evloop = ghv_shared_event_loop();
        server = new hv::TcpServerEventLoopTmpl<>(evloop);
        // 0 worker threads = all connections handled on acceptor_loop (main thread)
        server->setThreadNum(0);
    }

    ~LuaTcpServer() {
        if (server) {
            server->stop(false);
            delete server;
            server = nullptr;
        }
        // unpack 由 unique_ptr 自动释放
    }
};

static LuaTcpServer* check_tcp_server(lua_State* L) {
    return *static_cast<LuaTcpServer**>(luaL_checkudata(L, 1, GHV_TCP_SERVER_META));
}

// Forward declaration (defined below release_server_ref)
static bool push_server_userdata(lua_State* L, LuaTcpServer* self);

// 辅助函数：通知 Lua 安全事件后关闭连接 (M1)
// 回合制游戏中，Lua 层必须明确感知安全事件类型以执行正确的回退逻辑
static void notify_security_and_close(lua_State* L, LuaTcpServer* self,
                                       const hv::SocketChannelPtr& channel,
                                       GHV_SecurityEvent event, uint32_t seq_no) {
    if (push_server_userdata(L, self)) {
        int ud_idx = lua_gettop(L);
        if (ghv_get_lua_ref(L, ud_idx, "on_security")) {
            lua_pushinteger(L, channel->id());
            lua_pushinteger(L, event);
            lua_pushinteger(L, seq_no);
            if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
                fprintf(stderr, "[ghv] security callback error: %s\n",
                        lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    channel->close();
}

// Release registry reference so GC can collect us
static void release_self_ref(LuaTcpServer* self) {
    if (self->self_ref != LUA_NOREF) {
        luaL_unref(self->L, LUA_REGISTRYINDEX, self->self_ref);
        self->self_ref = LUA_NOREF;
    }
}

static bool push_server_userdata(lua_State* L, LuaTcpServer* self) {
    if (self->self_ref == LUA_NOREF) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->self_ref);
    return true;
}

// hv:start(ip, port)
static int l_tcp_server_start(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    const char* ip = luaL_checkstring(L, 2);
    int port = static_cast<int>(luaL_checkinteger(L, 3));

    int listenfd = self->server->createsocket(port, ip);
    if (listenfd < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }

    // Save registry reference
    if (self->self_ref == LUA_NOREF) {
        lua_pushvalue(L, 1);
        self->self_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    // Setup onConnection - runs DIRECTLY in main thread
    self->server->onConnection = [self](const hv::SocketChannelPtr& channel) {
        lua_State* L = self->L;
        uint32_t id = channel->id();

        if (channel->isConnected()) {
            // === Socket optimization for game RPC ===
            int fd = channel->fd();
            // 1. Disable Nagle algorithm for low-latency RPC
            int flag = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
            // 2. Increase kernel send/recv buffers (256KB each)
            //    Default is ~8-64KB; larger buffers prevent kernel-level backpressure
            //    when server sends many NPC data packets in quick succession.
            int bufsize = 256 * 1024;  // 256KB
            setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufsize), sizeof(bufsize));
            setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufsize), sizeof(bufsize));
            // 3. Enable TCP keepalive (detect dead connections)
            setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&flag), sizeof(flag));
            if (push_server_userdata(L, self)) {
                int ud_idx = lua_gettop(L);
                if (ghv_get_lua_ref(L, ud_idx, "on_connect")) {
                    lua_pushinteger(L, id);
                    lua_pushstring(L, channel->peeraddr().c_str());
                    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                        fprintf(stderr, "[ghv] server connect callback error: %s\n", lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
        } else {
            // === Connection closed: clean up per-connection crypto state ===
            // C3 FIX: unique_ptr 接管裸指针，析构时自动调用 OPENSSL_cleanse
            std::unique_ptr<SessionCrypto> session(
                static_cast<SessionCrypto*>(channel->context()));
            channel->setContext(nullptr);
            // session 在此作用域结束时自动析构

            if (push_server_userdata(L, self)) {
                int ud_idx = lua_gettop(L);
                if (ghv_get_lua_ref(L, ud_idx, "on_close")) {
                    lua_pushinteger(L, id);
                    lua_pushinteger(L, channel->error());
                    lua_pushstring(L, "");
                    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
                        fprintf(stderr, "[ghv] server close callback error: %s\n", lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
        }
    };

    // Setup onMessage — with optional per-connection AEAD decryption
    self->server->onMessage = [self](const hv::SocketChannelPtr& channel, hv::Buffer* buf) {
        lua_State* L = self->L;

        // Check per-connection encryption state
        auto* session = static_cast<SessionCrypto*>(channel->context());
        if (session && session->state == ConnectionSecurityState::Encrypted
                && session->crypto.IsKeySet()) {
            // --- Encrypted mode: 手动循环拆帧 ---
            // libhv unpack 在 0-worker-thread 模式下可能不可靠，
            // buf 可能包含多个加密帧的粘包数据。按 Length 字段循环拆分。
            auto& rb = session->recv_buf;
            const uint8_t* incoming = static_cast<const uint8_t*>(buf->data());
            size_t incoming_len = buf->size();
            rb.insert(rb.end(), incoming, incoming + incoming_len);

            // P3-1: 基于偏移量遍历，循环结束后批量 erase（单次 memmove）
            size_t consumed = 0;
            while (rb.size() - consumed >= 6) {  // 至少需要 Magic(2) + Length(4) 才能解析
                uint8_t* ptr = rb.data() + consumed;

                // P3-5: 快速校验 Magic 字节，非法数据立即失败
                if (ptr[0] != GHV_MAGIC_BYTE_0 || ptr[1] != GHV_MAGIC_BYTE_1) {
                    fprintf(stderr, "[ghv] SECURITY: server invalid magic in recv buffer "
                                    "for conn %u, kicking\n", channel->id());
                    rb.clear();
                    notify_security_and_close(L, self, channel, GHV_SEC_FRAME_INVALID, 0);
                    return;
                }

                // 读取 Length 字段（LE, offset 2, 4 bytes）
                uint32_t body_len;
                memcpy(&body_len, ptr + 2, 4);
                size_t frame_size = static_cast<size_t>(body_len) + 6;

                // 帧尺寸合法性校验
                if (frame_size < GHV_MIN_FRAME_SIZE || frame_size > GHV_MAX_FRAME_SIZE) {
                    fprintf(stderr, "[ghv] SECURITY: server invalid encrypted frame size %zu "
                                    "(body_len=%u) for conn %u, kicking\n",
                            frame_size, body_len, channel->id());
                    rb.clear();
                    notify_security_and_close(L, self, channel, GHV_SEC_FRAME_INVALID, 0);
                    return;
                }

                if (rb.size() - consumed < frame_size) {
                    break;  // 帧未完整，等待更多数据
                }

                // 完整帧：就地解密（ptr 指向 rb 内部可写区域）
                CryptoProtocol::DecryptResult decrypt_out;
                if (!session->crypto.DecryptAndVerify(ptr, frame_size, decrypt_out)) {
                    fprintf(stderr, "[ghv] SECURITY: server decrypt/MAC failed for conn %u, kicking\n",
                            channel->id());
                    rb.clear();
                    notify_security_and_close(L, self, channel, GHV_SEC_MAC_FAILED, 0);
                    return;
                }

                auto [seq_no, plaintext, plain_len] = decrypt_out;

                if (!session->replay_window.Accept(seq_no)) {
                    fprintf(stderr, "[ghv] SECURITY: server replay detected (conn=%u, seq=%u), kicking\n",
                            channel->id(), seq_no);
                    rb.clear();
                    notify_security_and_close(L, self, channel, GHV_SEC_REPLAY, seq_no);
                    return;
                }

                // 投递解密后的明文给 Lua
                // NOTE: plaintext 指向 rb 内部（就地解密），lua_pushlstring 会复制数据，
                //       后续操作不会影响已复制的 Lua 字符串。
                if (push_server_userdata(L, self)) {
                    int ud_idx = lua_gettop(L);
                    if (ghv_get_lua_ref(L, ud_idx, "on_message")) {
                        lua_pushinteger(L, channel->id());
                        lua_pushlstring(L, reinterpret_cast<const char*>(plaintext), plain_len);
                        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                            fprintf(stderr, "[ghv] server message callback error: %s\n",
                                    lua_tostring(L, -1));
                            lua_pop(L, 1);
                        }
                    }
                    lua_pop(L, 1);
                }

                consumed += frame_size;
            }
            // P3-1: 批量移除已处理的帧（单次 memmove，替代逐帧 erase）
            if (consumed > 0) {
                rb.erase(rb.begin(), rb.begin() + static_cast<ptrdiff_t>(consumed));
            }
            return;
        } else if (session && session->state == ConnectionSecurityState::Handshaking) {
            // --- Handshake phase: reject non-handshake data ---
            fprintf(stderr, "[ghv] SECURITY: server dropping data during key exchange (conn=%u)\n",
                    channel->id());
            return;
        }

        // --- Plaintext mode ---
        const uint8_t* payload = static_cast<const uint8_t*>(buf->data());
        size_t payload_len = buf->size();

        if (push_server_userdata(L, self)) {
            int ud_idx = lua_gettop(L);
            if (ghv_get_lua_ref(L, ud_idx, "on_message")) {
                lua_pushinteger(L, channel->id());
                lua_pushlstring(L, reinterpret_cast<const char*>(payload), payload_len);
                if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                    fprintf(stderr, "[ghv] server message callback error: %s\n", lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }
    };

    // Apply unpack setting
    if (self->unpack) {
        self->server->setUnpack(self->unpack.get());
    }

    // startAccept directly (no thread launch)
    int rc = self->server->startAccept();

    lua_pushboolean(L, rc == 0 ? 1 : 0);
    return 1;
}

// hv:stop()
static int l_tcp_server_stop(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    if (self->server) {
        self->server->stop(false);
    }
    release_self_ref(self);
    return 0;
}

// hv:send(id, data)
static int l_tcp_server_send(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    uint32_t id = ghv_check_uint32(L, 2);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 3, &len);

    auto channel = self->server->getChannelById(id);
    if (channel) {
        auto* session = static_cast<SessionCrypto*>(channel->context());
        if (session && session->state == ConnectionSecurityState::Encrypted
                && session->crypto.IsKeySet()) {
            // Encrypted send (C3: 复用 send_buf，避免每次堆分配)
            uint32_t seq = ++session->send_seq;
            if (seq == 0) {
                seq = ++session->send_seq;  // Skip 0 (reserved by AntiReplayWindow)
                fprintf(stderr, "[ghv] WARNING: server send_seq wrapped for conn %u, "
                                "consider re-keying\n", id);
            }
            session->send_buf.clear();  // clear 不释放内存，仅重置 size
            if (!session->crypto.EncryptAndSeal(
                    reinterpret_cast<const uint8_t*>(data), len, seq, session->send_buf)) {
                fprintf(stderr, "[ghv] server send encryption failed for conn %u\n", id);
                lua_pushboolean(L, 0);
                return 1;
            }
            channel->write(reinterpret_cast<const char*>(session->send_buf.data()),
                           session->send_buf.size());
        } else {
            // Plaintext send
            channel->write(data, len);
        }
        lua_pushboolean(L, 1);
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

// hv:broadcast(data)
static int l_tcp_server_broadcast(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    if (!self->server) return 0;

    // C2 修复：逐连接广播，加密连接独立加密（每个连接有独立的 seq/nonce）
    self->server->foreachChannel([&](const hv::SocketChannelPtr& channel) {
        auto* session = static_cast<SessionCrypto*>(channel->context());
        if (session && session->state == ConnectionSecurityState::Encrypted
                && session->crypto.IsKeySet()) {
            // 加密连接：逐连接独立加密
            uint32_t seq = ++session->send_seq;
            if (seq == 0) {
                seq = ++session->send_seq;
                fprintf(stderr, "[ghv] WARNING: server broadcast send_seq wrapped for conn, "
                                "consider re-keying\n");
            }
            session->send_buf.clear();
            if (session->crypto.EncryptAndSeal(
                    reinterpret_cast<const uint8_t*>(data), len, seq, session->send_buf)) {
                channel->write(reinterpret_cast<const char*>(session->send_buf.data()),
                               session->send_buf.size());
            }
        } else {
            // 明文连接：直接发送
            channel->write(data, len);
        }
    });

    return 0;
}

// hv:disconnect(id)
static int l_tcp_server_disconnect(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    uint32_t id = ghv_check_uint32(L, 2);
    auto channel = self->server->getChannelById(id);
    if (channel) {
        channel->close();
    }
    return 0;
}

// hv:connectionNum()
static int l_tcp_server_connection_num(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    lua_pushinteger(L, self->server ? static_cast<lua_Integer>(self->server->connectionNum()) : 0);
    return 1;
}

// hv:setMaxConnectionNum(num)
static int l_tcp_server_set_max_conn(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    uint32_t num = ghv_check_uint32(L, 2);
    if (self->server) {
        self->server->setMaxConnectionNum(num);
    }
    return 0;
}

// hv:setThreadNum(num) - IGNORED in single-thread mode, kept for API compat
static int l_tcp_server_set_thread_num(lua_State* L) {
    return 0;
}

// Callback setters
static int l_tcp_server_setcb_connect(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_connect", 2);
    return 0;
}

static int l_tcp_server_setcb_message(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_message", 2);
    return 0;
}

static int l_tcp_server_setcb_close(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_close", 2);
    return 0;
}

// hv:setcb_security(function(id, event, seq_no) end)  (M1: 安全事件回调)
// event: 1=MAC验证失败  2=重放攻击  3=帧格式非法
static int l_tcp_server_setcb_security(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_security", 2);
    return 0;
}

// hv:setUnpack(head_flag_len, len_field_bytes)
// Legacy unpack for plaintext mode
static int l_tcp_server_set_unpack(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    int head_flag_len = static_cast<int>(luaL_checkinteger(L, 2));
    int len_field_bytes = static_cast<int>(luaL_checkinteger(L, 3));

    if (!self->unpack) {
        self->unpack = std::make_unique<unpack_setting_t>();
    }
    memset(self->unpack.get(), 0, sizeof(unpack_setting_t));
    self->unpack->mode = UNPACK_BY_LENGTH_FIELD;
    self->unpack->package_max_length = GHV_MAX_FRAME_SIZE;  // 统一 1MB 限制，防恶意超大包 (C1)
    self->unpack->body_offset = head_flag_len + len_field_bytes;
    self->unpack->length_field_offset = head_flag_len;
    self->unpack->length_field_bytes = len_field_bytes;
    self->unpack->length_field_coding = ENCODE_BY_LITTEL_ENDIAN;
    self->unpack->length_adjustment = 0;

    return 0;
}


// hv:setEncryptionKey(id, key_string_32bytes)
// Set encryption key for a specific connection
static int l_tcp_server_set_encryption_key(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    uint32_t id = ghv_check_uint32(L, 2);
    size_t key_len = 0;
    const char* key = luaL_checklstring(L, 3, &key_len);

    if (key_len != GHV_KEY_SIZE) {
        return luaL_error(L, "encryption key must be exactly %d bytes, got %d",
                          static_cast<int>(GHV_KEY_SIZE), static_cast<int>(key_len));
    }

    auto channel = self->server->getChannelById(id);
    if (!channel) {
        return luaL_error(L, "connection %d not found", id);
    }

    // Get or create SessionCrypto for this connection
    auto* session = static_cast<SessionCrypto*>(channel->context());
    if (!session) {
        // C3 FIX: make_unique 创建 → release() 交给 channel context
        auto owned = std::make_unique<SessionCrypto>();
        session = owned.get();
        channel->setContext(owned.release());
    }

    session->crypto.SetSessionKey(reinterpret_cast<const uint8_t*>(key), key_len);
    session->state = ConnectionSecurityState::Encrypted;
    session->send_seq = 0;
    session->replay_window.Reset();
    session->recv_buf.clear();

    return 0;
}

// hv:clearEncryption(id)
static int l_tcp_server_clear_encryption(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    uint32_t id = ghv_check_uint32(L, 2);

    auto channel = self->server->getChannelById(id);
    if (channel) {
        auto* session = static_cast<SessionCrypto*>(channel->context());
        if (session) {
            session->crypto.ClearKey();
            session->state = ConnectionSecurityState::Plaintext;
            session->send_seq = 0;
            session->replay_window.Reset();
            session->recv_buf.clear();
        }
    }
    return 0;
}

// hv:generateKeyPair(id) -> pub_key_string (32 bytes)
static int l_tcp_server_generate_key_pair(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    uint32_t id = ghv_check_uint32(L, 2);

    auto channel = self->server->getChannelById(id);
    if (!channel) return luaL_error(L, "connection %d not found", id);

    auto* session = static_cast<SessionCrypto*>(channel->context());
    if (!session) {
        auto owned = std::make_unique<SessionCrypto>();
        session = owned.get();
        channel->setContext(owned.release());
    }

    uint8_t pub_key[32];
    if (!session->kex.GenerateKeyPair(pub_key)) {
        return luaL_error(L, "X25519 key generation failed");
    }
    // NOTE: 不设置 Handshaking 状态 — 密钥交换 RPC 往返期间需要明文通信
    // deriveAndEncrypt 成功后直接 None → Encrypted
    lua_pushlstring(L, reinterpret_cast<const char*>(pub_key), 32);
    return 1;
}

// hv:deriveAndEncrypt(id, peer_pub_32bytes) -> boolean
static int l_tcp_server_derive_and_encrypt(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    uint32_t id = ghv_check_uint32(L, 2);
    size_t peer_len = 0;
    const char* peer_pub = luaL_checklstring(L, 3, &peer_len);
    if (peer_len != 32) {
        return luaL_error(L, "peer public key must be 32 bytes, got %d",
                          static_cast<int>(peer_len));
    }

    auto channel = self->server->getChannelById(id);
    if (!channel) return luaL_error(L, "connection %d not found", id);

    auto* session = static_cast<SessionCrypto*>(channel->context());
    if (!session) return luaL_error(L, "no session for connection %d", id);

    uint8_t session_key[GHV_KEY_SIZE];
    if (!session->kex.DeriveSessionKey(
            reinterpret_cast<const uint8_t*>(peer_pub), session_key)) {
        OPENSSL_cleanse(session_key, sizeof(session_key));
        lua_pushboolean(L, 0);
        return 1;
    }

    session->crypto.SetSessionKey(session_key, GHV_KEY_SIZE);
    OPENSSL_cleanse(session_key, sizeof(session_key));

    // 禁用该连接的 libhv unpack — 加密模式由 C++ 手动拆帧接管
    channel->setUnpack(nullptr);

    session->state = ConnectionSecurityState::Encrypted;
    session->send_seq = 0;
    session->replay_window.Reset();
    session->recv_buf.clear();

    lua_pushboolean(L, 1);
    return 1;
}

// hv:withSSL(crt, key, [verify_peer])
static int l_tcp_server_with_ssl(lua_State* L) {
    LuaTcpServer* self = check_tcp_server(L);
    hssl_ctx_opt_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.endpoint = HSSL_SERVER;

    if (lua_isstring(L, 2)) opt.crt_file = lua_tostring(L, 2);
    if (lua_isstring(L, 3)) opt.key_file = lua_tostring(L, 3);
    if (lua_isboolean(L, 4)) opt.verify_peer = lua_toboolean(L, 4);

    self->server->withTLS(&opt);
    return 0;
}

// hv:run() -- main thread event dispatch (shared with TcpClient)
static int l_tcp_server_run(lua_State* L) {
    ghv_poll_events();
    return 0;
}

// ============================================================
// Ed25519 (防 MITM)
// ============================================================

// hv:ed25519Sign(msg, priv_key_32bytes) -> sig_64bytes
static int l_tcp_server_ed25519_sign(lua_State* L) {
    // 允许不传递 self 也可以当静态方法调用，但鉴于冒号语法，这里假定 1 是 self
    size_t msg_len = 0;
    const char* msg = luaL_checklstring(L, 2, &msg_len);
    size_t priv_len = 0;
    const char* priv = luaL_checklstring(L, 3, &priv_len);
    if (priv_len != 32) return luaL_error(L, "Ed25519 private key must be 32 bytes");

#ifndef GHV_NO_CRYPTO
    uint8_t sig[64];
    if (KeyExchange::Ed25519Sign(reinterpret_cast<const uint8_t*>(msg), msg_len,
                                 reinterpret_cast<const uint8_t*>(priv), sig)) {
        lua_pushlstring(L, reinterpret_cast<const char*>(sig), 64);
        return 1;
    }
#endif
    return 0;
}

// hv:ed25519GenerateKeyPair() -> priv_key_out, pub_key_out
static int l_tcp_server_ed25519_gen(lua_State* L) {
#ifndef GHV_NO_CRYPTO
    uint8_t priv[32];
    uint8_t pub[32];
    if (KeyExchange::Ed25519GenerateKeyPair(priv, pub)) {
        lua_pushlstring(L, reinterpret_cast<const char*>(priv), 32);
        lua_pushlstring(L, reinterpret_cast<const char*>(pub), 32);
        return 2;
    }
#endif
    return 0;
}

// GC — deferred destruction to prevent UAF when GC fires inside callback stacks.
// Same pattern as TcpClient: if GC triggers during onMessage/onConnection,
// direct delete → ~LuaTcpServer → server->stop() would execute inside the
// event dispatch call stack = UAF.
static int l_tcp_server_gc(lua_State* L) {
    auto** ud = static_cast<LuaTcpServer**>(luaL_checkudata(L, 1, GHV_TCP_SERVER_META));
    LuaTcpServer* self = *ud;
    if (!self) return 0;
    *ud = nullptr;  // Prevent double-free if __gc is re-entered

    // Release registry ref immediately (safe during GC)
    release_self_ref(self);

    // Clear callbacks to prevent any future event from touching dead Lua state
    if (self->server) {
        self->server->onConnection = nullptr;
        self->server->onMessage = nullptr;
    }

    // Defer actual destruction via queueInLoop (eventfd), guaranteeing
    // execution in a clean call stack during the next event loop iteration.
    bool deferred = false;
    auto& loop = ghv_shared_event_loop();
    if (loop && loop->isRunning()) {
        loop->queueInLoop([self]() {
            delete self;
        });
        deferred = true;
    }

    if (!deferred) {
        // Fallback: loop not running (e.g., program shutdown).
        delete self;
    }
    return 0;
}

// Constructor
static int l_tcp_server_new(lua_State* L) {
    LuaTcpServer* self = new LuaTcpServer(L);
    auto** ud = static_cast<LuaTcpServer**>(lua_newuserdata(L, sizeof(LuaTcpServer*)));
    *ud = self;
    lua_newtable(L);
    lua_setiuservalue(L, -2, 1);
    luaL_setmetatable(L, GHV_TCP_SERVER_META);
    return 1;
}

GHV_EXPORT int luaopen_ghv_TcpServer(lua_State* L)
{
    luaL_Reg methods[] = {
        {"start",               l_tcp_server_start},
        {"stop",                l_tcp_server_stop},
        {"send",                l_tcp_server_send},
        {"broadcast",           l_tcp_server_broadcast},
        {"disconnect",          l_tcp_server_disconnect},
        {"connectionNum",       l_tcp_server_connection_num},
        {"setMaxConnectionNum", l_tcp_server_set_max_conn},
        {"setThreadNum",        l_tcp_server_set_thread_num},
        {"setcb_connect",       l_tcp_server_setcb_connect},
        {"setcb_message",       l_tcp_server_setcb_message},
        {"setcb_close",         l_tcp_server_setcb_close},
        {"setcb_security",      l_tcp_server_setcb_security},  // M1: 安全事件回调
        {"setUnpack",           l_tcp_server_set_unpack},
        {"setEncryptionKey",    l_tcp_server_set_encryption_key},  // 保留低级 API
        {"clearEncryption",     l_tcp_server_clear_encryption},
        {"generateKeyPair",     l_tcp_server_generate_key_pair},   // ECDH
        {"deriveAndEncrypt",    l_tcp_server_derive_and_encrypt},  // ECDH
        {"ed25519Sign",         l_tcp_server_ed25519_sign},        // 防 MITM 签名
        {"ed25519GenerateKeyPair", l_tcp_server_ed25519_gen},      // 生成主公私钥对
        {"withSSL",             l_tcp_server_with_ssl},
        {"run",                 l_tcp_server_run},
        {NULL, NULL},
    };

    luaL_newmetatable(L, GHV_TCP_SERVER_META);
    luaL_newlib(L, methods);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_tcp_server_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    lua_pushcfunction(L, l_tcp_server_new);
    return 1;
}
