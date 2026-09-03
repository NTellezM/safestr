/*
 * test_extras.c - tests de safestr_arena.h y safestr_split.h
 *
 * Sin dependencias de POSIX ni de medicion de tiempo: compila con C99, C11,
 * C17 y como C++, para poder correrlo en toda la matriz del CI.
 *
 *     cc -std=c99 -Wall -Wextra -Wpedantic -I. test_extras.c safestr.c -o t && ./t
 */

#include <stdio.h>
#include <string.h>
#include "safestr.h"
#include "safestr_arena.h"
#include "safestr_split.h"

static int fallos = 0;
static int total  = 0;

static void ok(const char* que, int cond)
{
    total++;
    if (!cond) { printf("  FALLA: %s\n", que); fallos++; }
}

/* ================================================================== */
/* Arena                                                              */
/* ================================================================== */

static void test_arena_basico(void)
{
    SsArena a;
    ss_arena_init(&a, 64 * 1024);
    ss_arena_activar(&a);

    SafeString s = ss_from("hola");
    ok("arena: contenido", ss_equals_cstr(&s, "hola"));
    ss_append(&s, " mundo");
    ok("arena: append", ss_equals_cstr(&s, "hola mundo"));
    ok("arena: sin error", ss_ok(&s));
    ok("arena: registro bloques", a.n_bloques > 0);

    /* ss_free sobre el ultimo bloque entregado lo devuelve a la arena:
       el contador baja. Cualquier otro free es un no-op. */
    size_t antes = a.n_bloques;
    ss_free(&s);
    ok("arena: el ultimo bloque se recicla", a.n_bloques == antes - 1);
    ss_arena_desactivar();
    ss_arena_free(&a);
    ok("arena: free deja cap_trozo", a.cap_trozo == 64 * 1024);
}

static void test_arena_crecimiento_en_sitio(void)
{
    SsArena a;
    ss_arena_init(&a, 1 << 20);
    ss_arena_activar(&a);

    SafeString s = ss_new();
    for (int i = 0; i < 5000; i++) ss_append_char(&s, 'x');

    ok("crecimiento: largo correcto", ss_len(&s) == 5000);
    ok("crecimiento: contenido intacto", ss_cstr(&s)[4999] == 'x');
    ok("crecimiento: terminado en 0", ss_cstr(&s)[5000] == '\0');
    ok("crecimiento: uso el camino en sitio", a.n_crecimientos_en_sitio > 0);

    ss_free(&s);
    ss_arena_desactivar();
    ss_arena_free(&a);
}

/* Dos strings creciendo alternadamente: solo uno puede ser "el ultimo",
   asi que el otro obliga a copiar. Verifica que la copia no corrompe. */
static void test_arena_intercalado(void)
{
    SsArena a;
    ss_arena_init(&a, 1 << 20);
    ss_arena_activar(&a);

    SafeString x = ss_new(), y = ss_new();
    for (int i = 0; i < 2000; i++) { ss_append_char(&x, 'a'); ss_append_char(&y, 'b'); }

    ok("intercalado: largo x", ss_len(&x) == 2000);
    ok("intercalado: largo y", ss_len(&y) == 2000);

    int x_puro = 1, y_puro = 1;
    for (size_t i = 0; i < 2000; i++)
    {
        if (ss_cstr(&x)[i] != 'a') x_puro = 0;
        if (ss_cstr(&y)[i] != 'b') y_puro = 0;
    }
    ok("intercalado: x sin contaminar", x_puro);
    ok("intercalado: y sin contaminar", y_puro);

    ss_free(&x); ss_free(&y);
    ss_arena_desactivar();
    ss_arena_free(&a);
}

