#ifndef _ASTART_H
#define _ASTART_H

#ifdef _WIN32
#include <Windows.h>
#else
#define WINAPI
typedef struct tag_POINT
{
    long x;
    long y;
} POINT;
#endif // _WIN32

#include "DLinkList.h"

enum GRID_ATTR //格子状态
{
    OK = 0,
    UNKNOWN = 0,
    CHECK,
    CLOSE
};

typedef struct tag_GRID_NODE
{
    unsigned char state; //状态
    unsigned int total;  //总距离
    unsigned int start;  //距起点
    unsigned int end;    //距终点
    POINT parent;        //父坐标
} GRID_NODE;

typedef struct _tag_Map
{
    unsigned char *data; //数据
    GRID_NODE *node;     //节点
    unsigned int width;  //宽度
    unsigned int height; //高度
    unsigned int size;   //大小
    DLinkList *list;     //优化list使用
} Map;

Map *WINAPI MapCreate(unsigned int width, unsigned int height, unsigned char *data);
void WINAPI MapDestroy(Map *map);
bool WINAPI FindPath(Map *map, POINT *start, POINT *end, bool mode = 0);
bool WINAPI NextPath(Map *map, POINT *pos);

#endif
