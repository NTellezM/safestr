#include "safestr.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SS_CAPACIDAD_INICIAL 8

static const char SS_CADENA_VACIA[] = "";

/* ------------------------------------------------------------------ */
/* Asignador configurable                                              */
/* ------------------------------------------------------------------ */

/* El asignador vive en almacenamiento por hilo: configurarlo no es una
   carrera y no hay estado global compartido entre hilos. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#  define SS_THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
#  define SS_THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#  define SS_THREAD_LOCAL __declspec(thread)
#else
#  define SS_THREAD_LOCAL
#  warning "Sin almacenamiento por hilo: el asignador de safestr sera global."
#endif

static void* ss_realloc_por_defecto(void* ctx, void* p, size_t n)
{
    (void) ctx;
    return realloc(p, n);
}

static void ss_free_por_defecto(void* ctx, void* p)
{
    (void) ctx;
    free(p);
}

/* Adaptador para la version sin contexto: el par de punteros del usuario se
   guarda aparte, tambien por hilo. */
typedef struct
{
    void* (*realloc_fn)(void*, size_t);
    void  (*free_fn)(void*);
} SsAsignadorSimple;

static SS_THREAD_LOCAL SsAsignadorSimple ss_simple = { NULL, NULL };

static void* ss_realloc_simple(void* ctx, void* p, size_t n)
{
    (void) ctx;
    return ss_simple.realloc_fn(p, n);
}

static void ss_free_simple(void* ctx, void* p)
{
    (void) ctx;
    ss_simple.free_fn(p);
}

static SS_THREAD_LOCAL SafeReallocFn ss_realloc_actual = ss_realloc_por_defecto;
static SS_THREAD_LOCAL SafeFreeFn    ss_free_actual    = ss_free_por_defecto;
static SS_THREAD_LOCAL void*         ss_ctx_actual     = NULL;

static SS_THREAD_LOCAL SafeReallocFn2 ss_realloc2_actual = NULL;

static void* ss_realloc_fn2(void* p, size_t viejo, size_t n)
{
    if (ss_realloc2_actual != NULL)
        return ss_realloc2_actual(ss_ctx_actual, p, viejo, n);
    return ss_realloc_actual(ss_ctx_actual, p, n);
}

static void* ss_realloc_fn(void* p, size_t n)
{
    return ss_realloc_fn2(p, 0, n);
}

void ss_set_allocator_ex2(SafeReallocFn2 realloc_fn, SafeFreeFn free_fn, void* ctx)
{
    ss_realloc2_actual = realloc_fn;
    ss_free_actual = free_fn;
    ss_ctx_actual = ctx;
}

static void ss_free_fn(void* p)
{
    ss_free_actual(ss_ctx_actual, p);
}

void ss_set_allocator(void* (*realloc_fn)(void*, size_t), void (*free_fn)(void*))
{
    if (realloc_fn == NULL || free_fn == NULL)
    {
        ss_simple.realloc_fn = NULL;
        ss_simple.free_fn = NULL;
        ss_realloc_actual = ss_realloc_por_defecto;
        ss_free_actual = ss_free_por_defecto;
        ss_realloc2_actual = NULL;
        ss_ctx_actual = NULL;
        return;
    }

    ss_simple.realloc_fn = realloc_fn;
    ss_simple.free_fn = free_fn;
    ss_realloc2_actual = NULL;
    ss_realloc_actual = ss_realloc_simple;
    ss_free_actual = ss_free_simple;
    ss_ctx_actual = NULL;
}

void ss_set_allocator_ex(SafeReallocFn realloc_fn, SafeFreeFn free_fn, void* ctx)
{
    if (realloc_fn == NULL || free_fn == NULL)
    {
        ss_set_allocator(NULL, NULL);
        return;
    }

    ss_simple.realloc_fn = NULL;
    ss_simple.free_fn = NULL;
    ss_realloc_actual = realloc_fn;
    ss_free_actual = free_fn;
    ss_ctx_actual = ctx;
}

void ss_mem_free(void* p)
{
    ss_free_fn(p);
}

/* ------------------------------------------------------------------ */
/* Helpers internos                                                    */
/* ------------------------------------------------------------------ */

/* true si a + b no desborda size_t */
/* Como strstr pero con largos explicitos: no se detiene en un byte 0.
   memmem existe en glibc pero no es estandar, asi que va aqui. */
