#ifndef SAFESTR_ARENA_H
#define SAFESTR_ARENA_H

/*
 * safestr_arena - asignador de arena para safestr
 *
 * Reserva la memoria en bloques grandes y reparte tramos secuencialmente.
 * Todo lo que se pide a una arena muere junto: no hay liberacion individual,
 * y se devuelve todo de una vez con ss_arena_free().
 *
 * Para que sirve
 * -------------
 *   1. Localidad. Los strings quedan contiguos en el orden en que se crearon,
 *      asi que recorrerlos despues no salta por todo el heap.
 *   2. Un solo free en vez de N. Desaparece la fuga por free olvidado y el
 *      free en orden incorrecto.
 *   3. Velocidad de reserva: un incremento de puntero contra la contabilidad
 *      completa de malloc.
 *
 * Cuando NO usarla
 * ----------------
 *   Si los objetos tienen tiempos de vida distintos y necesitas liberar unos
 *   mientras otros siguen vivos. La arena solo recicla el ULTIMO bloque
 *   entregado; cualquier otro free es un no-op y esa memoria queda ocupada
 *   hasta el reset. Con vidas mezcladas, terminas usando mas memoria que con
 *   malloc.
 *
 * Uso
 * ---
 *     SsArena a;
 *     ss_arena_init(&a, 1 << 20);       // bloques de 1 MB
 *     ss_arena_activar(&a);             // desde aqui, safestr usa la arena
 *
 *     SafeStringList partes = ss_split(&texto, ",");
 *     ... trabajar ...
 *
 *     ss_arena_desactivar();            // vuelve a realloc/free de C
 *     ss_arena_free(&a);                // libera TODO de una vez
 *
 * El asignador de safestr es por hilo, asi que la arena tambien lo es:
 * activala en el mismo hilo donde vas a crear y usar los strings.
 *
 * IMPORTANTE: ss_release() sobre un string de arena devuelve un puntero que
 * NO se puede pasar a free() del sistema. Muere con la arena.
 *
 * El detalle que hace que esto funcione con strings
 * -------------------------------------------------
 * ss_append hace crecer el buffer con realloc. Una arena ingenua, sin
 * crecimiento en sitio, copiaria el string entero a un tramo nuevo en cada
 * append y abandonaria el anterior: el crecimiento amortizado de safestr se
 * volveria cuadratico en tiempo y en memoria. Por eso ss_arena_realloc
 * reconoce el caso "este bloque es el ultimo que entregue" y lo extiende
 * donde esta, sin copiar.
 */

#include <stdlib.h>
#include <string.h>
#include "safestr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 8 basta: lo mas exigente que reparte safestr son punteros y size_t.
   Con 16 se desperdiciaban ~8 bytes por campo en un CSV de campos cortos. */
#ifndef SS_ARENA_ALINEACION
#  define SS_ARENA_ALINEACION 8
#endif

/* Cabecera de cada bloque entregado: guarda el tamaño pedido, que realloc
   necesita para saber cuanto copiar. Ocupa una unidad de alineacion completa
   para que el payload quede alineado. */
#define SS_ARENA_CABECERA sizeof(size_t)

/* La cabecera guarda el tamaño pedido y, en el bit mas alto, si el bloque
   vive fuera de la arena. Un solo size_t en vez de dos. */
#define SS_ARENA_BIT_GRANDE (((size_t) 1) << (sizeof(size_t) * 8 - 1))
#define SS_ARENA_TAM(h)     ((h) & ~SS_ARENA_BIT_GRANDE)

typedef struct SsArenaTrozo
{
    struct SsArenaTrozo* sig;
    char*                base;      /* inicio del area repartible, alineado */
    size_t               cap;       /* bytes repartibles                    */
    size_t               usado;     /* bytes ya repartidos                  */
} SsArenaTrozo;

/* Bloque grande: no cabe comodo en un trozo, se delega al sistema y se
   registra aparte para poder liberarlo con la arena. Asi un string que
   crece hasta cientos de MB usa realloc/mremap en vez de copiarse a un
   trozo nuevo cada vez. */
typedef struct SsArenaGrande
{
    struct SsArenaGrande* sig;
    struct SsArenaGrande* ant;
    size_t                n;
    /* payload a continuacion */
} SsArenaGrande;

typedef struct
{
    SsArenaTrozo* actual;
    SsArenaGrande* grandes;
    size_t        cap_trozo;        /* tamaño de trozo por defecto */

    /* Ultimo bloque entregado: habilita crecer y liberar en sitio. */
    char*         ultimo;           /* payload del ultimo bloque, o NULL */
    SsArenaTrozo* ultimo_trozo;

    /* Estadisticas, utiles para medir y para decidir cap_trozo. */
    size_t        bytes_pedidos;
    size_t        bytes_reservados;
    size_t        n_bloques;
    size_t        n_trozos;
    size_t        n_crecimientos_en_sitio;
    size_t        n_copias;
    size_t        n_grandes;
} SsArena;

