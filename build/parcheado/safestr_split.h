#ifndef SAFESTR_SPLIT_H
#define SAFESTR_SPLIT_H

/*
 * safestr_split - division en partes con UNA sola reserva
 *
 * ss_split() entrega SafeStrings independientes: 1 reserva para el arreglo
 * mas 1 por cada parte. Dividir un CSV de 200.000 campos son 200.001 bloques
 * repartidos por el heap, y recorrerlos despues salta de uno a otro.
 *
 * SafeSplit hace una sola reserva con esta forma:
 *
 *     [ SafeView[count] ][ campo0 \0 campo1 \0 campo2 \0 ... ]
 *      \__ indice ____/   \______ texto contiguo ________/
 *
 * Las vistas apuntan dentro del mismo bloque, en el orden en que aparecen.
 * Recorrer los campos es recorrer memoria consecutiva.
 *
 * Cada campo queda terminado en '\0', asi que sirve tanto para las funciones
 * sv_* (sin copiar) como para printf("%s", ...) via ss_packed_cstr().
 *
 * Que se gana y que se pierde
 * ---------------------------
 *   +  1 reserva y 1 free en vez de N+1.
 *   +  Texto e indice contiguos: el recorrido no depende del estado del heap.
 *   +  Cada campo son 16 bytes de vista, no 32 de SafeString.
 *   -  Los campos NO son dueños de su texto y no se pueden modificar ni hacer
 *      crecer. Es una division de solo lectura.
 *   -  El bloque entero vive mientras viva cualquier campo. Si te quedas con
 *      dos campos de un CSV de 100 MB, retienes los 100 MB. Para conservar
 *      unos pocos, materializalos con ss_from_view().
 *
 * Asignador
 * ---------
 * El bloque sale del asignador configurado en el hilo, igual que cualquier
 * SafeString: si hay una arena activa, la division cae en la arena. Se pide
 * a traves de un SafeString temporal (ss_reserve + ss_release) porque la
 * libreria no expone una funcion de reserva directa.
 *
 * Vale la misma regla que para los SafeString: hay que liberar con el mismo
 * asignador con que se reservo. Si activas una arena, divides, y desactivas
 * la arena antes de llamar a ss_split_packed_free, el free se hace con el
 * asignador equivocado. Libera antes de cambiar, o deja que muera la arena.
 *
 * Si necesitas modificar las partes, usa ss_split(). Si solo necesitas
 * leerlas, filtrarlas o compararlas -que es el caso habitual-, esta.
 */

#include <stdlib.h>
#include <string.h>
#include "safestr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    void*     bloque;   /* la unica reserva; NO liberar con free() a mano */
    SafeView* items;    /* apunta dentro de bloque                        */
    size_t    count;
    bool      error;
} SafeSplit;

#define SS_SPLIT_INIT { NULL, NULL, 0, false }

/* ------------------------------------------------------------------ */

static inline const char* ss_split_buscar_(const char* heno, size_t n_heno,
                                    const char* aguja, size_t n_aguja)
{
    if (n_aguja == 0 || n_heno < n_aguja) return NULL;
    const char* fin = heno + (n_heno - n_aguja) + 1;
    for (const char* p = heno; p < fin; p++)
    {
        p = (const char*) memchr(p, aguja[0], (size_t)(fin - p));
        if (p == NULL) return NULL;
        if (memcmp(p, aguja, n_aguja) == 0) return p;
    }
    return NULL;
}

/* Divide `texto` por `sep`. Funciona sobre cualquier SafeView, asi que
   sirve igual para un SafeString (ss_view) o para un buffer prestado. */
