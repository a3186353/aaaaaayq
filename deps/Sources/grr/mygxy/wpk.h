#pragma once

#include "lua_proxy.h"
#include "sdl_proxy.h"

#define WPK_NAME "xyq_wpk"

typedef struct WPK_UserData WPK_UserData;

int WPK_NativeReadData(WPK_UserData* ud, unsigned int id, unsigned char** outData, size_t* outSize,
                       char* err, size_t errSize);

void WPK_PushPerfStats(lua_State* L);