/* Un string que supera el umbral se muda a un bloque del sistema. */
static void test_arena_bloque_grande(void)
{
    SsArena a;
    ss_arena_init(&a, 64 * 1024);        /* umbral = 16 KB */
    ss_arena_activar(&a);

    SafeString s = ss_new();
    char trozo[1024];
    memset(trozo, 'z', sizeof trozo);
    for (int i = 0; i < 512; i++) ss_append_len(&s, trozo, sizeof trozo);

    ok("grande: largo", ss_len(&s) == 512 * 1024);
    ok("grande: se delego al sistema", a.n_grandes > 0);
    ok("grande: primer byte", ss_cstr(&s)[0] == 'z');
    ok("grande: ultimo byte", ss_cstr(&s)[512 * 1024 - 1] == 'z');
    ok("grande: la arena no exploto", a.bytes_reservados <= 4u * 64 * 1024);

    ss_free(&s);
    ok("grande: se libero", a.n_grandes == 0);

    ss_arena_desactivar();
    ss_arena_free(&a);
}

static void test_arena_reset(void)
{
    SsArena a;
    ss_arena_init(&a, 64 * 1024);
    ss_arena_activar(&a);

    for (int vuelta = 0; vuelta < 3; vuelta++)
    {
        SafeString s = ss_from("lote");
        ss_appendf(&s, " %d", vuelta);
        ok("reset: contenido por vuelta", ss_len(&s) == 6);
        /* sin ss_free: el reset se lleva todo */
        ss_arena_reset(&a);
    }
    ok("reset: reutiliza los trozos", a.n_trozos == 1);

    ss_arena_desactivar();
    ss_arena_free(&a);
}

/* Cambiar de asignador y volver al de C no debe dejar nada colgando. */
static void test_arena_ida_y_vuelta(void)
{
    SafeString antes = ss_from("heap");

    SsArena a;
    ss_arena_init(&a, 16 * 1024);
    ss_arena_activar(&a);
    SafeString dentro = ss_from("arena");
    ok("ida y vuelta: string de arena", ss_equals_cstr(&dentro, "arena"));
    ss_free(&dentro);
    ss_arena_desactivar();
    ss_arena_free(&a);

    ok("ida y vuelta: el de antes sigue vivo", ss_equals_cstr(&antes, "heap"));
    ss_append(&antes, " otra vez");
    ok("ida y vuelta: sigue usable", ss_equals_cstr(&antes, "heap otra vez"));
    ss_free(&antes);
}

/* ================================================================== */
/* Split empaquetado                                                  */
/* ================================================================== */

static void comparar_con_ss_split(const char* entrada, const char* sep)
{
    SafeString s = ss_from(entrada);
    SafeStringList a = ss_split(&s, sep);
    SafeSplit     b = ss_split_packed(&s, sep);

    ok("packed: mismo numero de partes", a.count == b.count);
    for (size_t i = 0; i < a.count && i < b.count; i++)
    {
        ok("packed: mismo contenido", sv_equals(ss_view(&a.items[i]), ss_packed_view(&b, i)));
        ok("packed: terminado en 0", ss_packed_cstr(&b, i)[ss_packed_view(&b, i).len] == '\0');
    }
    ss_list_free(&a);
    ss_split_packed_free(&b);
    ss_free(&s);
}

static void test_split_equivalencia(void)
{
    comparar_con_ss_split("a,b,c", ",");
    comparar_con_ss_split("a,,b", ",");
    comparar_con_ss_split("a,", ",");
    comparar_con_ss_split(",a", ",");
    comparar_con_ss_split(",,,", ",");
    comparar_con_ss_split("", ",");
    comparar_con_ss_split("sin separador", ",");
    comparar_con_ss_split("a::b::c", "::");
    comparar_con_ss_split("aXXbXXXc", "XX");
    comparar_con_ss_split("hola", "hola");
}

static void test_split_binario(void)
{
    SafeString bin = ss_from_len("a\0b,c\0d", 7);
    SafeSplit p = ss_split_packed(&bin, ",");
    ok("binario: 2 partes", p.count == 2);
    ok("binario: largo del primero", ss_packed_view(&p, 0).len == 3);
    ok("binario: byte 0 conservado", ss_packed_view(&p, 0).ptr[1] == '\0');
    ss_split_packed_free(&p);
    ss_free(&bin);
}