/* Un pedido mayor a esta fraccion del trozo se delega al sistema. */
#ifndef SS_ARENA_UMBRAL_GRANDE
#  define SS_ARENA_UMBRAL_GRANDE(cap) ((cap) / 4)
#endif

/* Marca en la cabecera para distinguir un bloque grande de uno de arena. */


/* ------------------------------------------------------------------ */

static inline size_t ss_arena_redondear_(size_t n)
{
    size_t a = SS_ARENA_ALINEACION;
    return (n + a - 1) & ~(a - 1);
}

static inline int ss_arena_nuevo_trozo_(SsArena* ar, size_t minimo)
{
    size_t cap = ar->cap_trozo;
    if (cap < minimo) cap = minimo;

    SsArenaTrozo* t = (SsArenaTrozo*) malloc(sizeof(SsArenaTrozo) + cap + SS_ARENA_ALINEACION);
    if (t == NULL) return 0;

    char* p = (char*)(t + 1);
    size_t desal = (size_t)((uintptr_t) p % SS_ARENA_ALINEACION);
    if (desal) p += SS_ARENA_ALINEACION - desal;

    t->base  = p;
    t->cap   = cap;
    t->usado = 0;
    t->sig   = ar->actual;
    ar->actual = t;

    ar->bytes_reservados += cap;
    ar->n_trozos++;
    return 1;
}

static inline void ss_arena_init(SsArena* ar, size_t cap_trozo)
{
    memset(ar, 0, sizeof *ar);
    ar->cap_trozo = (cap_trozo != 0) ? cap_trozo : (size_t)(1 << 20);
}

static inline void ss_arena_free(SsArena* ar)
{
    SsArenaGrande* g = ar->grandes;
    while (g != NULL) { SsArenaGrande* sig = g->sig; free(g); g = sig; }
    ar->grandes = NULL;

    SsArenaTrozo* t = ar->actual;
    while (t != NULL)
    {
        SsArenaTrozo* sig = t->sig;
        free(t);
        t = sig;
    }
    size_t cap = ar->cap_trozo;
    memset(ar, 0, sizeof *ar);
    ar->cap_trozo = cap;
}

/* Devuelve todo el espacio sin liberar los trozos: reutiliza la memoria ya
   pedida al sistema. Ideal para un bucle que procesa lote tras lote. */
static inline void ss_arena_reset(SsArena* ar)
{
    SsArenaGrande* g = ar->grandes;
    while (g != NULL) { SsArenaGrande* sig = g->sig; free(g); g = sig; }
    ar->grandes = NULL;
    ar->n_grandes = 0;

    for (SsArenaTrozo* t = ar->actual; t != NULL; t = t->sig)
        t->usado = 0;
    ar->ultimo = NULL;
    ar->ultimo_trozo = NULL;
    ar->bytes_pedidos = 0;
    ar->n_bloques = 0;
    ar->n_crecimientos_en_sitio = 0;
    ar->n_copias = 0;
}

/* ------------------------------------------------------------------ */

/* Reserva fuera de la arena, con realloc del sistema por debajo. */
static inline void* ss_arena_grande_(SsArena* ar, void* viejo_payload, size_t n)
{
    SsArenaGrande* g = NULL;
    if (viejo_payload != NULL)
        g = (SsArenaGrande*)((char*) viejo_payload - sizeof(SsArenaGrande) - SS_ARENA_CABECERA);

    SsArenaGrande* ant = g ? g->ant : NULL;
    SsArenaGrande* sig = g ? g->sig : NULL;

    SsArenaGrande* nuevo = (SsArenaGrande*) realloc(g, sizeof(SsArenaGrande) + SS_ARENA_CABECERA + n);
    if (nuevo == NULL) return NULL;

    nuevo->n = n;
    if (g == NULL)   /* alta: al frente de la lista */
    {
        nuevo->ant = NULL;
        nuevo->sig = ar->grandes;
        if (ar->grandes) ar->grandes->ant = nuevo;
        ar->grandes = nuevo;
        ar->n_grandes++;
    }
    else if (nuevo != g)   /* se movio: recoser los vecinos */
    {
        nuevo->ant = ant; nuevo->sig = sig;
        if (ant) ant->sig = nuevo; else ar->grandes = nuevo;
        if (sig) sig->ant = nuevo;
    }

    char* cab = (char*)(nuevo + 1);
    *(size_t*) cab = n | SS_ARENA_BIT_GRANDE;
    return cab + SS_ARENA_CABECERA;
}

