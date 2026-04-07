/**
 * ghv_tcp_client.cpp - libhv TcpClient Lua binding (single-thread refactor)
 * Exports: luaopen_ghv_TcpClient
 *
 * Key changes:
 * - Uses TcpClientEventLoopTmpl (no separate IO thread) instead of TcpClientTmpl
 * - Shares global hloop_t, driven by ghv:run() via hloop_process_events(loop, 0)
 * - Callbacks fire directly in main-thread Lua VM, no cross-thread event queue
 * - AEAD encryption (AES-256-GCM) with anti-replay protection
 */
#include "ghv_common.h"
#include "ghv_crypto.h"
#include "ghv_net_protocol.h"
#ifndef GHV_NO_CRYPTO
#include <openssl/crypto.h>  // OPENSSL_cleanse
#endif
#include "TcpClient.h"

#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h>
#endif

#define GHV_TCP_CLIENT_META "GHV_TcpClient"

struct LuaTcpClient {
    std::shared_ptr<hv::TcpClientEventLoopTmpl<hv::SocketChannel>> client;
    lua_State*     L;
    bool           connected;
    int            self_ref;  // registry reference to the userdata (prevent GC during callbacks)

    // unpack setting (owned, lifetime matches client)
    std::unique_ptr<unpack_setting_t> unpack;

    // Encryption state
    CryptoProtocol  crypto;
    AntiReplayWindow replay_window;
    uint32_t        send_seq_           = 0;
    ConnectionSecurityState security_state_ = ConnectionSecurityState::Plaintext;
    // send_buf_ removed to avoid async IO memory races
    std::vector<uint8_t> recv_buf_;     // 加密帧接收缓冲（手动循环拆帧）
    KeyExchange         kex_;            // ECDH 密钥协商（X25519）

    // M3: 应用层心跳
    int         heartbeat_interval_ms_ = 0;   // 0 = 关闭心跳
    hv::TimerID heartbeat_timer_id_    = 0;   // 活跃的心跳定时器 ID（0=未启动）

    LuaTcpClient(lua_State* _L)
        : L(_L), connected(false), self_ref(LUA_NOREF)
    {
        auto evloop = ghv_shared_event_loop();
        client = std::make_shared<hv::TcpClientEventLoopTmpl<>>(evloop);
    }

    ~LuaTcpClient() {
        // Bug-2 FIX: 析构前停止心跳定时器，防止 UAF
        if (heartbeat_timer_id_ != 0 && client) {
            auto& loop = client->loop();
            if (loop) loop->killTimer(heartbeat_timer_id_);
            heartbeat_timer_id_ = 0;
        }
        if (client) {
            client->closesocket();
            client.reset();
        }
        // unpack 由 unique_ptr 自动释放
    }
};

static LuaTcpClient* check_tcp_client(lua_State* L) {
    return *static_cast<LuaTcpClient**>(luaL_checkudata(L, 1, GHV_TCP_CLIENT_META));
}

// 安全事件枚举 — 用于通知 Lua 状态机具体的安全威胁类型 (M1)
enum GHV_ClientSecurityEvent : int {
    GHV_CLI_SEC_MAC_FAILED    = 1,  // MAC 验证失败（数据被篡改）
    GHV_CLI_SEC_REPLAY        = 2,  // 重放攻击检测
};

// Forward declaration (defined below release_self_ref)
static bool push_self_userdata(lua_State* L, LuaTcpClient* self);

