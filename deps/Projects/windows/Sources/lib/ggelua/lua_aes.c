/*
** lua_aes.c - AES encryption/decryption Lua binding
**
** Wraps rijndael.c (from SQLite3MultipleCiphers) to provide
** AES-CBC with PKCS7 padding for Lua.
**
** Usage:
**   local aes = require("aes")
**   local ct = aes.encrypt(key, iv, plaintext)
**   local pt = aes.decrypt(key, iv, ciphertext)
**
** key: 16/24/32 bytes (AES-128/192/256)
** iv:  16 bytes (CBC initialization vector)
*/

#include "luaopen.h"
#include <string.h>
#include <stdint.h>

/* Include rijndael as a single translation unit.
   Use explicit relative path (Sources/lib/ggelua → ../../.. → GGELUA3/)
   to avoid relying on build-system include paths, which are unreliable
   across generators (Xcode/CMake on iOS fails to propagate
   target_include_directories for this case). */
#include "../../../Dependencies/SQLite3MultipleCiphers/src/rijndael.c"

/* Map key length in bytes to Rijndael key length constant */
static int get_key_type(size_t keylen)
{
    switch (keylen)
    {
    case 16: return RIJNDAEL_Direction_KeyLength_Key16Bytes;
    case 24: return RIJNDAEL_Direction_KeyLength_Key24Bytes;
    case 32: return RIJNDAEL_Direction_KeyLength_Key32Bytes;
    default: return -1;
    }
}

/*
** aes.encrypt(key, iv, plaintext) -> ciphertext
**
** AES-CBC encryption with PKCS7 padding.
** Returns the encrypted binary string.
*/
static int LUA_AES_Encrypt(lua_State *L)
{
    const char *key  = luaL_checkstring(L, 1);
    const char *iv   = luaL_checkstring(L, 2);
    const char *data = luaL_checkstring(L, 3);
    
    size_t keylen = lua_rawlen(L, 1);
    size_t ivlen  = lua_rawlen(L, 2);
    size_t datalen= lua_rawlen(L, 3);

    int keyType = get_key_type(keylen);
    if (keyType < 0)
        return luaL_error(L, "aes.encrypt: key must be 16, 24, or 32 bytes (got %d)", (int)keylen);
    if (ivlen != 16)
        return luaL_error(L, "aes.encrypt: iv must be 16 bytes (got %d)", (int)ivlen);

    /* PKCS7 padding: pad to 16-byte boundary */
    int padLen = 16 - (int)(datalen % 16);
    size_t totalLen = datalen + padLen;

    /* Allocate padded input buffer */
    unsigned char *padded = (unsigned char *)lua_newuserdata(L, totalLen);
    memcpy(padded, data, datalen);
    memset(padded + datalen, (unsigned char)padLen, padLen);

    /* Initialize Rijndael for encryption */
    Rijndael rijn;
    RijndaelCreate(&rijn);
    int ret = RijndaelInit(&rijn,
        RIJNDAEL_Direction_Mode_CBC,
        RIJNDAEL_Direction_Encrypt,
        (UINT8 *)key, keyType,
        (UINT8 *)iv);

    if (ret != RIJNDAEL_SUCCESS)
        return luaL_error(L, "aes.encrypt: init failed (error %d)", ret);

    /* Encrypt */
    unsigned char *outbuf = (unsigned char *)lua_newuserdata(L, totalLen);
    ret = RijndaelBlockEncrypt(&rijn, padded, (int)(totalLen * 8), outbuf);
    if (ret < 0)
        return luaL_error(L, "aes.encrypt: encryption failed (error %d)", ret);

    lua_pushlstring(L, (const char *)outbuf, totalLen);
    return 1;
}

/*
** aes.decrypt(key, iv, ciphertext) -> plaintext
**
** AES-CBC decryption with PKCS7 unpadding.
** Returns the decrypted binary string.
*/
static int LUA_AES_Decrypt(lua_State *L)
{
    const char *key  = luaL_checkstring(L, 1);
    const char *iv   = luaL_checkstring(L, 2);
    const char *data = luaL_checkstring(L, 3);
    
    size_t keylen = lua_rawlen(L, 1);
    size_t ivlen  = lua_rawlen(L, 2);
    size_t datalen= lua_rawlen(L, 3);

    int keyType = get_key_type(keylen);
    if (keyType < 0)
        return luaL_error(L, "aes.decrypt: key must be 16, 24, or 32 bytes (got %d)", (int)keylen);
    if (ivlen != 16)
        return luaL_error(L, "aes.decrypt: iv must be 16 bytes (got %d)", (int)ivlen);
    if (datalen == 0 || datalen % 16 != 0)
        return luaL_error(L, "aes.decrypt: ciphertext length must be a multiple of 16 (got %d)", (int)datalen);

    /* Initialize Rijndael for decryption */
    Rijndael rijn;
    RijndaelCreate(&rijn);
    int ret = RijndaelInit(&rijn,
        RIJNDAEL_Direction_Mode_CBC,
        RIJNDAEL_Direction_Decrypt,
        (UINT8 *)key, keyType,
        (UINT8 *)iv);

    if (ret != RIJNDAEL_SUCCESS)
        return luaL_error(L, "aes.decrypt: init failed (error %d)", ret);

    /* Decrypt */
    unsigned char *decbuf = (unsigned char *)lua_newuserdata(L, datalen);

    ret = RijndaelBlockDecrypt(&rijn, (UINT8 *)data, (int)(datalen * 8), decbuf);
    if (ret < 0)
        return luaL_error(L, "aes.decrypt: decryption failed (error %d)", ret);

    /* PKCS7 unpadding */
    unsigned char padVal = decbuf[datalen - 1];
    
    if (padVal < 1 || padVal > 16)
        return luaL_error(L, "aes.decrypt: invalid PKCS7 padding value (%d)", padVal);

    /* Verify all padding bytes */
    for (int i = 0; i < padVal; i++)
    {
        if (decbuf[datalen - 1 - i] != padVal)
            return luaL_error(L, "aes.decrypt: corrupted PKCS7 padding");
    }

    size_t plainLen = datalen - padVal;
    lua_pushlstring(L, (const char *)decbuf, plainLen);
    return 1;
}

static const luaL_Reg aes_funcs[] = {
    {"encrypt", LUA_AES_Encrypt},
    {"decrypt", LUA_AES_Decrypt},
    {NULL, NULL}
};

int luaopen_aes(lua_State *L)
{
    luaL_newlib(L, aes_funcs);
    return 1;
}
