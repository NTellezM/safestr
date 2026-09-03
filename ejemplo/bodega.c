/*
 * bodega.c - ejemplo de uso de safestr_arena.h y safestr_split.h
 *
 * Procesa un CSV de movimientos de bodega y resume el total por pasillo.
 * Formato de cada linea:
 *
 *     ubicacion,sku,cantidad
 *     PAS03-C12-N02,SKU-0042,15
 *
 * Dos modos:
 *
 *     ./bodega generar 50000 > movimientos.csv
 *     ./bodega resumir < movimientos.csv
 *     ./bodega resumir movimientos.csv
 *
 * Que muestra este ejemplo
 * ------------------------
 *   - Una arena para toda la corrida: un free al final en vez de uno por
 *     linea, y las lineas quedan contiguas en el orden en que se leyeron.
 *   - ss_split_packed para partir cada linea: una reserva por linea en vez
 *     de una por campo.
 *   - SafeView para comparar y agrupar sin copiar un solo byte.
 *   - sv_to_long para leer la cantidad sin buffer temporal.
 *
 * Compilar:
 *     cc -std=c17 -O2 -I. ejemplo/bodega.c safestr.c -o bodega
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "safestr.h"
#include "safestr_arena.h"
#include "safestr_split.h"

/* ------------------------------------------------------------------ */
/* Generador de datos de prueba                                        */
/* ------------------------------------------------------------------ */