// 辅助函数：通知 Lua 安全事件后关闭连接 (M1)
static void notify_client_security_and_close(lua_State* L, LuaTcpClient* self,
                                              const hv::SocketChannelPtr& channel,
                                              GHV_ClientSecurityEvent event, uint32_t seq_no) {
    if (push_self_userdata(L, self)) {
        int ud_idx = lua_gettop(L);
        if (ghv_get_lua_ref(L, ud_idx, "on_security")) {
            lua_pushinteger(L, event);
            lua_pushinteger(L, seq_no);
            if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                fprintf(stderr, "[ghv] client security callback error: %s\n",
                        lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    channel->close();
}

// Release registry reference so GC can collect us
static void release_self_ref(LuaTcpClient* self) {
    if (self->self_ref != LUA_NOREF) {
        luaL_unref(self->L, LUA_REGISTRYINDEX, self->self_ref);
        self->self_ref = LUA_NOREF;
    }
}

// Push self userdata onto stack from registry for callback dispatch
static bool push_self_userdata(lua_State* L, LuaTcpClient* self) {
    if (self->self_ref == LUA_NOREF) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->self_ref);
    return true;
}

// hv:connect(ip, port)
static int l_tcp_client_connect(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
    const char* ip = luaL_checkstring(L, 2);
    int port = static_cast<int>(luaL_checkinteger(L, 3));

    // Apply unpack setting to the base client to enable native TCP defragmentation
    if (self->unpack) {
        self->client->setUnpack(self->unpack.get());
    }

    // Clean up old connection if one exists
    if (self->connected) {
        self->client->closesocket();
        self->connected = false;
    }

    // S1 FIX: 重连时重置加密状态，防止旧密钥/SeqNo 泄漏到新连接
    // 场景：自动重连(setReconnect)后，新连接需要重新协商密钥
    self->crypto.ClearKey();
    self->security_state_ = ConnectionSecurityState::Plaintext;
    self->send_seq_ = 0;
    self->replay_window.Reset();
    self->recv_buf_.clear();  // 清空加密帧接收缓冲

    // Save a registry reference to the userdata so we can find it during callbacks
    if (self->self_ref == LUA_NOREF) {
        lua_pushvalue(L, 1);
        self->self_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    // Setup socket
    int connfd = self->client->createsocket(port, ip);
    if (connfd < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }

    // Setup onConnection callback - runs DIRECTLY in main thread during hloop_process_events
    self->client->onConnection = [self](const hv::SocketChannelPtr& channel) {
        lua_State* L = self->L;
        if (channel->isConnected()) {
            // === Socket optimization for game RPC ===
            int fd = channel->fd();
            // 1. Disable Nagle algorithm for low-latency RPC
            int flag = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
            // 2. (REMOVED) Windows 系统默认的 AFD 自动缓冲缩放能更好地应付爆发型大包数据
            //    硬编码限制 256KB 反而容易导致发送满溢而触及 libhv 层 max_write_bufsize 断开
            // 3. Enable TCP keepalive
            setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&flag), sizeof(flag));
            self->connected = true;

            // M3: 启动应用层心跳定时器（如果已配置间隔）
            if (self->heartbeat_interval_ms_ > 0 && self->heartbeat_timer_id_ == 0) {
                auto& loop = self->client->loop();
                if (loop) {
                    self->heartbeat_timer_id_ = loop->setInterval(
                        self->heartbeat_interval_ms_,
                        [self](hv::TimerID) {
                            if (!self->connected) return;
                            lua_State* LL = self->L;
                            if (push_self_userdata(LL, self)) {
                                int ud = lua_gettop(LL);
                                if (ghv_get_lua_ref(LL, ud, "on_heartbeat")) {
                                    if (lua_pcall(LL, 0, 0, 0) != LUA_OK) {
                                        fprintf(stderr, "[ghv] heartbeat callback error: %s\n",
                                                lua_tostring(LL, -1));
                                        lua_pop(LL, 1);
                                    }
                                }
                                lua_pop(LL, 1); // pop userdata
                            }
                        });
                }
            }

            if (push_self_userdata(L, self)) {
                int ud_idx = lua_gettop(L);
                if (ghv_get_lua_ref(L, ud_idx, "on_connect")) {
                    lua_pushstring(L, channel->peeraddr().c_str());
                    lua_pushboolean(L, 1);
                    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                        fprintf(stderr, "[ghv] connect callback error: %s\n", lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1); // pop userdata
            }
        } else {
            self->connected = false;

            // M3: 停止心跳定时器
            if (self->heartbeat_timer_id_ != 0) {
                auto& loop = self->client->loop();
                if (loop) loop->killTimer(self->heartbeat_timer_id_);
                self->heartbeat_timer_id_ = 0;
            }

            if (push_self_userdata(L, self)) {
                int ud_idx = lua_gettop(L);
                if (ghv_get_lua_ref(L, ud_idx, "on_close")) {
                    lua_pushinteger(L, channel->error());
                    lua_pushstring(L, "");
                    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                        fprintf(stderr, "[ghv] close callback error: %s\n", lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1); // pop userdata
            }
            // Connection closed: defer release of registry ref via queueInLoop
            // (eventfd). Unlike setTimeout(1ms) which can fire in the SAME
            // hloop_process_events iteration during the timer phase, queueInLoop
            // posts via eventfd and is guaranteed to run in the NEXT iteration.
            {
                lua_State* LL = self->L;
                int ref = self->self_ref;
                self->self_ref = LUA_NOREF;  // Mark as "release scheduled"
                if (ref != LUA_NOREF) {
                    self->client->loop()->queueInLoop([LL, ref]() {
                        luaL_unref(LL, LUA_REGISTRYINDEX, ref);
                    });
                }
            }
        }
    };

    // Setup onMessage callback — with optional AEAD decryption
    self->client->onMessage = [self](const hv::SocketChannelPtr& channel, hv::Buffer* buf) {
        lua_State* L = self->L;

        if (self->security_state_ == ConnectionSecurityState::Encrypted
                && self->crypto.IsKeySet()) {
            // --- Encrypted mode: 手动循环拆帧 ---
            // libhv unpack 可能不可靠地拆分帧，buf 可能包含多帧粘包数据。
            auto& rb = self->recv_buf_;
            const uint8_t* incoming = static_cast<const uint8_t*>(buf->data());
            size_t incoming_len = buf->size();
            rb.insert(rb.end(), incoming, incoming + incoming_len);

            // P3-1: 循环解包，处理完毕立即从缓冲区截断并丢弃指针，避免 Lua pcall 重入引发的 Iterator Invalidation (UAF) 与内存争用
            while (rb.size() >= 6) {  // 至少需要 Magic(2) + Length(4)
                uint8_t* ptr = rb.data();

                // P3-5: 快速校验 Magic 字节，非法数据立即失败
                if (ptr[0] != GHV_MAGIC_BYTE_0 || ptr[1] != GHV_MAGIC_BYTE_1) {
                    fprintf(stderr, "[ghv] SECURITY: client invalid magic in recv buffer, closing\n");
                    rb.clear();
                    notify_client_security_and_close(L, self, channel, GHV_CLI_SEC_MAC_FAILED, 0);
                    return;
                }

                // 读取 Length 字段（LE, offset 2, 4 bytes）
                uint32_t body_len;
                memcpy(&body_len, ptr + 2, 4);
                size_t frame_size = static_cast<size_t>(body_len) + 6;

                // 帧尺寸合法性校验
                if (frame_size < GHV_MIN_FRAME_SIZE || frame_size > GHV_MAX_FRAME_SIZE) {
                    fprintf(stderr, "[ghv] SECURITY: client invalid encrypted frame size %zu "
                                    "(body_len=%u), closing\n", frame_size, body_len);
                    rb.clear();
                    notify_client_security_and_close(L, self, channel, GHV_CLI_SEC_MAC_FAILED, 0);
                    return;
                }

                if (rb.size() < frame_size) {
                    break;  // 帧未完整，等待更多数据
                }

                // 完整帧：就地解密（ptr 指向 rb 内部可写区域）
                CryptoProtocol::DecryptResult decrypt_out;
                if (!self->crypto.DecryptAndVerify(ptr, frame_size, decrypt_out)) {
                    fprintf(stderr, "[ghv] SECURITY: decrypt/MAC failed, closing connection\n");
                    rb.clear();
                    notify_client_security_and_close(L, self, channel, GHV_CLI_SEC_MAC_FAILED, 0);
                    return;
                }

                auto [seq_no, plaintext, plain_len] = decrypt_out;

                if (!self->replay_window.Accept(seq_no)) {
                    fprintf(stderr, "[ghv] SECURITY: replay detected (seq=%u), closing connection\n", seq_no);
                    rb.clear();
                    notify_client_security_and_close(L, self, channel, GHV_CLI_SEC_REPLAY, seq_no);
                    return;
                }

                // 关键一步：在进入可能挂起或重入的 Lua pcall 之前，
                // 深拷贝出业务明文，并【立即】将这段记忆从缓冲区擦除，不再信任 ptr 的生命周期！
                std::string lua_payload(reinterpret_cast<const char*>(plaintext), plain_len);
                rb.erase(rb.begin(), rb.begin() + static_cast<ptrdiff_t>(frame_size));

                // 投递深拷贝后的明文给 Lua
                if (push_self_userdata(L, self)) {
                    int ud_idx = lua_gettop(L);
                    if (ghv_get_lua_ref(L, ud_idx, "on_message")) {
                        lua_pushlstring(L, lua_payload.data(), lua_payload.size());
                        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                            fprintf(stderr, "[ghv] message callback error: %s\n",
                                    lua_tostring(L, -1));
                            lua_pop(L, 1);
                        }
                    }
                    lua_pop(L, 1);
                }
            }
            return;
        } else if (self->security_state_ == ConnectionSecurityState::Handshaking) {
            // --- Handshake phase: reject non-handshake data ---
            fprintf(stderr, "[ghv] SECURITY: dropping data during key exchange\n");
            return;
        }

        // --- Plaintext mode (legacy) ---
        const uint8_t* payload = static_cast<const uint8_t*>(buf->data());
        size_t payload_len = buf->size();

        // Deliver plaintext payload to Lua callback
        if (push_self_userdata(L, self)) {
            int ud_idx = lua_gettop(L);
            if (ghv_get_lua_ref(L, ud_idx, "on_message")) {
                lua_pushlstring(L, reinterpret_cast<const char*>(payload), payload_len);
                if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                    fprintf(stderr, "[ghv] message callback error: %s\n", lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1); // pop userdata
        }
    };

    // Apply unpack setting if set
    if (self->unpack) {
        self->client->setUnpack(self->unpack.get());
    }

    // Directly start connect (no thread, no queueInLoop)
    int rc = self->client->startConnect();

    lua_pushboolean(L, rc >= 0 ? 1 : 0);
    return 1;
}

// hv:disconnect()
static int l_tcp_client_disconnect(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
    if (self->client) {
        self->connected = false;
        self->client->closesocket();
    }
    return 0;
}

// hv:send(data)
static int l_tcp_client_send(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    if (self->client && self->connected) {
        if (self->security_state_ == ConnectionSecurityState::Encrypted
                && self->crypto.IsKeySet()) {
            // Encrypted send: plaintext → encrypt → send frame (C3: 复用 send_buf_)
            uint32_t seq = ++self->send_seq_;
            if (seq == 0) {
                seq = ++self->send_seq_;  // Skip 0 (reserved by AntiReplayWindow)
                fprintf(stderr, "[ghv] WARNING: client send_seq wrapped around, "
                                "consider re-keying the connection\n");
            }
            std::vector<uint8_t> frame_buf;
            if (!self->crypto.EncryptAndSeal(
                    reinterpret_cast<const uint8_t*>(data), len, seq, frame_buf)) {
                fprintf(stderr, "[ghv] send encryption failed\n");
                lua_pushboolean(L, 0);
                return 1;
            }
            int ret = self->client->send(reinterpret_cast<const char*>(frame_buf.data()),
                                          static_cast<int>(frame_buf.size()));
            lua_pushboolean(L, ret >= 0);
        } else {
            // Plaintext send (legacy)
            int ret = self->client->send(data, static_cast<int>(len));
            lua_pushboolean(L, ret >= 0);
        }
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

// hv:isConnected()
static int l_tcp_client_is_connected(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
    lua_pushboolean(L, self->connected ? 1 : 0);
    return 1;
}

// hv:setcb_connect(function(addr, connected) end)
static int l_tcp_client_setcb_connect(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_connect", 2);
    return 0;
}

// hv:setcb_message(function(data) end)
static int l_tcp_client_setcb_message(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_message", 2);
    return 0;
}

// hv:setcb_close(function(err, errstr) end)
static int l_tcp_client_setcb_close(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_close", 2);
    return 0;
}

// hv:setcb_security(function(event, seq_no) end)  (M1: 安全事件回调)
// event: 1=MAC验证失败  2=重放攻击
static int l_tcp_client_setcb_security(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_security", 2);
    return 0;
}

// hv:setUnpack(head_len, body_len_field_size)
// Legacy unpack for plaintext mode
static int l_tcp_client_set_unpack(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
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

    // C6: 已连接时热切换 unpack（支持加密→明文回退）
    if (self->connected && self->client && self->client->channel) {
        self->client->channel->setUnpack(self->unpack.get());
    }

    return 0;
}



// hv:setEncryptionKey(key_string_32bytes)
static int l_tcp_client_set_encryption_key(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
    size_t key_len = 0;
    const char* key = luaL_checklstring(L, 2, &key_len);

    if (key_len != GHV_KEY_SIZE) {
        return luaL_error(L, "encryption key must be exactly %d bytes, got %d",
                          static_cast<int>(GHV_KEY_SIZE), static_cast<int>(key_len));
    }

    self->crypto.SetSessionKey(reinterpret_cast<const uint8_t*>(key), key_len);
    self->security_state_ = ConnectionSecurityState::Encrypted;
    self->send_seq_ = 0;
    self->replay_window.Reset();
    self->recv_buf_.clear();

    return 0;
}

// hv:clearEncryption()
static int l_tcp_client_clear_encryption(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
    self->crypto.ClearKey();
    self->security_state_ = ConnectionSecurityState::Plaintext;
    self->send_seq_ = 0;
    self->replay_window.Reset();
    self->recv_buf_.clear();
    return 0;
}

// hv:generateKeyPair() -> pub_key_string (32 bytes)
// 生成 X25519 临时密钥对，返回 32 字节公钥（可明文发送给对方）
static int l_tcp_client_generate_key_pair(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
    uint8_t pub_key[32];
    if (!self->kex_.GenerateKeyPair(pub_key)) {
        return luaL_error(L, "X25519 key generation failed");
    }
    // NOTE: 不设置 Handshaking 状态 — 密钥交换 RPC 往返期间需要明文通信
    // deriveAndEncrypt 成功后直接 None → Encrypted
    lua_pushlstring(L, reinterpret_cast<const char*>(pub_key), 32);
    return 1;
}

// hv:deriveAndEncrypt(peer_pub_32bytes) -> boolean
// ECDH → HKDF → SetSessionKey → 切换加密，session_key 永不经过 Lua
static int l_tcp_client_derive_and_encrypt(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
    size_t peer_len = 0;
    const char* peer_pub = luaL_checklstring(L, 2, &peer_len);
    if (peer_len != 32) {
        return luaL_error(L, "peer public key must be 32 bytes, got %d",
                          static_cast<int>(peer_len));
    }

    // 1. ECDH + HKDF → session_key (在栈上，不进 Lua)
    uint8_t session_key[GHV_KEY_SIZE];
    if (!self->kex_.DeriveSessionKey(
            reinterpret_cast<const uint8_t*>(peer_pub), session_key)) {
        OPENSSL_cleanse(session_key, sizeof(session_key));
        lua_pushboolean(L, 0);
        return 1;
    }

    // 2. 设置加密密钥
    self->crypto.SetSessionKey(session_key, GHV_KEY_SIZE);
    OPENSSL_cleanse(session_key, sizeof(session_key));  // 立即擦除

    // 3. 禁用 libhv unpack — 加密模式由 C++ 手动循环拆帧完全接管
    //    如果不禁用，libhv 可能在加密切换边界错误拆分帧，
    //    导致手动解析器收到非 0x4747 开头的碎片数据 (invalid magic)
    if (self->connected && self->client && self->client->channel) {
        self->client->channel->setUnpack(nullptr);
    }
    self->recv_buf_.clear();  // 清空接收缓冲区

    // 4. 更新状态
    self->security_state_ = ConnectionSecurityState::Encrypted;
    self->send_seq_ = 0;
    self->replay_window.Reset();

    lua_pushboolean(L, 1);
    return 1;
}

// hv:setReconnect(max_retry_count)
static int l_tcp_client_set_reconnect(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
    int max_retry = static_cast<int>(luaL_optinteger(L, 2, DEFAULT_RECONNECT_MAX_RETRY_CNT));

    reconn_setting_t reconn;
    reconn_setting_init(&reconn);
    reconn.max_retry_cnt = max_retry;
    self->client->setReconnect(&reconn);

    return 0;
}

// hv:withSSL([crt], [key], [verify_peer])
static int l_tcp_client_with_ssl(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
    hssl_ctx_opt_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.endpoint = HSSL_CLIENT;

    if (lua_isstring(L, 2)) opt.crt_file = lua_tostring(L, 2);
    if (lua_isstring(L, 3)) opt.key_file = lua_tostring(L, 3);
    if (lua_isboolean(L, 4)) opt.verify_peer = lua_toboolean(L, 4);

    self->client->withTLS(&opt);
    return 0;
}

// hv:setHeartbeat(interval_ms)  (M3: 应用层心跳间隔)
// interval_ms=0 关闭心跳。设置后在下次连接成功时自动启动。
static int l_tcp_client_set_heartbeat(lua_State* L) {
    LuaTcpClient* self = check_tcp_client(L);
    int ms = static_cast<int>(luaL_optinteger(L, 2, 0));
    self->heartbeat_interval_ms_ = ms;

    // 如果已连接，立即生效（重启定时器）
    if (self->connected && self->client) {
        auto& loop = self->client->loop();
        if (loop) {
            // 先停旧的
            if (self->heartbeat_timer_id_ != 0) {
                loop->killTimer(self->heartbeat_timer_id_);
                self->heartbeat_timer_id_ = 0;
            }
            // 再启新的
            if (ms > 0) {
                self->heartbeat_timer_id_ = loop->setInterval(ms,
                    [self](hv::TimerID) {
                        if (!self->connected) return;
                        lua_State* LL = self->L;
                        if (push_self_userdata(LL, self)) {
                            int ud = lua_gettop(LL);
                            if (ghv_get_lua_ref(LL, ud, "on_heartbeat")) {
                                if (lua_pcall(LL, 0, 0, 0) != LUA_OK) {
                                    fprintf(stderr, "[ghv] heartbeat callback error: %s\n",
                                            lua_tostring(LL, -1));
                                    lua_pop(LL, 1);
                                }
                            }
                            lua_pop(LL, 1);
                        }
                    });
            }
        }
    }
    return 0;
}

// hv:setcb_heartbeat(function() end)  (M3: 心跳触发回调)
// Lua 层在回调中发送自定义心跳包（不硬编码格式）
static int l_tcp_client_setcb_heartbeat(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_heartbeat", 2);
    return 0;
}

// ============================================================
// Ed25519 (防 MITM)
// ============================================================

// hv:ed25519Verify(msg, sig_64bytes, pub_key_32bytes) -> boolean
static int l_tcp_client_ed25519_verify(lua_State* L) {
    size_t msg_len = 0;
    const char* msg = luaL_checklstring(L, 2, &msg_len);
    size_t sig_len = 0;
    const char* sig = luaL_checklstring(L, 3, &sig_len);
    size_t pub_len = 0;
    const char* pub = luaL_checklstring(L, 4, &pub_len);

    if (sig_len != 64) return luaL_error(L, "Ed25519 signature must be exactly 64 bytes");
    if (pub_len != 32) return luaL_error(L, "Ed25519 public key must be exactly 32 bytes");

#ifndef GHV_NO_CRYPTO
    bool ok = KeyExchange::Ed25519Verify(
        reinterpret_cast<const uint8_t*>(msg), msg_len,
        reinterpret_cast<const uint8_t*>(sig), 
        reinterpret_cast<const uint8_t*>(pub));
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
#else
    lua_pushboolean(L, 0);
    return 1;
#endif
}

// hv:run() -- called from main thread each frame, non-blocking poll
static int l_tcp_client_run(lua_State* L) {
    ghv_poll_events();
    return 0;
}

// GC — deferred destruction to prevent UAF when GC fires inside callback stacks.
// Scenario: RPC handler → __网关:断开() → collectgarbage() → __gc
// If we delete self here, ~LuaTcpClient → closesocket() → runInLoop → direct
// execution → hio_close while still inside the onMessage/onread call stack = UAF.
static int l_tcp_client_gc(lua_State* L) {
    auto** ud = static_cast<LuaTcpClient**>(luaL_checkudata(L, 1, GHV_TCP_CLIENT_META));
    LuaTcpClient* self = *ud;
    if (!self) return 0;
    *ud = nullptr;  // Prevent double-free if __gc is re-entered

    // Release registry ref immediately (safe during GC, just modifies registry table)
    release_self_ref(self);

    // Clear callbacks to prevent any future event from touching dead Lua state
    if (self->client) {
        self->client->onConnection = nullptr;
        self->client->onMessage = nullptr;
    }

    // C5 FIX: 停止心跳定时器，防止延迟 delete 前仍有心跳回调触发
    if (self->heartbeat_timer_id_ != 0 && self->client) {
        auto& loop = self->client->loop();
        if (loop) loop->killTimer(self->heartbeat_timer_id_);
        self->heartbeat_timer_id_ = 0;
    }

    // Defer actual destruction via queueInLoop (eventfd), guaranteeing
    // execution in a clean call stack during the next event loop iteration.
    bool deferred = false;
    if (self->client) {
        auto& loop = self->client->loop();
        if (loop && loop->isRunning()) {
            auto client = self->client;
            loop->queueInLoop([self, client]() {
                if (client && client->channel && !client->channel->isClosed()) {
                    client->closesocket();
                }
                delete self;
            });
            deferred = true;
        }
    }

    if (!deferred) {
        // Fallback: loop not running (e.g., program shutdown).
        // Synchronous close is safe — no callback re-entrancy concern.
        delete self;
    }
    return 0;
}

// Constructor: require('ghv.TcpClient')()
static int l_tcp_client_new(lua_State* L) {
    LuaTcpClient* self = new LuaTcpClient(L);
    auto** ud = static_cast<LuaTcpClient**>(lua_newuserdata(L, sizeof(LuaTcpClient*)));
    *ud = self;
    // Create a table as uservalue to store Lua callback references
    lua_newtable(L);
    lua_setiuservalue(L, -2, 1);
    luaL_setmetatable(L, GHV_TCP_CLIENT_META);
    return 1;
}

GHV_EXPORT int luaopen_ghv_TcpClient(lua_State* L)
{
    luaL_Reg methods[] = {
        {"connect",            l_tcp_client_connect},
        {"disconnect",         l_tcp_client_disconnect},
        {"send",               l_tcp_client_send},
        {"isConnected",        l_tcp_client_is_connected},
        {"setcb_connect",      l_tcp_client_setcb_connect},
        {"setcb_message",      l_tcp_client_setcb_message},
        {"setcb_close",        l_tcp_client_setcb_close},
        {"setcb_security",     l_tcp_client_setcb_security},  // M1: 安全事件回调
        {"setUnpack",          l_tcp_client_set_unpack},
        {"setEncryptionKey",   l_tcp_client_set_encryption_key},  // 保留低级 API（调试用）
        {"clearEncryption",    l_tcp_client_clear_encryption},
        {"generateKeyPair",    l_tcp_client_generate_key_pair},   // ECDH: 生成公钥
        {"deriveAndEncrypt",   l_tcp_client_derive_and_encrypt},  // ECDH: 协商+加密
        {"ed25519Verify",      l_tcp_client_ed25519_verify},      // 防 MITM 验签
        {"setReconnect",       l_tcp_client_set_reconnect},
        {"withSSL",            l_tcp_client_with_ssl},
        {"setHeartbeat",        l_tcp_client_set_heartbeat},       // M3: 心跳间隔
        {"setcb_heartbeat",    l_tcp_client_setcb_heartbeat},     // M3: 心跳回调
        {"run",                l_tcp_client_run},
        {NULL, NULL},
    };

    luaL_newmetatable(L, GHV_TCP_CLIENT_META);
    luaL_newlib(L, methods);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_tcp_client_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    lua_pushcfunction(L, l_tcp_client_new);
    return 1;
}
