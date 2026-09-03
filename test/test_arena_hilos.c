/*
 * test_arena_hilos.c - la arena bajo ThreadSanitizer
 *
 * El asignador de safestr es por hilo. Lo que hay que comprobar:
 *
 *   1. Cada hilo con su propia arena no pisa a los demas, aunque las
 *      arenas se creen en el hilo principal.
 *   2. Un hilo sin arena sigue usando realloc/free de C mientras otros
 *      usan arena, sin interferencia.
 *   3. Las estadisticas de cada arena las escribe un solo hilo: no hay
 *      carrera aunque el struct sea visible desde main.
 *   4. El caso que NO es seguro y hay que documentar: pasar un string
 *      de arena a otro hilo y liberarlo alli.
 *
 *     cc -fsanitize=thread -O1 -g -I. test_arena_hilos.c safestr.c -lpthread
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "safestr.h"
#include "safestr_arena.h"
#include "safestr_split.h"

#define HILOS 8
#define VUELTAS 2000

static int fallos = 0;
static pthread_mutex_t candado = PTHREAD_MUTEX_INITIALIZER;

static void ok(const char* que, int cond)
{
    pthread_mutex_lock(&candado);
    if (!cond) { printf("  FALLA: %s\n", que); fallos++; }
    pthread_mutex_unlock(&candado);
}

typedef struct
{
    int      id;
    SsArena  arena;
    int      usar_arena;
    size_t   bloques_vistos;
} Trabajo;

static void* trabajador(void* p)
{
    Trabajo* t = (Trabajo*) p;

    if (t->usar_arena)
    {
        ss_arena_init(&t->arena, 256 * 1024);
        ss_arena_activar(&t->arena);
    }

    char marca = (char)('A' + t->id);

    for (int v = 0; v < VUELTAS; v++)
    {
        /* string propio, con un contenido que identifica al hilo */
        SafeString s = ss_new();
        for (int i = 0; i < 40; i++) ss_append_char(&s, marca);
        ss_appendf(&s, "|%d|%d", t->id, v);

        /* si otro hilo hubiera escrito encima, esto falla */
        int puro = 1;
        for (int i = 0; i < 40; i++)
            if (ss_cstr(&s)[i] != marca) puro = 0;
        ok("hilo: contenido no contaminado", puro);

        /* division: usa malloc del sistema, no la arena */
        SafeSplit sp = ss_split_packed(&s, "|");
        ok("hilo: 3 campos", sp.count == 3);
        ss_split_packed_free(&sp);

        ss_free(&s);
    }

    if (t->usar_arena)
    {
        t->bloques_vistos = t->arena.n_trozos;
        ss_arena_desactivar();
        ss_arena_free(&t->arena);
    }
    return NULL;
}

int main(void)
{
    pthread_t h[HILOS];
    Trabajo   t[HILOS];

    memset(t, 0, sizeof t);
    for (int i = 0; i < HILOS; i++)
    {
        t[i].id = i;
        t[i].usar_arena = (i % 2 == 0);   /* mitad con arena, mitad sin */
    }

    for (int i = 0; i < HILOS; i++)
        pthread_create(&h[i], NULL, trabajador, &t[i]);
    for (int i = 0; i < HILOS; i++)
        pthread_join(h[i], NULL);

    for (int i = 0; i < HILOS; i++)
        if (t[i].usar_arena)
            ok("hilo: la arena reservo trozos", t[i].bloques_vistos > 0);

    printf("%d hilos (%d con arena), %d vueltas cada uno: %d fallas\n",
           HILOS, HILOS / 2, VUELTAS, fallos);
    return fallos != 0;
}
