#ifndef SAFESTR_H
#define SAFESTR_H

/*
 * safestr - strings dinamicos y seguros para C
 *
 * Invariantes que la libreria garantiza siempre:
 *   1. Si data != NULL, el buffer esta terminado en '\0' en la posicion length.
 *   2. ss_cstr() NUNCA devuelve NULL (devuelve "" si el string esta vacio),
 *      asi printf("%s", ss_cstr(&s)) es seguro incluso recien creado.
 *   3. Un SafeString recien declarado con SS_INIT o ss_new() es valido:
 *      se le puede llamar cualquier funcion, incluido ss_free().
 *   4. El campo `error` es "pegajoso": si una reserva de memoria falla, se
 *      marca y las siguientes escrituras no hacen nada. Permite encadenar
 *      varias operaciones y revisar el error UNA sola vez con ss_ok().
 *
 * Errores de argumento (NULL, indice fuera de rango) devuelven false pero NO
 * marcan `error`: el string sigue siendo perfectamente usable.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct
{
    char*  data;      /* buffer en el heap ("" nunca garantizado: usa ss_cstr) */
    size_t length;    /* caracteres, sin contar el '\0' */
    size_t capacity;  /* bytes reservados, incluyendo el '\0' */
    bool   error;     /* true si alguna reserva de memoria fallo */
} SafeString;

/* Inicializador para declaraciones: SafeString s = SS_INIT; */
#define SS_INIT { NULL, 0, 0, false }

/* Valor devuelto cuando una posicion no existe */
#define SS_NPOS ((size_t) -1)

#if defined(__GNUC__)
#  define SS_PRINTF_FMT(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#  define SS_PRINTF_FMT(fmt_idx, arg_idx)
#endif

/* ------------------------------------------------------------------ */
/* Ciclo de vida                                                       */
/* ------------------------------------------------------------------ */

SafeString ss_new(void);                            /* string vacio             */
SafeString ss_from(const char* cstr);               /* crea a partir de un char* */
SafeString ss_clone(const SafeString* s);           /* copia independiente      */
void       ss_free(SafeString* s);                  /* libera y deja reutilizable */
void       ss_clear(SafeString* s);                 /* vacia sin liberar buffer */
bool       ss_reserve(SafeString* s, size_t min_capacidad);

/* ------------------------------------------------------------------ */
/* Lectura (nunca devuelven NULL ni revientan)                         */
/* ------------------------------------------------------------------ */

const char* ss_cstr(const SafeString* s);           /* "" si esta vacio */
size_t      ss_len(const SafeString* s);
bool        ss_is_empty(const SafeString* s);
bool        ss_ok(const SafeString* s);             /* false si hubo fallo de memoria */

/* ------------------------------------------------------------------ */
/* Escritura                                                           */
/* ------------------------------------------------------------------ */

bool ss_set(SafeString* s, const char* cstr);       /* reemplaza el contenido   */
bool ss_append(SafeString* s, const char* text);
bool ss_append_len(SafeString* s, const char* text, size_t len);
bool ss_append_char(SafeString* s, char c);
bool ss_append_ss(SafeString* s, const SafeString* otro);
bool ss_appendf(SafeString* s, const char* fmt, ...) SS_PRINTF_FMT(2, 3);
bool ss_vappendf(SafeString* s, const char* fmt, va_list ap);
bool ss_insert(SafeString* s, size_t pos, const char* text);
void ss_trim(SafeString* s);

/* Reemplaza TODAS las apariciones de `viejo` por `nuevo`.
   Devuelve true aunque no haya encontrado nada. `viejo` vacio es un error:
   buscarlo encontraria un resultado en cada posicion, sin avanzar nunca. */
bool ss_replace_all(SafeString* s, const char* viejo, const char* nuevo);

/* ------------------------------------------------------------------ */
/* Consultas                                                           */
/* ------------------------------------------------------------------ */

bool   ss_equals(const SafeString* a, const SafeString* b);
bool   ss_equals_cstr(const SafeString* s, const char* cstr);
size_t ss_index_of(const SafeString* s, const char* buscado);   /* SS_NPOS si no esta */
bool   ss_contains(const SafeString* s, const char* buscado);
bool   ss_starts_with(const SafeString* s, const char* prefijo);
bool   ss_ends_with(const SafeString* s, const char* sufijo);

/* Devuelve el trozo [inicio, fin) como un SafeString nuevo.
   Si el rango es invalido devuelve un string con error = true. */
SafeString ss_slice(const SafeString* s, size_t inicio, size_t fin);

/* ------------------------------------------------------------------ */
/* Lectura de archivos                                                 */
/* ------------------------------------------------------------------ */

/* Lee una linea completa de `f`, del largo que sea, y la deja en `linea`
   (reutilizando el buffer que ya tuviera). El salto de linea final no se
   incluye, y \r\n de Windows se maneja igual que \n.

   Devuelve false solo cuando no quedaba nada por leer, asi que sirve
   directamente como condicion de un while:

       SafeString linea = ss_new();
       while (ss_read_line(stdin, &linea))
           printf("%s\n", ss_cstr(&linea));
       ss_free(&linea);

   Trata la entrada como texto: un byte 0 dentro del archivo corta la linea. */
bool ss_read_line(FILE* f, SafeString* linea);

/* ------------------------------------------------------------------ */
/* Division en partes                                                  */
/* ------------------------------------------------------------------ */

/* Resultado de ss_split. `items` son `count` SafeStrings independientes.
   Se libera todo junto con ss_list_free; no liberes los items por separado. */
typedef struct
{
    SafeString* items;
    size_t      count;
    bool        error;
} SafeStringList;

/* Divide `s` cada vez que aparece `sep`.

     "a,b,c"  con ","  ->  3 partes: "a", "b", "c"
     "a,,b"   con ","  ->  3 partes: "a", "", "b"   (los vacios se conservan)
     "a,"     con ","  ->  2 partes: "a", ""
     ""       con ","  ->  1 parte:  ""

   `sep` vacio o NULL devuelve una lista con error = true. */
SafeStringList ss_split(const SafeString* s, const char* sep);

/* Une las partes de una lista intercalando `sep` y deja el texto en `out`.
   Es la operacion inversa de ss_split: unir con el mismo separador con que se
   dividio devuelve el original. `sep` puede ser "" (une sin separador).
   El contenido previo de `out` se reemplaza; en caso de fallo queda vacio. */
bool        ss_join(const SafeStringList* lista, const char* sep, SafeString* out);

void        ss_list_free(SafeStringList* lista);
/* Acceso seguro: devuelve "" si el indice no existe, nunca NULL. */
const char* ss_list_cstr(const SafeStringList* lista, size_t i);

/* ------------------------------------------------------------------ */
/* API previa (se mantiene por compatibilidad)                         */
/* ------------------------------------------------------------------ */

bool ss_from_cstr(SafeString* s, const char* cstr);
bool ss_find(const SafeString* s, const char* buscado, size_t* pos);
bool ss_substring(const SafeString* s, size_t inicio, size_t fin, SafeString* out);

#endif /* SAFESTR_H */
