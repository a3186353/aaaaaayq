
#include "Astart.h"
#include "DLinkList.h"
#include <string.h>
#include <limits.h>
//测距
unsigned int CalcDistance(POINT p1, POINT p2)
{
    unsigned int x, y;
    x = (p1.x > p2.x) ? p1.x - p2.x : p2.x - p1.x;
    y = (p1.y > p2.y) ? p1.y - p2.y : p2.y - p1.y;

    return (x + y) * 10;
}

Map* WINAPI MapCreate(unsigned int width, unsigned int height, unsigned char* data)
{
    if (width == 0 || height == 0)
        return NULL;
    if (width > UINT_MAX / height)
        return NULL;

    Map* map = (Map*)calloc(1, sizeof(Map));
    if (!map)
        return NULL;

    map->width = width;
    map->height = height;
    map->data = data;
    map->size = width * height;
    map->list = new DLinkList(map->size);

    if (!map->list)
    {
        free(map);
        map = NULL;
    }
    if (!(map->node = (GRID_NODE*)calloc(map->size, sizeof(GRID_NODE))))
    {
        delete map->list;
        free(map);
        map = NULL;
    }

    return map;
}

void WINAPI MapDestroy(Map* map)
{
    delete map->list;
    free(map->node);
    free(map);
}

bool WINAPI FindPath(Map* map, POINT* pStart, POINT* pEnd, bool mode)
{
    if (!map || !map->data || !map->node || !map->list || !pStart || !pEnd)
        return false;

    /* 终点开始查找 */
    POINT start = *pEnd;
    POINT end = *pStart;

    unsigned char* data = map->data;
    unsigned int width = map->width;
    unsigned int height = map->height;
    unsigned int size = map->size;

    if ((unsigned int)start.x >= width || (unsigned int)start.y >= height ||
        (unsigned int)end.x >= width || (unsigned int)end.y >= height)
        return false;

    DLinkList* list = map->list;
    list->Clean();

    bool isOK = false;
    static int x[] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    static int y[] = { 0, -1, -1, -1, 0, 1, 1, 1 };

    GRID_NODE* node = map->node;
    memset(node, 0, size * sizeof(GRID_NODE));

    unsigned int index = width * start.y + start.x;
    if (index >= size)
        return false;
    if (data[index] != OK || data[width * end.y + end.x] != OK)
        return false;

    node[index].start = CHECK;
    node[index].end = CalcDistance(start, end); //距终点
    node[index].total = node[index].end;        //总距离

    list->Insert((void*)index, node[index].total);

    while (list->Get_length() != 0 && isOK == false)
    {
        /* 在待检链表中取出 F(总距离) 最小的节点, 并将其选为当前点 */
        list->Seek_tail();                                       //到尾节点
        size_t now_index = (size_t)list->Get_data(); //当前索引
        list->Delete();
        node[now_index].state = CLOSE;

        POINT cur_pos =
        {
            (long)(now_index % width),
            (long)(now_index / width) };

        /* 遍历当前坐标的八个相邻坐标 */
        for (int i = 0; i < 8; i++)
        {
            if (mode && i % 2)
                continue;
            POINT beside_pos = //相邻坐标
            {
                cur_pos.x + x[i],
                cur_pos.y + y[i] };

            /* 检查坐标有效性 */
            if (beside_pos.x < 0 || (unsigned int)beside_pos.x >= width || beside_pos.y < 0 || (unsigned int)beside_pos.y >= height)
                continue;

            unsigned int beside_index = width * beside_pos.y + beside_pos.x; //相邻索引
            if (data[beside_index] != OK)
                continue;
            switch (i)
            {
            case 1:
                if (data[beside_index + 1] != OK || data[beside_index + width] != OK)
                    continue;
                break;
            case 3:
                if (data[beside_index - 1] != OK || data[beside_index + width] != OK)
                    continue;
                break;
            case 5:
                if (data[beside_index - 1] != OK || data[beside_index - width] != OK)
                    continue;
                break;
            case 7:
                if (data[beside_index + 1] != OK || data[beside_index - width] != OK)
                    continue;
                break;
            }

            /* 检查是否已到达终点 */
            if (beside_pos.x == end.x && beside_pos.y == end.y)
            {
                isOK = true;
                node[beside_index].parent = cur_pos;
                break;
            }

            unsigned int g = ((i % 2) ? 14 : 10) + abs(data[now_index] - data[beside_index]);

            if (node[beside_index].state == UNKNOWN)
            {
                /* 放入待检链表中 */
                node[beside_index].state = CHECK;                                             //待检
                node[beside_index].start = node[now_index].start + g;                         //距起点
                node[beside_index].end = CalcDistance(beside_pos, end);                       //距终点
                node[beside_index].total = node[beside_index].start + node[beside_index].end; //总距离
                node[beside_index].parent = cur_pos;                                          //父坐标

                unsigned int total = node[beside_index].total;
                if (list->Get_length() == 0)
                    list->Insert((void*)beside_index, node[beside_index].total);
                else
                {
                    list->Seek_tail(); //到尾节点
                    while (true)
                    {
                        if (list->Get_key() >= total)
                        {
                            list->Insert((void*)beside_index, node[beside_index].total);
                            break;
                        }
                        if (!list->Seek_pre()) //到父节点
                        {
                            list->Insert((void*)beside_index, node[beside_index].total, INSERT_PRE);
                            break;
                        }
                    }
                }
            }
            else if (node[beside_index].state == CHECK)
            {
                /* 如果将当前点设为父 G(距起点) 值是否更小 */
                if (node[beside_index].start > node[now_index].start + g)
                {
                    node[beside_index].parent = cur_pos;                                          //父坐标
                    node[beside_index].start = node[now_index].start + g;                         //距起点
                    node[beside_index].total = node[beside_index].start + node[beside_index].end; //总距离
                }
            }
        }
    }

    return isOK;
}

bool WINAPI NextPath(Map* map, POINT* pos)
{
    if (!map || !pos)
        return false;
    unsigned int index = pos->y * map->width + pos->x;
    if (index < map->size)
    {
        *pos = map->node[index].parent;
        return true;
    }
    return false;
}