static const char* ss_memmem(const char* heno, size_t n_heno,
                             const char* aguja, size_t n_aguja)
{
    if (n_aguja == 0)
        return heno;

    if (n_aguja > n_heno)
        return NULL;

    const char* ultimo = heno + (n_heno - n_aguja);

    for (const char* p = heno; p <= ultimo; p++)
    {
        p = (const char*) memchr(p, aguja[0], (size_t)(ultimo - p) + 1);
        if (p == NULL)
            return NULL;

        if (memcmp(p, aguja, n_aguja) == 0)
            return p;
    }

    return NULL;
}

static bool ss_suma_segura(size_t a, size_t b)
{
    return a <= SIZE_MAX - b;
}

/* Si `p` apunta dentro del buffer de `s`, devuelve su offset; si no, SS_NPOS.
   Necesario porque realloc() puede mover el buffer y dejar colgado un puntero
   como en ss_append(&s, s.data). */
static size_t ss_offset_interno(const SafeString* s, const char* p)
{
    if (s->data == NULL || p == NULL)
        return SS_NPOS;

    uintptr_t base = (uintptr_t) s->data;
    uintptr_t pos  = (uintptr_t) p;

    if (pos < base || pos >= base + s->capacity)
        return SS_NPOS;

    return (size_t)(pos - base);
}

/* Duplicar mientras el string es chico (crecimiento amortizado O(1)) y pasar
   a bloques fijos cuando ya es grande, para no pedir el doble de 600 MB solo
   para agregar una coma. La politica es la de sds (Redis). */
static size_t ss_capacidad_objetivo(size_t actual, size_t minima)
{
    if (minima >= SS_MAX_PREALLOC)
    {
        if (minima > SIZE_MAX - SS_MAX_PREALLOC)
            return minima;
        return minima + SS_MAX_PREALLOC;
    }

    size_t nueva = (actual == 0) ? SS_CAPACIDAD_INICIAL : actual;
    while (nueva < minima)
    {
        if (nueva > SIZE_MAX / 2)
            return minima;
        nueva *= 2;
    }
    return nueva;
}

static bool ss_grow(SafeString* s, size_t min_capacidad)
{
    if (min_capacidad <= s->capacity)
        return true;

    size_t nueva_capacidad = ss_capacidad_objetivo(s->capacity, min_capacidad);

    char* nuevo_buffer = (char*) ss_realloc_fn2(s->data, s->capacity, nueva_capacidad);
    if (nuevo_buffer == NULL)
    {
        s->error = true;
        return false;
    }

    if (s->capacity == 0)
        nuevo_buffer[0] = '\0';   /* invariante: el buffer siempre esta terminado */

    s->data = nuevo_buffer;
    s->capacity = nueva_capacidad;
    return true;
}

/* ------------------------------------------------------------------ */
/* Ciclo de vida                                                       */
/* ------------------------------------------------------------------ */

SafeString ss_new(void)
{
    SafeString s = SS_INIT;
    return s;
}

SafeString ss_from(const char* cstr)
{
    SafeString s = ss_new();

    if (cstr == NULL || !ss_append(&s, cstr))
        s.error = true;

    return s;
}

SafeString ss_from_len(const char* datos, size_t len)
{
    SafeString s = ss_new();

    if (datos == NULL || !ss_append_len(&s, datos, len))
        s.error = true;

    return s;
}

bool ss_shrink(SafeString* s)
{
    if (s == NULL || s->error || s->data == NULL)
        return false;

    if (s->capacity == s->length + 1)
        return true;

    char* ajustado = (char*) ss_realloc_fn2(s->data, s->capacity, s->length + 1);
    if (ajustado == NULL)
        return false;   /* el buffer original sigue siendo valido */

    s->data = ajustado;
    s->capacity = s->length + 1;
    return true;
}

char* ss_release(SafeString* s)
{
    if (s == NULL || s->error)
        return NULL;

    if (s->data == NULL && !ss_grow(s, 1))   /* garantiza un "" liberable */
        return NULL;

    char* buffer = s->data;
    s->data = NULL;
    s->length = 0;
    s->capacity = 0;
    return buffer;
}

