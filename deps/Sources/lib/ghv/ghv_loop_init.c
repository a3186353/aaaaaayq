/**
 * ghv_loop_init.c - Helper to initialize hloop for polling mode
 * Must be compiled as C (not C++) to safely include hevent.h
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "hloop.h"
#include "hevent.h"

/* Set loop status to RUNNING and tid to current thread
 * This is needed because hloop_process_events() returns 0
 * if status == HLOOP_STATUS_STOP (hloop.c line 170)
 */
void ghv_init_loop_for_polling(hloop_t* loop) {
    loop->status = HLOOP_STATUS_RUNNING;
#ifdef _WIN32
    loop->tid = (long)GetCurrentThreadId();
#else
    loop->tid = (long)pthread_self();
#endif
}
