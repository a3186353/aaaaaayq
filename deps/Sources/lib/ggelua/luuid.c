#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include <stdio.h>

#ifdef _WIN32
#include <objbase.h>
#else
//#include <uuid/uuid.h>
#endif

static int
luuid(lua_State* L) {
	char buf[40];
#ifdef _WIN32
	GUID guid = { 0 };
	CoCreateGuid(&guid);
	sprintf(buf, "%.8X%.4X%.4X%.8X%.8X", guid.Data1, guid.Data2, guid.Data3, ((unsigned int*)(guid.Data4))[0], ((unsigned int*)(guid.Data4))[1]);
#else//TODO
	//	uuid_t guid;
	//	uuid_generate(guid);
	//	sprintf(buf,"%.8X%.8X%.8X%.8X",((unsigned int*)(guid))[0],((unsigned int*)(guid))[1],((unsigned int*)(guid))[2],((unsigned int*)(guid))[3]);
#endif
	lua_pushstring(L, buf);
	return 1;
}

LUALIB_API int luaopen_uuid(lua_State* L) {
	lua_pushcfunction(L, luuid);
	return 1;
}