SafeString ss_clone(const SafeString* s)
{
    SafeString copia = ss_new();

    if (s == NULL || s->error)
    {
        copia.error = true;
        return copia;
    }

    if (!ss_append_len(&copia, ss_cstr(s), s->length))
        copia.error = true;

    return copia;
}

void ss_free(SafeString* s)
{
    if (s == NULL)
        return;

    ss_free_fn(s->data);
    s->data = NULL;
    s->length = 0;
    s->capacity = 0;
    s->error = false;   /* queda listo para reutilizarse */
}

void ss_clear(SafeString* s)
{
    if (s == NULL)
        return;

    s->length = 0;
    if (s->data != NULL)
        s->data[0] = '\0';
}

bool ss_reserve(SafeString* s, size_t min_capacidad)
{
    if (s == NULL || s->error)
        return false;

    return ss_grow(s, min_capacidad);
}

/* ------------------------------------------------------------------ */
/* Lectura                                                             */
/* ------------------------------------------------------------------ */

const char* ss_cstr(const SafeString* s)
{
    if (s == NULL || s->data == NULL)
        return SS_CADENA_VACIA;

    return s->data;
}

size_t ss_len(const SafeString* s)
{
    return (s == NULL) ? 0 : s->length;
}

bool ss_is_empty(const SafeString* s)
{
    return ss_len(s) == 0;
}

bool ss_ok(const SafeString* s)
{
    return s != NULL && !s->error;
}

/* ------------------------------------------------------------------ */
/* Escritura                                                           */
/* ------------------------------------------------------------------ */

bool ss_append_len(SafeString* s, const char* text, size_t len)
{
    if (s == NULL || text == NULL || s->error)
        return false;

    if (len == 0)
        return ss_grow(s, s->length + 1);

    if (!ss_suma_segura(s->length, len) || !ss_suma_segura(s->length + len, 1))
        return false;

    size_t offset = ss_offset_interno(s, text);

    if (!ss_grow(s, s->length + len + 1))
        return false;

    if (offset != SS_NPOS)          /* el buffer pudo moverse: reubicamos text */
        text = s->data + offset;

    memmove(s->data + s->length, text, len);
    s->length += len;
    s->data[s->length] = '\0';
    return true;
}

bool ss_append(SafeString* s, const char* text)
{
    if (s == NULL || text == NULL)
        return false;

    return ss_append_len(s, text, strlen(text));
}

bool ss_append_char(SafeString* s, char c)
{
    return ss_append_len(s, &c, 1);
}

bool ss_append_ss(SafeString* s, const SafeString* otro)
{
    if (otro == NULL)
        return false;

    return ss_append_len(s, ss_cstr(otro), otro->length);
}

bool ss_vappendf(SafeString* s, const char* fmt, va_list ap)
{
    if (s == NULL || fmt == NULL || s->error)
        return false;

    va_list copia;
    va_copy(copia, ap);
    int necesarios = vsnprintf(NULL, 0, fmt, copia);
    va_end(copia);

    if (necesarios < 0)
        return false;

    size_t extra = (size_t) necesarios;

    if (!ss_suma_segura(s->length, extra) || !ss_suma_segura(s->length + extra, 1))
        return false;

    if (!ss_grow(s, s->length + extra + 1))
        return false;

    vsnprintf(s->data + s->length, extra + 1, fmt, ap);
    s->length += extra;
    return true;
}

bool ss_appendf(SafeString* s, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    bool ok = ss_vappendf(s, fmt, ap);
    va_end(ap);
    return ok;
}

bool ss_set_len(SafeString* s, const char* datos, size_t len)
{
    if (s == NULL || datos == NULL || s->error)
        return false;

    if (!ss_suma_segura(len, 1))
        return false;

    size_t offset = ss_offset_interno(s, datos);

    if (!ss_grow(s, len + 1))
        return false;

    if (offset != SS_NPOS)
        datos = s->data + offset;

    memmove(s->data, datos, len);   /* memmove: puede solaparse consigo mismo */
    s->data[len] = '\0';
    s->length = len;
    return true;
}

bool ss_set(SafeString* s, const char* cstr)
{
    if (cstr == NULL)
        return false;

    return ss_set_len(s, cstr, strlen(cstr));
}

