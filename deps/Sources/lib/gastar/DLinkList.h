#ifndef _DLINNKLIST_H
#define _DLINNKLIST_H

#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#include "memorypool.h"
}
#endif

typedef struct _tag_DLinkListNode *LPDLinkListNode;
typedef struct _tag_DLinkListNode
{
    LPDLinkListNode pre;  //父节点
    LPDLinkListNode next; //子节点
    void *data;           //节点值
    unsigned int key;     //键值
} DLinkListNode;

typedef struct _tag_DLinkListNodeHead
{
    LPDLinkListNode top;  //首节点
    LPDLinkListNode tail; //尾节点
    LPDLinkListNode cur;  //当前节点
    unsigned int length;  //节点数

} DLinkListNodeHead;

enum INSER_TYPE
{
    INSERT_PRE,
    INSERT_NEXT
}; //插入方向

class DLinkList
{
private:
    MemoryPool *m_heap;
    unsigned int m_max;
    DLinkListNodeHead m_head;

public:
    DLinkList(unsigned int max); //最大节点数
    ~DLinkList();

    bool Insert(void *data, unsigned int key, INSER_TYPE type = INSERT_NEXT);
    void Delete();
    void Delete(void *node);
    void Clean();
    void Seek(void *node);
    bool Seek_pre();  //到父节点
    bool Seek_next(); //到子节点
    void Seek_top();  //到首节点
    void Seek_tail(); //到尾节点
    void *Get_data();
    unsigned int Get_key();
    void *Get_node();
    unsigned int Get_length();
};

#endif
