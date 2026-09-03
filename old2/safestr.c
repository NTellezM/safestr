#include "safestr.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SS_CAPACIDAD_INICIAL 8

static const char SS_CADENA_VACIA[] = "";

/* ------------------------------------------------------------------ */
/* Helpers internos                                                    */
/* ------------------------------------------------------------------ */

/* true si a + b no desborda size_t */
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

static bool ss_grow(SafeString* s, size_t min_capacidad)
{
    if (min_capacidad <= s->capacity)
        return true;

    size_t nueva_capacidad = (s->capacity == 0) ? SS_CAPACIDAD_INICIAL : s->capacity;
    while (nueva_capacidad < min_capacidad)
    {
        if (nueva_capacidad > SIZE_MAX / 2)   /* evita desbordar al duplicar */
        {
            nueva_capacidad = min_capacidad;
            break;
        }
        nueva_capacidad *= 2;
    }

    char* nuevo_buffer = realloc(s->data, nueva_capacidad);
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

    free(s->data);
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

bool ss_set(SafeString* s, const char* cstr)
{
    if (s == NULL || cstr == NULL || s->error)
        return false;

    size_t len = strlen(cstr);

    if (!ss_suma_segura(len, 1))
        return false;

    size_t offset = ss_offset_interno(s, cstr);

    if (!ss_grow(s, len + 1))
        return false;

    if (offset != SS_NPOS)
        cstr = s->data + offset;

    memmove(s->data, cstr, len);   /* memmove: puede solaparse consigo mismo */
    s->data[len] = '\0';
    s->length = len;
    return true;
}

bool ss_insert(SafeString* s, size_t pos, const char* text)
{
    if (s == NULL || text == NULL || s->error)
        return false;

    if (pos > s->length)
        return false;

    size_t len = strlen(text);
    if (len == 0)
        return true;

    if (!ss_suma_segura(s->length, len) || !ss_suma_segura(s->length + len, 1))
        return false;

    /* Si `text` vive dentro del propio buffer, el desplazamiento lo pisaria.
       Trabajamos sobre una copia temporal. */
    char* temporal = NULL;
    if (ss_offset_interno(s, text) != SS_NPOS)
    {
        temporal = malloc(len + 1);
        if (temporal == NULL)
        {
            s->error = true;
            return false;
        }
        memcpy(temporal, text, len);
        temporal[len] = '\0';
        text = temporal;
    }

    if (!ss_grow(s, s->length + len + 1))
    {
        free(temporal);
        return false;
    }

    /* +1 para arrastrar tambien el '\0' final */
    memmove(s->data + pos + len, s->data + pos, s->length - pos + 1);
    memcpy(s->data + pos, text, len);
    s->length += len;

    free(temporal);
    return true;
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

bool ss_replace_all(SafeString* s, const char* viejo, const char* nuevo)
{
    if (s == NULL || viejo == NULL || nuevo == NULL || s->error)
        return false;

    size_t len_viejo = strlen(viejo);

    /* Buscar la cadena vacia encontraria algo en cada posicion sin consumir
       un solo byte: bucle infinito que ademas va reservando memoria. */
    if (len_viejo == 0)
        return false;

    /* Se construye un buffer nuevo y al final se intercambia. Modificar en
       el sitio obligaria a desplazar la cola en cada reemplazo. */
    SafeString resultado = ss_new();
    const char* resto = ss_cstr(s);
    const char* encontrado;

    while ((encontrado = strstr(resto, viejo)) != NULL)
    {
        if (!ss_append_len(&resultado, resto, (size_t)(encontrado - resto)) ||
            !ss_append(&resultado, nuevo))
        {
            ss_free(&resultado);
            s->error = true;
            return false;
        }
        resto = encontrado + len_viejo;   /* seguimos DESPUES de lo insertado */
    }

    if (!ss_append(&resultado, resto))
    {
        ss_free(&resultado);
        s->error = true;
        return false;
    }

    ss_free(s);
    *s = resultado;
    return true;
}

/* ------------------------------------------------------------------ */
/* Lectura de archivos                                                 */
/* ------------------------------------------------------------------ */

bool ss_read_line(FILE* f, SafeString* linea)
{
    if (f == NULL || linea == NULL || linea->error)
        return false;

    ss_clear(linea);

    /* El buffer de fgets es intencionalmente modesto: si la linea es mas
       larga, fgets la entrega por trozos y los vamos acumulando. */
    char trozo[256];
    bool hubo_algo = false;

    while (fgets(trozo, sizeof trozo, f) != NULL)
    {
        hubo_algo = true;
        size_t n = strlen(trozo);

        if (n > 0 && trozo[n - 1] == '\n')
        {
            if (!ss_append_len(linea, trozo, n - 1))
                return false;

            /* El \r puede haber quedado al final del trozo anterior, asi que
               se recorta sobre el resultado y no sobre el buffer de fgets. */
            if (linea->length > 0 && linea->data[linea->length - 1] == '\r')
            {
                linea->length--;
                linea->data[linea->length] = '\0';
            }
            return true;
        }

        if (!ss_append_len(linea, trozo, n))
            return false;
    }

    if (hubo_algo && linea->length > 0 && linea->data[linea->length - 1] == '\r')
    {
        linea->length--;
        linea->data[linea->length] = '\0';
    }

    return hubo_algo;   /* ultima linea sin salto al final */
}

/* ------------------------------------------------------------------ */
/* Division en partes                                                  */
/* ------------------------------------------------------------------ */

SafeStringList ss_split(const SafeString* s, const char* sep)
{
    SafeStringList lista = { NULL, 0, false };

    if (s == NULL || sep == NULL || sep[0] == '\0' || s->error)
    {
        lista.error = true;
        return lista;
    }

    size_t len_sep = strlen(sep);

    /* Contamos primero para reservar el arreglo de una sola vez. */
    size_t partes = 1;
    for (const char* p = strstr(ss_cstr(s), sep); p != NULL; p = strstr(p + len_sep, sep))
        partes++;

    lista.items = calloc(partes, sizeof(SafeString));
    if (lista.items == NULL)
    {
        lista.error = true;
        return lista;
    }
    lista.count = partes;

    const char* resto = ss_cstr(s);

    for (size_t i = 0; i < partes; i++)
    {
        const char* encontrado = (i + 1 < partes) ? strstr(resto, sep) : NULL;
        size_t len = (encontrado != NULL) ? (size_t)(encontrado - resto) : strlen(resto);

        lista.items[i] = ss_new();

        if (!ss_append_len(&lista.items[i], resto, len))
        {
            lista.error = true;
            return lista;   /* el que llama igual debe llamar a ss_list_free */
        }

        resto = (encontrado != NULL) ? encontrado + len_sep : resto + len;
    }

    return lista;
}

bool ss_join(const SafeStringList* lista, const char* sep, SafeString* out)
{
    if (lista == NULL || sep == NULL || out == NULL || lista->error)
        return false;

    if (lista->count > 0 && lista->items == NULL)
        return false;

    SafeString resultado = ss_new();

    for (size_t i = 0; i < lista->count; i++)
    {
        if (i > 0 && !ss_append(&resultado, sep))
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

void ss_list_free(SafeStringList* lista)
{
    if (lista == NULL)
        return;

    for (size_t i = 0; i < lista->count; i++)
        ss_free(&lista->items[i]);

    free(lista->items);
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

bool ss_equals_cstr(const SafeString* s, const char* cstr)
{
    if (s == NULL || cstr == NULL)
        return false;

    return strcmp(ss_cstr(s), cstr) == 0;
}

size_t ss_index_of(const SafeString* s, const char* buscado)
{
    if (s == NULL || buscado == NULL)
        return SS_NPOS;

    if (buscado[0] == '\0')
        return 0;

    if (s->length == 0)
        return SS_NPOS;

    const char* encontrado = strstr(s->data, buscado);
    if (encontrado == NULL)
        return SS_NPOS;

    return (size_t)(encontrado - s->data);
}

bool ss_contains(const SafeString* s, const char* buscado)
{
    return ss_index_of(s, buscado) != SS_NPOS;
}

bool ss_starts_with(const SafeString* s, const char* prefijo)
{
    if (s == NULL || prefijo == NULL)
        return false;

    size_t len = strlen(prefijo);
    if (len > s->length)
        return false;

    return memcmp(ss_cstr(s), prefijo, len) == 0;
}

bool ss_ends_with(const SafeString* s, const char* sufijo)
{
    if (s == NULL || sufijo == NULL)
        return false;

    size_t len = strlen(sufijo);
    if (len > s->length)
        return false;

    return memcmp(ss_cstr(s) + (s->length - len), sufijo, len) == 0;
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