static inline SafeSplit ss_split_packed_view(SafeView texto, SafeView sep)
{
    SafeSplit r = SS_SPLIT_INIT;

    if (sep.ptr == NULL || sep.len == 0 || (texto.ptr == NULL && texto.len != 0))
    {
        r.error = true;
        return r;
    }

    /* --- pasada 1: contar --- */
    size_t partes = 1;
    {
        const char* p = texto.ptr;
        size_t restante = texto.len;
        const char* hit;
        while ((hit = ss_split_buscar_(p, restante, sep.ptr, sep.len)) != NULL)
        {
            partes++;
            restante -= (size_t)(hit - p) + sep.len;
            p = hit + sep.len;
        }
    }

    /* --- una sola reserva: indice + texto ---
       El texto ocupa a lo sumo len bytes de contenido mas un '\0' por parte. */
    size_t bytes_idx   = partes * sizeof(SafeView);
    size_t bytes_texto = texto.len + partes + 1;

    /* El arreglo de vistas va primero, asi queda con la alineacion del bloque.
       La reserva pasa por un SafeString temporal para usar el asignador que
       el hilo tenga configurado (una arena, por ejemplo) en vez de malloc. */
    SafeString tmp = ss_new();
    if (!ss_reserve(&tmp, bytes_idx + bytes_texto))
    {
        ss_free(&tmp);
        r.error = true;
        return r;
    }
    char* bloque = ss_release(&tmp);   /* entrega el buffer sin encogerlo */
    if (bloque == NULL)
    {
        ss_free(&tmp);
        r.error = true;
        return r;
    }

    r.bloque = bloque;
    r.items  = (SafeView*) bloque;
    r.count  = partes;

    /* --- pasada 2: copiar campos consecutivos, cada uno terminado en '\0' --- */
    char* destino = bloque + bytes_idx;
    const char* resto = texto.ptr;
    size_t restante = texto.len;

    for (size_t i = 0; i < partes; i++)
    {
        const char* hit = (i + 1 < partes)
                        ? ss_split_buscar_(resto, restante, sep.ptr, sep.len)
                        : NULL;
        size_t len = (hit != NULL) ? (size_t)(hit - resto) : restante;

        if (len != 0) memcpy(destino, resto, len);
        destino[len] = '\0';

        r.items[i].ptr = destino;
        r.items[i].len = len;

        destino += len + 1;
        if (hit != NULL)
        {
            size_t avance = len + sep.len;
            resto += avance;
            restante -= avance;
        }
    }

    return r;
}

static inline SafeSplit ss_split_packed_len(const SafeString* s, const char* sep, size_t len_sep)
{
    if (s == NULL || !ss_ok(s))
    {
        SafeSplit r = SS_SPLIT_INIT;
        r.error = true;
        return r;
    }
    return ss_split_packed_view(ss_view(s), sv_len(sep, len_sep));
}

static inline SafeSplit ss_split_packed(const SafeString* s, const char* sep)
{
    return ss_split_packed_len(s, sep, (sep != NULL) ? strlen(sep) : 0);
}

static inline void ss_split_packed_free(SafeSplit* r)
{
    if (r == NULL) return;
    ss_mem_free(r->bloque);    /* un solo free, con el asignador configurado */
    r->bloque = NULL;
    r->items  = NULL;
    r->count  = 0;
    r->error  = false;
}

/* --- acceso seguro: nunca NULL, nunca fuera de rango --- */

static inline SafeView ss_packed_view(const SafeSplit* r, size_t i)
{
    if (r == NULL || r->items == NULL || i >= r->count) return SV_NULA;
    return r->items[i];
}

static inline const char* ss_packed_cstr(const SafeSplit* r, size_t i)
{
    if (r == NULL || r->items == NULL || i >= r->count) return "";
    return r->items[i].ptr;    /* terminado en '\0' por construccion */
}

static inline size_t ss_packed_count(const SafeSplit* r)
{
    return (r != NULL) ? r->count : 0;
}

/* Puente al mundo dueño de su memoria, para quedarse con un campo suelto
   sin retener el bloque completo. */
static inline SafeString ss_packed_take(const SafeSplit* r, size_t i)
{
    return ss_from_view(ss_packed_view(r, i));
}

#if defined(__GNUC__) || defined(__clang__)
static inline void ss_split_auto_free_(SafeSplit* r) { ss_split_packed_free(r); }
#  define SS_SPLIT_AUTO __attribute__((cleanup(ss_split_auto_free_)))
#else
#  define SS_SPLIT_AUTO
#endif

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* SAFESTR_SPLIT_H */