bool ss_insert_len(SafeString* s, size_t pos, const char* datos, size_t len)
{
    if (s == NULL || datos == NULL || s->error)
        return false;

    if (pos > s->length)
        return false;

    if (len == 0)
        return true;

    if (!ss_suma_segura(s->length, len) || !ss_suma_segura(s->length + len, 1))
        return false;

    /* Si `datos` vive dentro del propio buffer, el desplazamiento lo pisaria.
       Trabajamos sobre una copia temporal. */
    char* temporal = NULL;
    if (ss_offset_interno(s, datos) != SS_NPOS)
    {
        temporal = (char*) ss_realloc_fn(NULL, len);
        if (temporal == NULL)
        {
            s->error = true;
            return false;
        }
        memcpy(temporal, datos, len);
        datos = temporal;
    }

    if (!ss_grow(s, s->length + len + 1))
    {
        ss_free_fn(temporal);
        return false;
    }

    /* +1 para arrastrar tambien el '\0' final */
    memmove(s->data + pos + len, s->data + pos, s->length - pos + 1);
    memcpy(s->data + pos, datos, len);
    s->length += len;

    ss_free_fn(temporal);
    return true;
}

bool ss_insert(SafeString* s, size_t pos, const char* text)
{
    if (text == NULL)
        return false;

    return ss_insert_len(s, pos, text, strlen(text));
}

void ss_trim(SafeString* s)
{
    if (s == NULL || s->data == NULL || s->length == 0)
        return;

    size_t inicio = 0;
    while (inicio < s->length && isspace((unsigned char) s->data[inicio]))
        inicio++;

    if (inicio == s->length)
    {
        s->length = 0;
        s->data[0] = '\0';
        return;
    }

    size_t fin = s->length;
    while (fin > inicio && isspace((unsigned char) s->data[fin - 1]))
        fin--;

    size_t nueva_longitud = fin - inicio;
    memmove(s->data, s->data + inicio, nueva_longitud);
    s->data[nueva_longitud] = '\0';
    s->length = nueva_longitud;
}

bool ss_replace_all_len(SafeString* s, const char* viejo, size_t len_viejo,
                        const char* nuevo, size_t len_nuevo)
{
    if (s == NULL || viejo == NULL || nuevo == NULL || s->error)
        return false;

    /* Buscar la cadena vacia encontraria algo en cada posicion sin consumir
       un solo byte: bucle infinito que ademas va reservando memoria. */
    if (len_viejo == 0)
        return false;

    /* Se construye un buffer nuevo y al final se intercambia. Modificar en
       el sitio obligaria a desplazar la cola en cada reemplazo. */
    SafeString resultado = ss_new();
    const char* resto = ss_cstr(s);
    size_t restante = s->length;
    const char* encontrado;

    while ((encontrado = ss_memmem(resto, restante, viejo, len_viejo)) != NULL)
    {
        size_t antes = (size_t)(encontrado - resto);

        if (!ss_append_len(&resultado, resto, antes) ||
            !ss_append_len(&resultado, nuevo, len_nuevo))
        {
            ss_free(&resultado);
            s->error = true;
            return false;
        }

        resto = encontrado + len_viejo;      /* seguimos DESPUES de lo insertado */
        restante -= antes + len_viejo;
    }

    if (!ss_append_len(&resultado, resto, restante))
    {
        ss_free(&resultado);
        s->error = true;
        return false;
    }

    ss_free(s);
    *s = resultado;
    return true;
}

bool ss_replace_all(SafeString* s, const char* viejo, const char* nuevo)
{
    if (viejo == NULL || nuevo == NULL)
        return false;

    return ss_replace_all_len(s, viejo, strlen(viejo), nuevo, strlen(nuevo));
}

/* ------------------------------------------------------------------ */
/* Lectura de archivos                                                 */
/* ------------------------------------------------------------------ */

bool ss_read_line(FILE* f, SafeString* linea)
{
    if (f == NULL || linea == NULL || linea->error)
        return false;

    ss_clear(linea);

    /* Se lee byte a byte y no con fgets a proposito: fgets no puede decir
       cuantos bytes escribio si la linea contiene un 0, asi que un archivo
       binario quedaria truncado en silencio. getc esta bufferizado, el costo
       real es la llamada, no el acceso a disco. */
    int c;
    bool hubo_algo = false;

    while ((c = getc(f)) != EOF)
    {
        hubo_algo = true;

        if (c == '\n')
            break;

        if (!ss_append_char(linea, (char) c))
            return false;
    }

    /* \r\n de Windows: el \r quedo al final de lo acumulado. */
    if (linea->length > 0 && linea->data[linea->length - 1] == '\r')
    {
        linea->length--;
        linea->data[linea->length] = '\0';
    }

    return hubo_algo;
}

