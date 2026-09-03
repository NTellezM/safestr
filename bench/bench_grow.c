#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "safestr.h"
#include "safestr_arena.h"

static double ahora(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* Arena INGENUA: identica, pero sin el camino de crecimiento en sitio.
   Cada realloc pide bloque nuevo y copia. Es lo que uno escribe la
   primera vez que implementa una arena. */
static void* arena_ingenua_realloc(void* ctx, void* p, size_t n)
{
    SsArena* ar = (SsArena*) ctx;
    if (p == NULL) return ss_arena_pedir_(ar, n);
    size_t viejo = *(size_t*)((char*) p - SS_ARENA_CABECERA);
    void* nuevo = ss_arena_pedir_(ar, n);
    if (!nuevo) return NULL;
    memcpy(nuevo, p, (viejo < n) ? viejo : n);
    ar->n_copias++;
    return nuevo;
}
static void arena_ingenua_liberar(void* ctx, void* p) { (void) ctx; (void) p; }

#define N 400000   /* caracteres a agregar de a uno */

static double medir(SsArena* ar, const char* etiqueta, int ingenua)
{
    ss_arena_reset(ar);
    if (ingenua)
        ss_set_allocator_ex(arena_ingenua_realloc, arena_ingenua_liberar, ar);
    else
        ss_arena_activar(ar);

    double t0 = ahora();
    SafeString s = ss_new();
    for (int i = 0; i < N; i++)
        ss_append_char(&s, 'a' + (i % 26));
    double t = ahora() - t0;
    size_t len = ss_len(&s);
    ss_free(&s);

    ss_arena_desactivar();
    printf("%-28s %.4f s   len %zu   reservado %zu KB   copias %zu\n",
           etiqueta, t, len, ar->bytes_reservados / 1024, ar->n_copias);
    return t;
}

int main(void)
{
    printf("%d appends de un caracter sobre un mismo string\n\n", N);

    /* malloc normal, como referencia */
    double t0 = ahora();
    SafeString s = ss_new();
    for (int i = 0; i < N; i++) ss_append_char(&s, 'a' + (i % 26));
    double t_malloc = ahora() - t0;
    ss_free(&s);
    printf("%-28s %.4f s\n", "malloc/realloc de C", t_malloc);

    SsArena a;
    ss_arena_init(&a, 1u << 20);
    double t_ing = medir(&a, "arena ingenua (copia)", 1);
    ss_arena_free(&a);

    SsArena b;
    ss_arena_init(&b, 1u << 20);
    double t_sitio = medir(&b, "arena con crecim. en sitio", 0);
    ss_arena_free(&b);

    printf("\nla arena ingenua es %.1fx mas lenta que la que crece en sitio\n",
           t_ing / t_sitio);
    return 0;
}