static void test_split_bordes(void)
{
    SafeString s = ss_from("a,b");

    SafeSplit q = ss_split_packed(&s, ",");
    ok("borde: fuera de rango da cadena vacia", ss_packed_cstr(&q, 99)[0] == '\0');
    ok("borde: fuera de rango da vista nula", ss_packed_view(&q, 99).ptr == NULL);
    ok("borde: count", ss_packed_count(&q) == 2);

    SafeString tomado = ss_packed_take(&q, 1);
    ok("borde: take copia", ss_equals_cstr(&tomado, "b"));
    ss_split_packed_free(&q);
    ok("borde: take sobrevive al free del bloque", ss_equals_cstr(&tomado, "b"));
    ss_free(&tomado);

    SafeSplit vacio = ss_split_packed(&s, "");
    ok("borde: separador vacio es error", vacio.error);
    ok("borde: error no reserva", vacio.bloque == NULL);
    ss_split_packed_free(&vacio);

    SafeSplit nulo = ss_split_packed(NULL, ",");
    ok("borde: NULL es error", nulo.error);
    ss_split_packed_free(&nulo);

    /* free doble e idempotente */
    SafeSplit d = ss_split_packed(&s, ",");
    ss_split_packed_free(&d);
    ss_split_packed_free(&d);
    ok("borde: free es idempotente", d.count == 0);

    ss_free(&s);
}

/* Los dos modulos juntos: con una arena activa, el bloque del split debe
   salir de la arena, no de malloc. */
static void test_split_con_arena_activa(void)
{
    SsArena a;
    ss_arena_init(&a, 64 * 1024);
    ss_arena_activar(&a);

    size_t antes = a.bytes_pedidos;

    SafeString s = ss_from("uno,dos,tres");
    SafeSplit p = ss_split_packed(&s, ",");
    ok("mixto: 3 partes", p.count == 3);
    ok("mixto: contenido", strcmp(ss_packed_cstr(&p, 2), "tres") == 0);
    ok("mixto: el bloque salio de la arena", a.bytes_pedidos > antes);

    /* liberar ANTES de desactivar: mismo asignador con que se reservo */
    ss_split_packed_free(&p);
    ss_free(&s);

    ss_arena_desactivar();
    ss_arena_free(&a);
}

/* Un CSV grande dentro de una arena: el bloque cruza el umbral y se delega
   al sistema, pero sigue siendo la arena quien lo administra. */
static void test_split_grande_en_arena(void)
{
    SsArena a;
    ss_arena_init(&a, 16 * 1024);    /* umbral = 4 KB */
    ss_arena_activar(&a);

    SafeString csv = ss_new();
    for (int i = 0; i < 2000; i++)
    {
        if (i) ss_append_char(&csv, ',');
        ss_appendf(&csv, "campo%04d", i);
    }

    SafeSplit p = ss_split_packed(&csv, ",");
    ok("split grande: 2000 partes", p.count == 2000);
    ok("split grande: primero", strcmp(ss_packed_cstr(&p, 0), "campo0000") == 0);
    ok("split grande: ultimo", strcmp(ss_packed_cstr(&p, 1999), "campo1999") == 0);
    ok("split grande: se delego", a.n_grandes > 0);

    ss_split_packed_free(&p);
    ss_free(&csv);
    ss_arena_desactivar();
    ss_arena_free(&a);
}

/* ================================================================== */

int main(void)
{
    test_arena_basico();
    test_arena_crecimiento_en_sitio();
    test_arena_intercalado();
    test_arena_bloque_grande();
    test_arena_reset();
    test_arena_ida_y_vuelta();

    test_split_equivalencia();
    test_split_binario();
    test_split_bordes();
    test_split_con_arena_activa();
    test_split_grande_en_arena();

    printf("%d comprobaciones, %d fallas\n", total, fallos);
    return fallos != 0;
}