static int generar(long n)
{
    printf("ubicacion,sku,cantidad\n");
    unsigned semilla = 12345;
    for (long i = 0; i < n; i++)
    {
        semilla = semilla * 1103515245u + 12345u;
        unsigned r = (semilla >> 16) & 0x7fff;
        printf("PAS%02u-C%02u-N%u,SKU-%04u,%u\n",
               r % 12, (r / 12) % 40, (r / 480) % 4, r % 500, 1 + r % 50);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Acumulador por pasillo                                              */
/* ------------------------------------------------------------------ */

#define MAX_PASILLOS 64

typedef struct
{
    SafeString nombre;   /* copia propia: ver la nota de abajo */
    long       unidades;
    long       movimientos;
} Pasillo;

static Pasillo pasillos[MAX_PASILLOS];
static size_t  n_pasillos = 0;

/* AQUI ESTA EL PUNTO DELICADO DE TODA LA LIBRERIA.
 *
 * La primera version de este ejemplo guardaba el nombre como un SafeView
 * que apuntaba al bloque de la linea. Compilaba sin un solo warning, pasaba
 * los sanitizers, y daba mal: un unico pasillo con las 50.000 lineas.
 *
 * El motivo: ese bloque se devuelve a la arena al final de cada vuelta y se
 * reutiliza en la siguiente, asi que todas las vistas terminaban mirando la
 * misma memoria reciclada. Es exactamente el contrato de vida util que
 * documenta safestr.h: una vista deja de valer en cuanto muere el texto del
 * que salio.
 *
 * La regla practica: si el dato tiene que sobrevivir al bloque donde nacio,
 * hay que materializarlo. ss_from_view() copia y devuelve un SafeString que
 * si es dueño de su texto. Con la arena activa esa copia sale de la arena y
 * se libera con todo lo demas.
 */
static Pasillo* buscar(SafeView nombre)
{
    for (size_t i = 0; i < n_pasillos; i++)
        if (sv_equals(ss_view(&pasillos[i].nombre), nombre))
            return &pasillos[i];

    if (n_pasillos == MAX_PASILLOS) return NULL;

    Pasillo* p = &pasillos[n_pasillos++];
    p->nombre = ss_from_view(nombre);      /* copia con vida propia */
    p->unidades = 0;
    p->movimientos = 0;
    return p;
}

static int por_unidades(const void* a, const void* b)
{
    long ua = ((const Pasillo*) a)->unidades;
    long ub = ((const Pasillo*) b)->unidades;
    return (ua < ub) - (ua > ub);          /* descendente */
}

/* ------------------------------------------------------------------ */
/* Resumen                                                             */
/* ------------------------------------------------------------------ */

static int resumir(FILE* f)
{
    SsArena arena;
    ss_arena_init(&arena, 1 << 20);        /* trozos de 1 MB */
    ss_arena_activar(&arena);

    SafeString linea = ss_new();
    long n_lineas = 0, descartadas = 0;
    int  primera = 1;

    while (ss_read_line(f, &linea))
    {
        /* La cabecera del CSV se salta */
        if (primera)
        {
            primera = 0;
            if (ss_starts_with(&linea, "ubicacion")) continue;
        }

        if (ss_is_empty(&linea)) continue;

        /* Una sola reserva para los tres campos de esta linea */
        SafeSplit campos = ss_split_packed(&linea, ",");
        if (campos.error || campos.count != 3)
        {
            descartadas++;
            ss_split_packed_free(&campos);
            continue;
        }

        SafeView ubicacion = ss_packed_view(&campos, 0);
        SafeView cantidad  = ss_packed_view(&campos, 2);

        /* "PAS03-C12-N02" -> "PAS03", sin copiar */
        SafeView pasillo = sv_slice(ubicacion, 0, 5);

        bool ok = false;
        long unidades = sv_to_long(sv_trim(cantidad), &ok);
        if (!ok || unidades < 0)
        {
            descartadas++;
            ss_split_packed_free(&campos);
            continue;
        }

        Pasillo* p = buscar(pasillo);
        if (p != NULL)
        {
            p->unidades += unidades;
            p->movimientos++;
        }

        /* Aqui muere el bloque de esta linea. Todo lo que haga falta
           despues ya fue copiado dentro de buscar(). */
        ss_split_packed_free(&campos);
        n_lineas++;
    }

    /* --- salida --- */
    qsort(pasillos, n_pasillos, sizeof(Pasillo), por_unidades);

    printf("%-10s %12s %12s %10s\n", "pasillo", "unidades", "movimientos", "prom");
    printf("---------- ------------ ------------ ----------\n");

    long total = 0;
    for (size_t i = 0; i < n_pasillos; i++)
    {
        Pasillo* p = &pasillos[i];
        printf("%-10s %12ld %12ld %10.1f\n",
               ss_cstr(&p->nombre),
               p->unidades, p->movimientos,
               (double) p->unidades / (double) p->movimientos);
        total += p->unidades;
    }

    printf("\n%ld lineas, %zu pasillos, %ld unidades", n_lineas, n_pasillos, total);
    if (descartadas) printf(", %ld lineas descartadas", descartadas);
    printf("\n");

    printf("arena: %zu bloques en %zu trozo(s), %zu KB pedidos, %zu free al final\n",
           arena.n_bloques, arena.n_trozos, arena.bytes_pedidos / 1024, (size_t) 1);

    /* Liberar ANTES de desactivar: mismo asignador con que se reservo. */
    for (size_t i = 0; i < n_pasillos; i++) ss_free(&pasillos[i].nombre);
    ss_free(&linea);
    ss_arena_desactivar();
    ss_arena_free(&arena);      /* se lleva todas las lineas de una vez */
    return 0;
}

/* ------------------------------------------------------------------ */

static void uso(const char* prog)
{
    fprintf(stderr,
        "uso:\n"
        "  %s generar N            escribe N movimientos de prueba a stdout\n"
        "  %s resumir [archivo]    resume por pasillo (stdin si no hay archivo)\n",
        prog, prog);
}

int main(int argc, char** argv)
{
    if (argc < 2) { uso(argv[0]); return 2; }

    if (strcmp(argv[1], "generar") == 0)
    {
        long n = (argc > 2) ? strtol(argv[2], NULL, 10) : 1000;
        if (n <= 0) { fprintf(stderr, "N debe ser positivo\n"); return 2; }
        return generar(n);
    }

    if (strcmp(argv[1], "resumir") == 0)
    {
        FILE* f = stdin;
        if (argc > 2)
        {
            f = fopen(argv[2], "r");
            if (f == NULL) { perror(argv[2]); return 1; }
        }
        int r = resumir(f);
        if (f != stdin) fclose(f);
        return r;
    }

    uso(argv[0]);
    return 2;
}
