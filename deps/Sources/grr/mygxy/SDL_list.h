#include "sdl_proxy.h"
#pragma once

typedef struct SDL_ListNode
{
    void* data;
    struct SDL_ListNode* next;
} SDL_ListNode;

void SDL_ListAdd(SDL_ListNode** list, void* data);
int SDL_ListRemove(SDL_ListNode** list, void* data);
void SDL_ListClear(SDL_ListNode** list);

