#ifndef SAFESTR_H
#define SAFESTR_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char*  data;
    size_t length;
    size_t capacity;
} SafeString;

SafeString ss_new(void);
bool ss_append(SafeString* s, const char* text);
void ss_free(SafeString* s);

bool ss_from_cstr(SafeString* s, const char* cstr);
bool ss_equals(const SafeString* a, const SafeString* b);
bool ss_find(const SafeString* s, const char* buscado, size_t* pos);
bool ss_substring(const SafeString* s, size_t inicio, size_t fin, SafeString* out);
void ss_trim(SafeString* s);
bool ss_insert(SafeString* s, size_t pos, const char* text); // Agrega esta línea

#endif