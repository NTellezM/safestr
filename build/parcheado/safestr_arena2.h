#ifndef SAFESTR_ARENA2_H
#define SAFESTR_ARENA2_H

/*
 * Arena SIN cabecera por bloque.
 *
 * Solo es posible porque ss_set_allocator_ex2 le pasa al asignador el tamaño
 * anterior del bloque. Sin eso, la arena tiene que guardarlo ella misma en
 * una cabecera de 8 bytes delante de cada reserva.
 *
 * Lo que cambia respecto de safestr_arena.h:
 *   - 0 bytes de sobrecosto por bloque en vez de 8.
 *   - El redondeo de alineacion sigue estando (es inevitable).
 *   - "Es el ultimo bloque?" ya no se responde leyendo una cabecera sino
 *     comparando el puntero, que es lo que ya haciamos.
 *   - Los bloques grandes se distinguen por rango de direcciones en vez de
 *     por una marca en la cabecera: se recorre la lista de grandes. Es O(n)
 *     en el numero de bloques grandes, que en la practica es 0 o 1.
 *
 * Es un prototipo para medir cuanto vale el cambio de API, no un reemplazo.
 */

#include <stdlib.h>
#include <string.h>
#include "safestr.h"

#ifndef SS_A2_ALINEACION
#  define SS_A2_ALINEACION 8
#endif

typedef struct SsA2Trozo
{
    struct SsA2Trozo* sig;
    char*             base;
    size_t            cap;
    size_t            usado;
} SsA2Trozo;

typedef struct SsA2Grande
{
    struct SsA2Grande* sig;
    char*              payload;
    size_t             n;
} SsA2Grande;

typedef struct
{
    SsA2Trozo*  actual;
    SsA2Grande* grandes;
    size_t      cap_trozo;
    char*       ultimo;
    SsA2Trozo*  ultimo_trozo;
    size_t      bytes_pedidos;
    size_t      bytes_reservados;
    size_t      n_bloques;
    size_t      n_trozos;
    size_t      n_crecimientos_en_sitio;
    size_t      n_copias;
} SsA2;

/* Alineacion exigible para un bloque de n bytes.
 *
 * En C, sizeof de cualquier tipo es multiplo de su alignof. Entonces un
 * bloque de n bytes solo puede alojar tipos cuya alineacion divide a n:
 * para n = 13 la unica alineacion posible es 1, para n = 12 es 4, etc.
 * La respuesta es el bit mas bajo de n, con tope en la alineacion maxima.
 *
 * Esto es exactamente lo que malloc esta obligado a garantizar, ni mas ni
 * menos, y para campos de texto de largo impar deja el sobrecosto en cero.
 *
 * Solo sirve sin cabecera: una cabecera size_t obligaria a alinear todo a 8.
 */
static inline size_t ss_a2_alineacion_(size_t n)
{
    if (n == 0) return 1;
    size_t bajo = n & (~n + 1);            /* bit menos significativo */
    return (bajo < SS_A2_ALINEACION) ? bajo : (size_t) SS_A2_ALINEACION;
}

static inline int ss_a2_nuevo_trozo_(SsA2* ar, size_t minimo)
{
    size_t cap = (ar->cap_trozo < minimo) ? minimo : ar->cap_trozo;
    SsA2Trozo* t = (SsA2Trozo*) malloc(sizeof(SsA2Trozo) + cap + SS_A2_ALINEACION);
    if (t == NULL) return 0;

    char* p = (char*)(t + 1);
    size_t d = (size_t)((uintptr_t) p % SS_A2_ALINEACION);
    if (d) p += SS_A2_ALINEACION - d;

    t->base = p; t->cap = cap; t->usado = 0; t->sig = ar->actual;
    ar->actual = t;
    ar->bytes_reservados += cap;
    ar->n_trozos++;
    return 1;
}

static inline void ss_a2_init(SsA2* ar, size_t cap_trozo)
{
    memset(ar, 0, sizeof *ar);
    ar->cap_trozo = cap_trozo ? cap_trozo : (size_t)(1 << 20);
}

static inline void ss_a2_free(SsA2* ar)
{
    SsA2Grande* g = ar->grandes;
    while (g) { SsA2Grande* s = g->sig; free(g->payload); free(g); g = s; }
    SsA2Trozo* t = ar->actual;
    while (t) { SsA2Trozo* s = t->sig; free(t); t = s; }
    size_t cap = ar->cap_trozo;
    memset(ar, 0, sizeof *ar);
    ar->cap_trozo = cap;
}