/* ------------------------------------------------------------------ */
/* Division en partes                                                  */
/* ------------------------------------------------------------------ */

SafeStringList ss_split_len(const SafeString* s, const char* sep, size_t len_sep)
{
    SafeStringList lista = { NULL, 0, false };

    if (s == NULL || sep == NULL || len_sep == 0 || s->error)
    {
        lista.error = true;
        return lista;
    }

    /* Contamos primero para reservar el arreglo de una sola vez. */
    size_t partes = 1;
    const char* p = ss_cstr(s);
    size_t restante = s->length;

    for (const char* hit = ss_memmem(p, restante, sep, len_sep); hit != NULL;
         hit = ss_memmem(p, restante, sep, len_sep))
    {
        partes++;
        restante -= (size_t)(hit - p) + len_sep;
        p = hit + len_sep;
    }

    lista.items = (SafeString*) ss_realloc_fn(NULL, partes * sizeof(SafeString));
    if (lista.items == NULL)
    {
        lista.error = true;
        return lista;
    }
    memset(lista.items, 0, partes * sizeof(SafeString));
    lista.count = partes;

    const char* resto = ss_cstr(s);
    restante = s->length;

    for (size_t i = 0; i < partes; i++)
    {
        const char* hit = (i + 1 < partes) ? ss_memmem(resto, restante, sep, len_sep) : NULL;
        size_t len = (hit != NULL) ? (size_t)(hit - resto) : restante;
        size_t avance = len + ((hit != NULL) ? len_sep : 0);

        lista.items[i] = ss_new();

        if (!ss_append_len(&lista.items[i], resto, len))
        {
            lista.error = true;
            return lista;   /* el que llama igual debe llamar a ss_list_free */
        }

        resto += avance;
        restante -= avance;
    }

    return lista;
}

SafeStringList ss_split(const SafeString* s, const char* sep)
{
    if (sep == NULL)
    {
        SafeStringList lista = { NULL, 0, true };
        return lista;
    }

    return ss_split_len(s, sep, strlen(sep));
}

bool ss_join_len(const SafeStringList* lista, const char* sep, size_t len_sep,
                 SafeString* out)
{
    if (lista == NULL || sep == NULL || out == NULL || lista->error)
        return false;

    if (lista->count > 0 && lista->items == NULL)
        return false;

    SafeString resultado = ss_new();

    for (size_t i = 0; i < lista->count; i++)
    {
        if (i > 0 && !ss_append_len(&resultado, sep, len_sep))
            break;

        if (!ss_append_ss(&resultado, &lista->items[i]))
            break;
    }

    if (!ss_ok(&resultado))
    {
        ss_free(&resultado);
        ss_clear(out);
        return false;
    }

    ss_free(out);       /* funciona aunque out sea uno de los items */
    *out = resultado;
    return true;
}

bool ss_join(const SafeStringList* lista, const char* sep, SafeString* out)
{
    if (sep == NULL)
        return false;

    return ss_join_len(lista, sep, strlen(sep), out);
}

void ss_list_free(SafeStringList* lista)
{
    if (lista == NULL)
        return;

    for (size_t i = 0; i < lista->count; i++)
        ss_free(&lista->items[i]);

    ss_free_fn(lista->items);
    lista->items = NULL;
    lista->count = 0;
    lista->error = false;
}

const char* ss_list_cstr(const SafeStringList* lista, size_t i)
{
    if (lista == NULL || lista->items == NULL || i >= lista->count)
        return SS_CADENA_VACIA;

    return ss_cstr(&lista->items[i]);
}

/* ------------------------------------------------------------------ */
/* Consultas                                                           */
/* ------------------------------------------------------------------ */

bool ss_equals(const SafeString* a, const SafeString* b)
{
    if (a == NULL || b == NULL)
        return false;

    if (a->length != b->length)
        return false;

    if (a->length == 0)
        return true;

    return memcmp(a->data, b->data, a->length) == 0;
}

