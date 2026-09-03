#include "safestr.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CAPACIDAD_INICIAL 8

static bool ss_grow(SafeString* s, size_t min_capacidad)
{
    size_t nueva_capacidad = (s->capacity == 0) ? CAPACIDAD_INICIAL : s->capacity;
    while (nueva_capacidad < min_capacidad)
        nueva_capacidad *= 2;

    char* nuevo_buffer = realloc(s->data, nueva_capacidad);
    if (nuevo_buffer == NULL)
        return false;

    s->data = nuevo_buffer;
    s->capacity = nueva_capacidad;
    return true;
}

SafeString ss_new(void)
{
    SafeString s = { .data = NULL, .length = 0, .capacity = 0 };
    return s;
}

bool ss_append(SafeString* s, const char* text)
{
    if (s == NULL || text == NULL)
        return false;

    size_t len_extra = strlen(text);
    size_t necesaria = s->length + len_extra + 1;

    if (necesaria > s->capacity)
        if (!ss_grow(s, necesaria))
            return false;

    memcpy(s->data + s->length, text, len_extra + 1);
    s->length += len_extra;
    return true;
}

void ss_free(SafeString* s)
{
    if (s == NULL)
        return;

    free(s->data);
    s->data = NULL;
    s->length = 0;
    s->capacity = 0;
}

bool ss_from_cstr(SafeString* s, const char* cstr)
{
    if (s == NULL || cstr == NULL)
        return false;

    ss_free(s); // Libera la memoria previa si existía

    return ss_append(s, cstr);
}

bool ss_equals(const SafeString* a, const SafeString* b)
{
    if (a == NULL || b == NULL)
        return false;

    if (a->length != b->length)
        return false;

    if (a->length == 0)
        return true; // los dos vacios, sin necesidad de comparar memoria

    return memcmp(a->data, b->data, a->length) == 0;
}

bool ss_find(const SafeString* s, const char* buscado, size_t* pos)
{
    if (s == NULL || buscado == NULL || pos == NULL)
        return false;

    if (s->length == 0) {
        if (buscado[0] == '\0') { *pos = 0; return true; }
        return false;
    }

    char* encontrado = strstr(s->data, buscado);
    if (encontrado == NULL)
        return false;

    *pos = (size_t)(encontrado - s->data);
    return true;
}

bool ss_substring(const SafeString* s, size_t inicio, size_t fin, SafeString* out)
{
    if (s == NULL || out == NULL)
        return false;

    if (inicio > fin || fin > s->length)
        return false;

    *out = ss_new();
    size_t len_sub = fin - inicio;

    if (len_sub == 0)
        return true; // substring vacio, out queda vacio -- valido, no es error

    if (!ss_grow(out, len_sub + 1))
        return false;

    memcpy(out->data, s->data + inicio, len_sub);
    out->data[len_sub] = '\0';
    out->length = len_sub;
    return true;
}

void ss_trim(SafeString* s)
{
    if (s == NULL || s->length == 0)
        return;

    size_t inicio = 0;
    while (inicio < s->length && isspace((unsigned char)s->data[inicio]))
        inicio++;

    if (inicio == s->length) {
        s->length = 0;
        s->data[0] = '\0';
        return;
    }

    size_t fin = s->length;
    while (fin > inicio && isspace((unsigned char)s->data[fin - 1]))
        fin--;

    size_t nueva_longitud = fin - inicio;
    memmove(s->data, s->data + inicio, nueva_longitud);
    s->data[nueva_longitud] = '\0';
    s->length = nueva_longitud;
}

bool ss_insert(SafeString* s, size_t pos, const char* text)
{
    if (s == NULL || text == NULL)
        return false;

    if (pos > s->length)
        return false;

    size_t len_texto = strlen(text);
    if (len_texto == 0)
        return true;

    size_t necesaria = s->length + len_texto + 1;
    if (necesaria > s->capacity)
        if (!ss_grow(s, necesaria))
            return false;
    
    if (s->length == 0) {
    s->data[0] = '\0';
    }

    size_t bytes_a_mover = s->length - pos + 1; // +1 incluye el '\0' final
    memmove(s->data + pos + len_texto, s->data + pos, bytes_a_mover);

    memcpy(s->data + pos, text, len_texto);

    s->length += len_texto;
    return true;
}