/* En la practica hay 0 o 1 bloques grandes vivos, asi que la busqueda
   lineal no aparece en el perfil. */
static inline SsA2Grande* ss_a2_buscar_grande_(SsA2* ar, void* p)
{
    for (SsA2Grande* g = ar->grandes; g != NULL; g = g->sig)
        if (g->payload == (char*) p) return g;
    return NULL;
}

static inline void* ss_a2_pedir_(SsA2* ar, size_t n)
{
    size_t al = ss_a2_alineacion_(n);

    /* Se alinea el INICIO segun lo que el bloque exige; el final no se
       redondea, asi que el siguiente bloque arranca pegado. */
    size_t relleno = 0;
    if (ar->actual != NULL)
    {
        size_t resto = (size_t)((uintptr_t)(ar->actual->base + ar->actual->usado) % al);
        if (resto) relleno = al - resto;
    }

    if (ar->actual == NULL || ar->actual->usado + relleno + n > ar->actual->cap)
    {
        if (!ss_a2_nuevo_trozo_(ar, n)) return NULL;
        relleno = 0;
    }

    SsA2Trozo* t = ar->actual;
    char* p = t->base + t->usado + relleno;
    t->usado += relleno + n;

    ar->ultimo = p;
    ar->ultimo_trozo = t;
    ar->bytes_pedidos += n;
    ar->n_bloques++;
    return p;
}

static inline void* ss_a2_realloc(void* ctx, void* p, size_t viejo, size_t n)
{
    SsA2* ar = (SsA2*) ctx;
    size_t umbral = ar->cap_trozo / 4;

    if (p == NULL)
    {
        if (n > umbral)
        {
            SsA2Grande* g = (SsA2Grande*) malloc(sizeof *g);
            if (!g) return NULL;
            g->payload = (char*) malloc(n);
            if (!g->payload) { free(g); return NULL; }
            g->n = n; g->sig = ar->grandes; ar->grandes = g;
            return g->payload;
        }
        return ss_a2_pedir_(ar, n);
    }

    SsA2Grande* g = ss_a2_buscar_grande_(ar, p);
    if (g != NULL)
    {
        char* nuevo = (char*) realloc(g->payload, n);
        if (!nuevo) return NULL;
        g->payload = nuevo; g->n = n;
        return nuevo;
    }

    /* crecimiento en sitio: el ultimo bloque, si el trozo tiene lugar */
    if ((char*) p == ar->ultimo && ar->ultimo_trozo != NULL && n <= umbral)
    {
        SsA2Trozo* t = ar->ultimo_trozo;
        size_t inicio = (size_t)((char*) p - t->base);
        if (inicio + n <= t->cap)
        {
            t->usado = inicio + n;
            ar->n_crecimientos_en_sitio++;
            return p;
        }
    }

    void* nuevo = ss_a2_realloc(ctx, NULL, 0, n);
    if (!nuevo) return NULL;
    memcpy(nuevo, p, (viejo < n) ? viejo : n);   /* <- el tamaño lo trajo quien llama */
    ar->n_copias++;
    return nuevo;
}

static inline void ss_a2_liberar(void* ctx, void* p)
{
    SsA2* ar = (SsA2*) ctx;
    if (p == NULL) return;

    SsA2Grande* ant = NULL;
    for (SsA2Grande* g = ar->grandes; g != NULL; ant = g, g = g->sig)
        if (g->payload == (char*) p)
        {
            if (ant) ant->sig = g->sig; else ar->grandes = g->sig;
            free(g->payload); free(g);
            return;
        }

    if ((char*) p == ar->ultimo && ar->ultimo_trozo != NULL)
    {
        ar->ultimo_trozo->usado = (size_t)((char*) p - ar->ultimo_trozo->base);
        ar->ultimo = NULL;
        ar->ultimo_trozo = NULL;
        ar->n_bloques--;
    }
}

static inline void ss_a2_activar(SsA2* ar) { ss_set_allocator_ex2(ss_a2_realloc, ss_a2_liberar, ar); }
static inline void ss_a2_desactivar(void)  { ss_set_allocator(NULL, NULL); }

#endif /* SAFESTR_ARENA2_H */