int ss_cmp(const SafeString* a, const SafeString* b)
{
    size_t la = ss_len(a), lb = ss_len(b);
    size_t minimo = (la < lb) ? la : lb;

    int r = (minimo == 0) ? 0 : memcmp(ss_cstr(a), ss_cstr(b), minimo);
    if (r != 0)
        return r;

    return (la < lb) ? -1 : (la > lb) ? 1 : 0;
}

bool ss_equals_len(const SafeString* s, const char* datos, size_t len)
{
    if (s == NULL || datos == NULL || s->length != len)
        return false;

    return (len == 0) || memcmp(ss_cstr(s), datos, len) == 0;
}

bool ss_equals_cstr(const SafeString* s, const char* cstr)
{
    if (cstr == NULL)
        return false;

    return ss_equals_len(s, cstr, strlen(cstr));
}

size_t ss_index_of_len(const SafeString* s, const char* buscado, size_t len)
{
    if (s == NULL || buscado == NULL)
        return SS_NPOS;

    if (len == 0)
        return 0;

    const char* encontrado = ss_memmem(ss_cstr(s), s->length, buscado, len);
    if (encontrado == NULL)
        return SS_NPOS;

    return (size_t)(encontrado - ss_cstr(s));
}

size_t ss_index_of(const SafeString* s, const char* buscado)
{
    if (buscado == NULL)
        return SS_NPOS;

    return ss_index_of_len(s, buscado, strlen(buscado));
}

bool ss_contains_len(const SafeString* s, const char* buscado, size_t len)
{
    return ss_index_of_len(s, buscado, len) != SS_NPOS;
}

bool ss_contains(const SafeString* s, const char* buscado)
{
    return ss_index_of(s, buscado) != SS_NPOS;
}

bool ss_starts_with_len(const SafeString* s, const char* prefijo, size_t len)
{
    if (s == NULL || prefijo == NULL || len > s->length)
        return false;

    return memcmp(ss_cstr(s), prefijo, len) == 0;
}

bool ss_starts_with(const SafeString* s, const char* prefijo)
{
    if (prefijo == NULL)
        return false;

    return ss_starts_with_len(s, prefijo, strlen(prefijo));
}

bool ss_ends_with_len(const SafeString* s, const char* sufijo, size_t len)
{
    if (s == NULL || sufijo == NULL || len > s->length)
        return false;

    return memcmp(ss_cstr(s) + (s->length - len), sufijo, len) == 0;
}

bool ss_ends_with(const SafeString* s, const char* sufijo)
{
    if (sufijo == NULL)
        return false;

    return ss_ends_with_len(s, sufijo, strlen(sufijo));
}

SafeString ss_slice(const SafeString* s, size_t inicio, size_t fin)
{
    SafeString trozo = ss_new();

    if (s == NULL || inicio > fin || fin > s->length)
    {
        trozo.error = true;
        return trozo;
    }

    size_t len = fin - inicio;

    if (!ss_grow(&trozo, len + 1))
        return trozo;   /* ss_grow ya marco el error */

    if (len > 0)
        memcpy(trozo.data, s->data + inicio, len);

    trozo.data[len] = '\0';
    trozo.length = len;
    return trozo;
}

/* ------------------------------------------------------------------ */
/* API previa                                                          */
/* ------------------------------------------------------------------ */

bool ss_from_cstr(SafeString* s, const char* cstr)
{
    return ss_set(s, cstr);
}

bool ss_find(const SafeString* s, const char* buscado, size_t* pos)
{
    if (s == NULL || buscado == NULL || pos == NULL)
        return false;

    size_t encontrado = ss_index_of(s, buscado);
    if (encontrado == SS_NPOS)
        return false;

    *pos = encontrado;
    return true;
}

bool ss_substring(const SafeString* s, size_t inicio, size_t fin, SafeString* out)
{
    if (s == NULL || out == NULL)
        return false;

    if (inicio > fin || fin > s->length)
        return false;

    SafeString trozo = ss_slice(s, inicio, fin);
    if (trozo.error)
        return false;

    ss_free(out);    /* evita la fuga si `out` ya tenia contenido */
    *out = trozo;    /* funciona incluso si out == s */
    return true;
}

/* ================================================================== */
/* Vistas: texto prestado, sin reservar memoria                        */
/* ================================================================== */

