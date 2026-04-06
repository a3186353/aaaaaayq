#include "sdl_proxy.h"
#include "SDL_list.h"
#include "map.h"

void SDL_ListAdd(SDL_ListNode** list, void* data)
{
    if (!list)
        return;

    SDL_ListNode* node = (SDL_ListNode*)MAP_MALLOC(sizeof(SDL_ListNode));
    if (!node)
        return;

    node->data = data;
    node->next = *list;
    *list = node;
}

int SDL_ListRemove(SDL_ListNode** list, void* data)
{
    if (!list || !*list)
        return 0;

    SDL_ListNode* prev = NULL;
    SDL_ListNode* cur = *list;

    while (cur)
    {
        if (cur->data == data)
        {
            if (prev)
                prev->next = cur->next;
            else
                *list = cur->next;

            MAP_FREE(cur);
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }

    return 0;
}

void SDL_ListClear(SDL_ListNode** list)
{
    if (!list)
        return;

    SDL_ListNode* cur = *list;
    while (cur)
    {
        SDL_ListNode* next = cur->next;
        MAP_FREE(cur);
        cur = next;
    }
    *list = NULL;
}
