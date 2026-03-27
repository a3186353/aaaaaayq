#include "lua.h"
#include "lauxlib.h"
#include <string.h>
#include "luaopen.h"

static void string_adjust(const char *path,char *p)
{
	int i;
	strncpy(p,path,260);

	for (i=0;p[i];i++) {
		if (p[i]>='A' && p[i]<='Z') p[i]+='a'-'A';
		else if (p[i]=='/') p[i]='\\';
	}
}

unsigned int g_tohash(const char *path)
{
	unsigned int m[70];

	//x86 - 32 bits - Registers
	unsigned int eax, ebx, ecx, edx, edi, esi;
	unsigned long long  num = 0;

	unsigned int v;
	unsigned int i;
	if(!path)
		return 0;
	string_adjust(path,(char*)m);

	for (i=0;i<256/4 && m[i];i++) ;

	m[i++] = 0x9BE74448; //
	m[i++] = 0x66F42C48; //

	v = 0xF4FA8928; //

	edi = 0x7758B42B;
	esi = 0x37A8470E; //

	for (ecx = 0; ecx < i; ecx++)
	{
		ebx = 0x267B0B11; //
		v = (v << 1) | (v >> 0x1F);
		ebx ^= v;
		eax = m[ecx];
		esi ^= eax;
		edi ^= eax;
		edx = ebx;
		edx += edi;
		edx |= 0x02040801; // 
		edx &= 0xBFEF7FDF;//
		num = edx;
		num *= esi;
		eax = (unsigned int)num;
		edx = (unsigned int)(num >> 0x20);
		if (edx != 0)
			eax++;
		num = eax;
		num += edx;
		eax = (unsigned int)num;
		if (((unsigned int)(num >> 0x20)) != 0)
			eax++;
		edx = ebx;
		edx += esi;
		edx |= 0x00804021; //
		edx &= 0x7DFEFBFF; //
		esi = eax;
		num = edi;
		num *= edx;
		eax = (unsigned int)num;
		edx = (unsigned int)(num >> 0x20);
		num = edx;
		num += edx;
		edx = (unsigned int)num;
		if (((unsigned int)(num >> 0x20)) != 0)
			eax++;
		num = eax;
		num += edx;
		eax = (unsigned int)num;
		if (((unsigned int)(num >> 0x20)) != 0)
			eax += 2;
		edi = eax;
	}
	esi ^= edi;
	v = esi;
	return v;
}

int luaopen_tohash(lua_State *L){
	lua_pushinteger(L,g_tohash(luaL_checkstring(L,1)));
	return 1;
}