SafeView sv(const char* cstr)
{
    SafeView v = { cstr, (cstr != NULL) ? strlen(cstr) : 0 };
    return v;
}

SafeView sv_len(const char* datos, size_t len)
{
    SafeView v = { datos, (datos != NULL) ? len : 0 };
    return v;
}

SafeView ss_view(const SafeString* s)
{
    SafeView v = { ss_cstr(s), ss_len(s) };
    return v;
}

SafeView ss_view_slice(const SafeString* s, size_t inicio, size_t fin)
{
    if (s == NULL || inicio > fin || fin > s->length)
        return SV_NULA;

    SafeView v = { ss_cstr(s) + inicio, fin - inicio };
    return v;
}

size_t sv_len_of(SafeView v)   { return v.len; }
bool   sv_is_empty(SafeView v) { return v.len == 0; }

bool sv_equals(SafeView a, SafeView b)
{
    if (a.len != b.len)
        return false;

    return (a.len == 0) || memcmp(a.ptr, b.ptr, a.len) == 0;
}

bool sv_equals_cstr(SafeView v, const char* cstr)
{
    return (cstr != NULL) && sv_equals(v, sv(cstr));
}

int sv_cmp(SafeView a, SafeView b)
{
    size_t minimo = (a.len < b.len) ? a.len : b.len;

    int r = (minimo == 0) ? 0 : memcmp(a.ptr, b.ptr, minimo);
    if (r != 0)
        return r;

    return (a.len < b.len) ? -1 : (a.len > b.len) ? 1 : 0;
}

size_t sv_index_of(SafeView heno, SafeView aguja)
{
    if (heno.ptr == NULL || aguja.ptr == NULL)
        return SS_NPOS;

    if (aguja.len == 0)
        return 0;

    const char* hit = ss_memmem(heno.ptr, heno.len, aguja.ptr, aguja.len);
    return (hit == NULL) ? SS_NPOS : (size_t)(hit - heno.ptr);
}

bool sv_contains(SafeView heno, SafeView aguja)
{
    return sv_index_of(heno, aguja) != SS_NPOS;
}

bool sv_starts_with(SafeView v, SafeView prefijo)
{
    if (v.ptr == NULL || prefijo.ptr == NULL || prefijo.len > v.len)
        return false;

    return memcmp(v.ptr, prefijo.ptr, prefijo.len) == 0;
}

bool sv_ends_with(SafeView v, SafeView sufijo)
{
    if (v.ptr == NULL || sufijo.ptr == NULL || sufijo.len > v.len)
        return false;

    return memcmp(v.ptr + (v.len - sufijo.len), sufijo.ptr, sufijo.len) == 0;
}

SafeView sv_slice(SafeView v, size_t inicio, size_t fin)
{
    if (v.ptr == NULL || inicio > fin || fin > v.len)
        return SV_NULA;

    SafeView r = { v.ptr + inicio, fin - inicio };
    return r;
}

SafeView sv_trim(SafeView v)
{
    if (v.ptr == NULL)
        return SV_NULA;

    size_t inicio = 0;
    while (inicio < v.len && isspace((unsigned char) v.ptr[inicio]))
        inicio++;

    size_t fin = v.len;
    while (fin > inicio && isspace((unsigned char) v.ptr[fin - 1]))
        fin--;

    SafeView r = { v.ptr + inicio, fin - inicio };
    return r;
}

long sv_to_long(SafeView v, bool* ok)
{
    if (ok != NULL)
        *ok = false;

    SafeView t = sv_trim(v);
    if (t.ptr == NULL || t.len == 0)
        return 0;

    size_t i = 0;
    bool negativo = false;

    if (t.ptr[0] == '+' || t.ptr[0] == '-')
    {
        negativo = (t.ptr[0] == '-');
        i = 1;
    }

    if (i >= t.len)
        return 0;

    unsigned long acumulado = 0;
    const unsigned long tope = negativo ? (unsigned long) LONG_MAX + 1u
                                        : (unsigned long) LONG_MAX;

    for (; i < t.len; i++)
    {
        if (t.ptr[i] < '0' || t.ptr[i] > '9')
            return 0;                       /* no era un entero */

        unsigned long digito = (unsigned long)(t.ptr[i] - '0');

        if (acumulado > (tope - digito) / 10)
            return 0;                       /* se pasa del rango de long */

        acumulado = acumulado * 10 + digito;
    }

    if (ok != NULL)
        *ok = true;

    return negativo ? -(long) acumulado : (long) acumulado;
}

