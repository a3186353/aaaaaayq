/*
 * IMG_stubs_android.c - Stub implementations for image formats
 * not available on Android (AVIF, JXL, WebP).
 * These are required because IMG.c unconditionally references them.
 */
#include "SDL_image.h"

/* AVIF stubs */
int IMG_InitAVIF(void) { return -1; }
void IMG_QuitAVIF(void) {}
int IMG_isAVIF(SDL_RWops *src) { (void)src; return 0; }
SDL_Surface *IMG_LoadAVIF_RW(SDL_RWops *src) { (void)src; return NULL; }

/* JXL stubs */
int IMG_InitJXL(void) { return -1; }
void IMG_QuitJXL(void) {}
int IMG_isJXL(SDL_RWops *src) { (void)src; return 0; }
SDL_Surface *IMG_LoadJXL_RW(SDL_RWops *src) { (void)src; return NULL; }

/* WebP stubs */
int IMG_InitWEBP(void) { return -1; }
void IMG_QuitWEBP(void) {}
int IMG_isWEBP(SDL_RWops *src) { (void)src; return 0; }
SDL_Surface *IMG_LoadWEBP_RW(SDL_RWops *src) { (void)src; return NULL; }
IMG_Animation *IMG_LoadWEBPAnimation_RW(SDL_RWops *src) { (void)src; return NULL; }
