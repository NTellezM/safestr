#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "safestr.h"
#include "safestr_arena.h"

#define CAMPOS 200000

static double ahora(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* Trabajo realista sobre el resultado: hash de cada campo.
   Toca todos los bytes, que es donde se paga la localidad. */
static uint64_t recorrer(const SafeStringList* l)
{
    uint64_t h = 0;
    for (size_t i = 0; i < l->count; i++)
        h ^= ss_hash64(&l->items[i]);
    return h;
}

/* Ensucia el heap: muchos bloques chicos, se libera uno de cada dos.
   Deja huecos por todas partes, como un proceso que lleva horas corriendo. */
static void **huecos;
static size_t n_huecos;

static void fragmentar(size_t n)
{
    huecos = malloc(n * sizeof *huecos);
    n_huecos = 0;
    for (size_t i = 0; i < n; i++)
    {
        void *p = malloc(48 + (i % 7) * 16);
        ((char*) p)[0] = 1;
        huecos[n_huecos++] = p;
    }
    for (size_t i = 0; i < n_huecos; i += 2)
    {
        free(huecos[i]);
        huecos[i] = NULL;
    }
}

static void limpiar_huecos(void)
{
    for (size_t i = 0; i < n_huecos; i++) free(huecos[i]);
    free(huecos);
    huecos = NULL; n_huecos = 0;
}

int main(void)
{
    /* --- texto de entrada: CAMPOS campos separados por coma --- */
    SafeString csv = ss_new();
    for (int i = 0; i < CAMPOS; i++)
    {
        if (i) ss_append_char(&csv, ',');
        ss_appendf(&csv, "PAS%02d-C%02d-N%d", i % 40, i % 60, i % 4);
    }
    printf("entrada: %zu campos, %zu bytes\n\n", (size_t) CAMPOS, ss_len(&csv));

    double t0, t_split, t_rec;
    uint64_t h;

    /* ---------------- A: malloc, heap limpio ---------------- */
    t0 = ahora();
    SafeStringList a = ss_split(&csv, ",");
    t_split = ahora() - t0;
    t0 = ahora();
    h = recorrer(&a);
    t_rec = ahora() - t0;
    printf("A  malloc, heap limpio      split %.4f s   recorrido %.4f s\n", t_split, t_rec);
    ss_list_free(&a);

    /* ---------------- B: malloc, heap fragmentado ---------------- */
    fragmentar(600000);
    t0 = ahora();
    SafeStringList b = ss_split(&csv, ",");
    t_split = ahora() - t0;
    t0 = ahora();
    h ^= recorrer(&b);
    t_rec = ahora() - t0;
    printf("B  malloc, heap fragmentado split %.4f s   recorrido %.4f s\n", t_split, t_rec);
    ss_list_free(&b);

    /* ---------------- C: arena, mismo heap fragmentado ---------------- */
    SsArena ar;
    ss_arena_init(&ar, 4u << 20);
    ss_arena_activar(&ar);

    t0 = ahora();
    SafeStringList c = ss_split(&csv, ",");
    t_split = ahora() - t0;
    t0 = ahora();
    h ^= recorrer(&c);
    t_rec = ahora() - t0;
    printf("C  arena, heap fragmentado  split %.4f s   recorrido %.4f s\n", t_split, t_rec);

    ss_list_free(&c);
    ss_arena_desactivar();

    printf("\narena: %zu bloques, %zu trozos, %zu KB pedidos / %zu KB reservados\n",
           ar.n_bloques, ar.n_trozos,
           ar.bytes_pedidos / 1024, ar.bytes_reservados / 1024);
    printf("       crecimientos en sitio %zu, copias %zu\n",
           ar.n_crecimientos_en_sitio, ar.n_copias);
    printf("frees: malloc %d   arena 1\n", CAMPOS + 1);

    ss_arena_free(&ar);
    limpiar_huecos();
    ss_free(&csv);
    printf("\n(centinela %llx)\n", (unsigned long long) h);
    return 0;
}
