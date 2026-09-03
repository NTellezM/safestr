#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "safestr.h"
#include "safestr_arena.h"
#include "safestr_split.h"

#define CAMPOS 200000

static double ahora(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static int fallos = 0;
static void ok(const char* que, int cond)
{
    if (!cond) { printf("  FALLA: %s\n", que); fallos++; }
}

/* ------------------------------------------------------------------ */
/* 1. Correctitud: mismo resultado que ss_split, caso por caso         */
/* ------------------------------------------------------------------ */
static void comparar(const char* entrada, const char* sep)
{
    SafeString s = ss_from(entrada);
    SafeStringList a = ss_split(&s, sep);
    SafeSplit     b = ss_split_packed(&s, sep);

    ok("mismo numero de partes", a.count == b.count);
    for (size_t i = 0; i < a.count && i < b.count; i++)
    {
        SafeView va = ss_view(&a.items[i]);
        SafeView vb = ss_packed_view(&b, i);
        ok("mismo contenido", sv_equals(va, vb));
        ok("terminado en 0", ss_packed_cstr(&b, i)[vb.len] == '\0');
    }
    ss_list_free(&a);
    ss_split_packed_free(&b);
    ss_free(&s);
}

static void tests(void)
{
    printf("== correctitud ==\n");
    comparar("a,b,c", ",");
    comparar("a,,b", ",");          /* vacio en el medio  */
    comparar("a,", ",");            /* vacio al final     */
    comparar(",a", ",");            /* vacio al principio */
    comparar("", ",");              /* entrada vacia      */
    comparar("sin separador", ",");
    comparar("a::b::c", "::");      /* separador de 2 bytes */
    comparar("aXXbXXXc", "XX");     /* solapamiento         */
    comparar(",,,", ",");

    /* bytes 0 dentro del contenido */
    SafeString bin = ss_from_len("a\0b,c\0d", 7);
    SafeSplit p = ss_split_packed(&bin, ",");
    ok("binario: 2 partes", p.count == 2);
    ok("binario: largo 3", ss_packed_view(&p, 0).len == 3);
    ok("binario: byte 0 intacto", ss_packed_view(&p, 0).ptr[1] == '\0');
    ss_split_packed_free(&p);
    ss_free(&bin);

    /* acceso fuera de rango */
    SafeString s = ss_from("a,b");
    SafeSplit q = ss_split_packed(&s, ",");
    ok("fuera de rango -> cadena vacia", ss_packed_cstr(&q, 99)[0] == '\0');
    ok("fuera de rango -> vista nula", ss_packed_view(&q, 99).ptr == NULL);
    SafeString tomado = ss_packed_take(&q, 1);
    ok("take copia", ss_equals_cstr(&tomado, "b"));
    ss_free(&tomado);
    ss_split_packed_free(&q);
    ss_free(&s);

    /* separador invalido */
    SafeString t = ss_from("a,b");
    SafeSplit bad = ss_split_packed(&t, "");
    ok("separador vacio -> error", bad.error);
    ss_split_packed_free(&bad);
    ss_free(&t);

    printf(fallos == 0 ? "  todo en orden\n\n" : "  %d fallas\n\n", fallos);
}

/* ------------------------------------------------------------------ */
/* 2. Rendimiento                                                      */
/* ------------------------------------------------------------------ */
static void** huecos; static size_t n_huecos;
static void fragmentar(size_t n)
{
    huecos = malloc(n * sizeof *huecos); n_huecos = 0;
    for (size_t i = 0; i < n; i++)
    { void* p = malloc(48 + (i % 7) * 16); ((char*) p)[0] = 1; huecos[n_huecos++] = p; }
    for (size_t i = 0; i < n_huecos; i += 2) { free(huecos[i]); huecos[i] = NULL; }
}
static void limpiar(void)
{ for (size_t i = 0; i < n_huecos; i++) free(huecos[i]); free(huecos); }

int main(void)
{
    tests();

    SafeString csv = ss_new();
    for (int i = 0; i < CAMPOS; i++)
    {
        if (i) ss_append_char(&csv, ',');
        ss_appendf(&csv, "PAS%02d-C%02d-N%d", i % 40, i % 60, i % 4);
    }
    printf("== rendimiento: %d campos, %zu bytes ==\n\n", CAMPOS, ss_len(&csv));
    printf("%-34s %8s %11s %10s\n", "", "split", "recorrido", "reservas");

    double t0, ts, tr; uint64_t h = 0;

    #define REC_LIST(l) do { for (size_t i = 0; i < (l).count; i++) h ^= ss_hash64(&(l).items[i]); } while (0)
    #define REC_PACK(p) do { for (size_t i = 0; i < (p).count; i++) h ^= sv_hash64((p).items[i]); } while (0)

    /* A: ss_split, heap limpio */
    t0 = ahora(); SafeStringList a = ss_split(&csv, ","); ts = ahora() - t0;
    t0 = ahora(); REC_LIST(a);                            tr = ahora() - t0;
    printf("%-34s %7.4fs %10.4fs %10d\n", "ss_split, heap limpio", ts, tr, CAMPOS + 1);
    ss_list_free(&a);

    /* D: packed, heap limpio */
    t0 = ahora(); SafeSplit d = ss_split_packed(&csv, ","); ts = ahora() - t0;
    t0 = ahora(); REC_PACK(d);                              tr = ahora() - t0;
    printf("%-34s %7.4fs %10.4fs %10d\n", "ss_split_packed, heap limpio", ts, tr, 1);
    ss_split_packed_free(&d);

    fragmentar(600000);

    /* B: ss_split, heap sucio */
    t0 = ahora(); SafeStringList b = ss_split(&csv, ","); ts = ahora() - t0;
    t0 = ahora(); REC_LIST(b);                            tr = ahora() - t0;
    printf("%-34s %7.4fs %10.4fs %10d\n", "ss_split, heap fragmentado", ts, tr, CAMPOS + 1);
    ss_list_free(&b);

    /* C: ss_split con arena, heap sucio */
    SsArena ar; ss_arena_init(&ar, 4u << 20); ss_arena_activar(&ar);
    t0 = ahora(); SafeStringList c = ss_split(&csv, ","); ts = ahora() - t0;
    t0 = ahora(); REC_LIST(c);                            tr = ahora() - t0;
    printf("%-34s %7.4fs %10.4fs %10d\n", "ss_split + arena, fragmentado", ts, tr, 1);
    ss_list_free(&c); ss_arena_desactivar(); ss_arena_free(&ar);

    /* E: packed, heap sucio */
    t0 = ahora(); SafeSplit e = ss_split_packed(&csv, ","); ts = ahora() - t0;
    t0 = ahora(); REC_PACK(e);                              tr = ahora() - t0;
    printf("%-34s %7.4fs %10.4fs %10d\n", "ss_split_packed, fragmentado", ts, tr, 1);

    printf("\nmemoria del resultado:\n");
    printf("  ss_split        %zu campos x 32 B de SafeString + %d bloques del heap\n",
           (size_t) CAMPOS, CAMPOS);
    printf("  ss_split_packed %zu campos x 16 B de vista + texto, en 1 bloque de %zu KB\n",
           (size_t) CAMPOS,
           (CAMPOS * sizeof(SafeView) + ss_len(&csv) + CAMPOS + 1) / 1024);

    ss_split_packed_free(&e);
    limpiar(); ss_free(&csv);
    printf("\n(centinela %llx)\n", (unsigned long long) h);
    return fallos ? 1 : 0;
}
