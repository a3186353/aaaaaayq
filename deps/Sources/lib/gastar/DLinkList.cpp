#include "DLinkList.h"
//	构造函数
DLinkList::DLinkList(unsigned int max)
{
    m_max = max;
    m_heap = MemoryPoolInit(m_max * sizeof(DLinkListNode) * 3, m_max * sizeof(DLinkListNode) * 2);
    m_head.top = NULL;
    m_head.tail = NULL;
    m_head.cur = NULL;
    m_head.length = 0;
}

//	析构函数
DLinkList::~DLinkList()
{
    if (m_heap)
    {
        MemoryPoolClear(m_heap);
        MemoryPoolDestroy(m_heap);
    }
}

bool DLinkList::Insert(void *data, unsigned int key, INSER_TYPE type)
{
    LPDLinkListNode node = (LPDLinkListNode)MemoryPoolAlloc(m_heap, sizeof(DLinkListNode));

    if (!node)
    {
        return false;
    }

    node->data = data;
    node->key = key;

    //	将当前节点的父与子链接起来
    if (m_head.cur)
    {
        if (type == INSERT_NEXT)
        {
            node->pre = m_head.cur;
            node->next = m_head.cur->next;
            m_head.cur->next = node;
            if (node->next)
                node->next->pre = node;
            if (m_head.cur == m_head.tail)
                m_head.tail = node;
        }
        else
        {
            node->pre = m_head.cur->pre;
            node->next = m_head.cur;
            m_head.cur->pre = node;
            if (node->pre)
                node->pre->next = node;
            if (m_head.cur == m_head.top)
                m_head.top = node;
        }
    }
    else
    {
        //	链表是空的
        node->pre = NULL;
        node->next = NULL;
        m_head.top = node;
        m_head.tail = node;
    }

    m_head.cur = node;

    m_head.length++;

    return true;
}

void DLinkList::Delete()
{
    if (!m_head.cur)
        return;

    LPDLinkListNode cur = m_head.cur;

    if (--m_head.length == 0)
    {
        m_head.top = NULL;
        m_head.tail = NULL;
        m_head.cur = NULL;
    }
    else
    {
        //	将当前节点的父与子链接起来
        if (cur->pre)
            cur->pre->next = cur->next;

        if (cur->next)
        {
            cur->next->pre = cur->pre;
            //	更新表头
            m_head.cur = cur->next;
        }
        else
        {
            m_head.cur = cur->pre;
        }

        if (cur == m_head.top)
            m_head.top = cur->next;
        else if (cur == m_head.tail)
            m_head.tail = cur->pre;
    }

    MemoryPoolFree(m_heap, cur);
}

void DLinkList::Delete(void *node)
{
    Seek(node);
    Delete();
}

void DLinkList::Clean()
{
    m_head.top = NULL;
    m_head.tail = NULL;
    m_head.cur = NULL;
    m_head.length = 0;
    MemoryPoolClear(m_heap);
}

void DLinkList::Seek(void *node)
{
    m_head.cur = (LPDLinkListNode)node;
}

bool DLinkList::Seek_pre()
{
    if (m_head.cur)
    {
        if (m_head.cur->pre)
        {
            m_head.cur = m_head.cur->pre;
            return true;
        }
    }

    return false;
}

bool DLinkList::Seek_next()
{
    if (m_head.cur)
    {
        if (m_head.cur->next)
        {
            m_head.cur = m_head.cur->next;
            return true;
        }
    }

    return false;
}

void DLinkList::Seek_top()
{
    m_head.cur = m_head.top;
}

void DLinkList::Seek_tail()
{
    m_head.cur = m_head.tail;
}

void *DLinkList::Get_data()
{
    return m_head.cur ? m_head.cur->data : NULL;
}

unsigned int DLinkList::Get_key()
{
    return m_head.cur ? m_head.cur->key : 0;
}

void *DLinkList::Get_node()
{
    return m_head.cur;
}

unsigned int DLinkList::Get_length()
{
    return m_head.length;
}