static inline int ss_arena_es_grande_(void* payload)
{
    return (*(size_t*)((char*) payload - SS_ARENA_CABECERA) & SS_ARENA_BIT_GRANDE) != 0;
}

static inline void* ss_arena_pedir_(SsArena* ar, size_t n)
{
    if (n > SS_ARENA_UMBRAL_GRANDE(ar->cap_trozo))
        return ss_arena_grande_(ar, NULL, n);

    size_t total = SS_ARENA_CABECERA + ss_arena_redondear_(n);

    if (ar->actual == NULL || ar->actual->usado + total > ar->actual->cap)
        if (!ss_arena_nuevo_trozo_(ar, total))
            return NULL;

    SsArenaTrozo* t = ar->actual;
    char* bloque = t->base + t->usado;
    *(size_t*) bloque = n;   /* tamaño pedido; bit alto en 0 = vive en la arena */
    char* payload = bloque + SS_ARENA_CABECERA;

    t->usado += total;

    ar->ultimo = payload;
    ar->ultimo_trozo = t;
    ar->bytes_pedidos += n;
    ar->n_bloques++;
    return payload;
}

static inline void* ss_arena_realloc(void* ctx, void* p, size_t n)
{
    SsArena* ar = (SsArena*) ctx;

    if (p == NULL)
        return ss_arena_pedir_(ar, n);

    if (n == 0)
        return ss_arena_pedir_(ar, 0);

    char* payload = (char*) p;
    size_t viejo = SS_ARENA_TAM(*(size_t*)(payload - SS_ARENA_CABECERA));

    /* Ya estaba fuera de la arena: sigue con realloc del sistema. */
    if (ss_arena_es_grande_(payload))
        return ss_arena_grande_(ar, payload, n);

    /* Crecio hasta no caber comodo: se muda a un bloque grande. */
    if (n > SS_ARENA_UMBRAL_GRANDE(ar->cap_trozo))
    {
        void* nuevo = ss_arena_grande_(ar, NULL, n);
        if (nuevo == NULL) return NULL;
        memcpy(nuevo, p, (viejo < n) ? viejo : n);
        ar->n_copias++;
        return nuevo;
    }

    /* Camino rapido: es el ultimo bloque que entregue y el trozo tiene sitio.
       Se extiende donde esta, sin copiar un solo byte. */
    if (payload == ar->ultimo && ar->ultimo_trozo != NULL)
    {
        SsArenaTrozo* t = ar->ultimo_trozo;
        size_t inicio = (size_t)((payload - SS_ARENA_CABECERA) - t->base);
        size_t total  = SS_ARENA_CABECERA + ss_arena_redondear_(n);

        if (inicio + total <= t->cap)
        {
            *(size_t*)(payload - SS_ARENA_CABECERA) = n;
            t->usado = inicio + total;
            ar->bytes_pedidos += (n > viejo) ? (n - viejo) : 0;
            ar->n_crecimientos_en_sitio++;
            return payload;
        }
    }

    /* Camino lento: bloque nuevo y copia. */
    void* nuevo = ss_arena_pedir_(ar, n);
    if (nuevo == NULL) return NULL;
    memcpy(nuevo, p, (viejo < n) ? viejo : n);
    ar->n_copias++;
    return nuevo;
}

static inline void ss_arena_liberar(void* ctx, void* p)
{
    SsArena* ar = (SsArena*) ctx;
    if (p == NULL) return;

    if (ss_arena_es_grande_(p))
    {
        SsArenaGrande* g = (SsArenaGrande*)((char*) p - sizeof(SsArenaGrande) - SS_ARENA_CABECERA);
        if (g->ant) g->ant->sig = g->sig; else ar->grandes = g->sig;
        if (g->sig) g->sig->ant = g->ant;
        free(g);
        ar->n_grandes--;
        return;
    }

    if (ar->ultimo_trozo == NULL) return;

    /* Solo el ultimo bloque se puede devolver. El resto espera al reset. */
    if ((char*) p == ar->ultimo)
    {
        SsArenaTrozo* t = ar->ultimo_trozo;
        t->usado = (size_t)(((char*) p - SS_ARENA_CABECERA) - t->base);
        ar->ultimo = NULL;
        ar->ultimo_trozo = NULL;
        ar->n_bloques--;
    }
}

/* ------------------------------------------------------------------ */

static inline void ss_arena_activar(SsArena* ar)
{
    ss_set_allocator_ex(ss_arena_realloc, ss_arena_liberar, ar);
}

static inline void ss_arena_desactivar(void)
{
    ss_set_allocator(NULL, NULL);
}

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* SAFESTR_ARENA_H */
