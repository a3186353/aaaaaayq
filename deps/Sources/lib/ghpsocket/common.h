#pragma once
#include "lua.hpp"
#include "HPSocket.h"

#define CASE(X) case X: lua_pushstring(L,#X);break;
//定义自己的互斥
#include "SDL_mutex.h"
#define HP_mutex SDL_mutex
#define HP_LockMutex SDL_LockMutex
#define HP_UnlockMutex SDL_UnlockMutex

class HP_Base
{
public:
	HP_Base(lua_State* L){
		m_self = luaL_ref(L,LUA_REGISTRYINDEX);
		m_L = L;
	};

	~HP_Base(){
		luaL_unref(m_L,LUA_REGISTRYINDEX,m_self);
	};

	int GetCallBack(const char* name){
		m_mutex = *(HP_mutex**)lua_getextraspace(m_L);
		if (HP_LockMutex(m_mutex)==0)
		{
			L = lua_newthread(m_L);
			lua_rawgeti(L, LUA_REGISTRYINDEX, m_self);
			lua_getfield(L, -1, name);
			lua_insert(L, 1);//self
			return 1;
		}
		return 0;
	};

	EnHandleResult GetResult(){
		Uint32 r = (Uint32)lua_tointeger(L,-1);
		lua_pop(m_L,1);//协程
		HP_UnlockMutex(m_mutex);
		return EnHandleResult(r);
	};

	EnHttpParseResult GetResult2(){
		Uint32 r = (Uint32)lua_tointeger(L,-1);
		lua_pop(m_L,1);//协程
		HP_UnlockMutex(m_mutex);
		return EnHttpParseResult(r);
	};

	HP_mutex *m_mutex;
	lua_State* m_L;//主状态机
	int m_self;
	lua_State* L;//协程
};
