#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "safestr.h"
#include "safestr_arena.h"
static double ahora(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec/1e9;}
static void* ingenua_realloc(void* ctx, void* p, size_t n){
    SsArena* ar=(SsArena*)ctx;
    if(!p) return ss_arena_pedir_(ar,n);
    size_t v=*(size_t*)((char*)p-SS_ARENA_CABECERA);
    void* nu=ss_arena_pedir_(ar,n); if(!nu) return NULL;
    memcpy(nu,p,v<n?v:n); ar->n_copias++; return nu;}
static void ingenua_liberar(void* c,void* p){(void)c;(void)p;}

#define OBJETIVO (16u<<20)      /* 16 MB, muy por encima de SS_MAX_PREALLOC */
#define PASO 4096

static void correr(const char* et, SsArena* ar, int ingenua){
    if(ingenua) ss_set_allocator_ex(ingenua_realloc, ingenua_liberar, ar);
    else ss_arena_activar(ar);
    char buf[PASO]; memset(buf,'x',sizeof buf);
    double t0=ahora();
    SafeString s=ss_new();
    for(size_t i=0;i<OBJETIVO/PASO;i++) ss_append_len(&s,buf,PASO);
    double t=ahora()-t0; size_t len=ss_len(&s);
    ss_free(&s); ss_arena_desactivar();
    printf("%-30s %.4f s  len %zu MB  reservado %zu MB  copias %zu\n",
        et,t,len>>20,ar->bytes_reservados>>20,ar->n_copias);
}
int main(void){
    printf("crecer un string hasta %u MB en pasos de %u B\n\n",OBJETIVO>>20,PASO);
    char buf[PASO]; memset(buf,'x',sizeof buf);
    double t0=ahora(); SafeString s=ss_new();
    for(size_t i=0;i<OBJETIVO/PASO;i++) ss_append_len(&s,buf,PASO);
    printf("%-30s %.4f s\n","malloc/realloc de C",ahora()-t0); ss_free(&s);
    SsArena a; ss_arena_init(&a,4u<<20); correr("arena ingenua (copia)",&a,1); ss_arena_free(&a);
    SsArena b; ss_arena_init(&b,4u<<20); correr("arena con crecim. en sitio",&b,0); ss_arena_free(&b);
    return 0;}