bool sv_next(SafeView* resto, SafeView sep, SafeView* campo)
{
    if (resto == NULL || campo == NULL || resto->ptr == NULL ||
        sep.ptr == NULL || sep.len == 0)
        return false;

    const char* hit = ss_memmem(resto->ptr, resto->len, sep.ptr, sep.len);

    if (hit == NULL)
    {
        *campo = *resto;
        *resto = SV_NULA;                   /* la proxima llamada da false */
        return true;
    }

    size_t antes = (size_t)(hit - resto->ptr);
    campo->ptr = resto->ptr;
    campo->len = antes;

    resto->ptr = hit + sep.len;
    resto->len -= antes + sep.len;
    return true;
}

size_t ss_split_view(SafeView texto, SafeView sep, SafeView* salida, size_t max)
{
    if (texto.ptr == NULL || sep.ptr == NULL || sep.len == 0)
        return 0;

    SafeView resto = texto, campo;
    size_t total = 0;

    while (sv_next(&resto, sep, &campo))
    {
        if (total < max && salida != NULL)
            salida[total] = campo;

        total++;
    }

    return total;
}

SafeString ss_from_view(SafeView v)
{
    return ss_from_len(v.ptr, v.len);
}

bool ss_append_view(SafeString* s, SafeView v)
{
    return ss_append_len(s, v.ptr, v.len);
}

bool ss_set_view(SafeString* s, SafeView v)
{
    return ss_set_len(s, v.ptr, v.len);
}

/* ================================================================== */
/* Utilidades                                                          */
/* ================================================================== */

void ss_to_upper(SafeString* s)
{
    if (s == NULL || s->error || s->data == NULL)
        return;

    for (size_t i = 0; i < s->length; i++)
        s->data[i] = (char) toupper((unsigned char) s->data[i]);
}

void ss_to_lower(SafeString* s)
{
    if (s == NULL || s->error || s->data == NULL)
        return;

    for (size_t i = 0; i < s->length; i++)
        s->data[i] = (char) tolower((unsigned char) s->data[i]);
}

void ss_chomp(SafeString* s)
{
    if (s == NULL || s->error || s->data == NULL || s->length == 0)
        return;

    if (s->data[s->length - 1] != '\n')
        return;

    s->length--;
    if (s->length > 0 && s->data[s->length - 1] == '\r')
        s->length--;

    s->data[s->length] = '\0';
}

bool ss_remove(SafeString* s, size_t inicio, size_t fin)
{
    if (s == NULL || s->error || inicio > fin || fin > s->length)
        return false;

    if (inicio == fin)
        return true;

    memmove(s->data + inicio, s->data + fin, s->length - fin + 1);
    s->length -= fin - inicio;
    return true;
}

char ss_pop(SafeString* s)
{
    if (s == NULL || s->error || s->length == 0)
        return '\0';

    char c = s->data[--s->length];
    s->data[s->length] = '\0';
    return c;
}

bool ss_vsetf(SafeString* s, const char* fmt, va_list ap)
{
    if (s == NULL || fmt == NULL || s->error)
        return false;

    ss_clear(s);
    return ss_vappendf(s, fmt, ap);
}

bool ss_setf(SafeString* s, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    bool ok = ss_vsetf(s, fmt, ap);
    va_end(ap);
    return ok;
}

uint32_t sv_hash(SafeView v)
{
    uint32_t h = 2166136261u;                 /* FNV-1a de 32 bits */

    for (size_t i = 0; i < v.len; i++)
    {
        h ^= (unsigned char) v.ptr[i];
        h *= 16777619u;
    }

    return h;
}

uint64_t sv_hash64(SafeView v)
{
    uint64_t h = 14695981039346656037ull;     /* FNV-1a de 64 bits */

    for (size_t i = 0; i < v.len; i++)
    {
        h ^= (unsigned char) v.ptr[i];
        h *= 1099511628211ull;
    }

    return h;
}

uint32_t ss_hash(const SafeString* s)   { return sv_hash(ss_view(s)); }
uint64_t ss_hash64(const SafeString* s) { return sv_hash64(ss_view(s)); }
