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
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef __cplusplus
#  include <stdbool.h>
#endif

/* Usable desde C++: los nombres se exportan con enlace C. safestr.c se puede
   compilar con un compilador de C o de C++, indistintamente. */
#ifdef __cplusplus
extern "C" {
#endif

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

/* A partir de este tamaño el buffer deja de duplicarse y crece de a bloques
   de 1 MB. Duplicar un string de 600 MB pediria 1.2 GB para agregarle una
   coma; la idea viene de sds (Redis). */
#define SS_MAX_PREALLOC (1024 * 1024)

#if defined(__GNUC__)
#  define SS_PRINTF_FMT(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#  define SS_PRINTF_FMT(fmt_idx, arg_idx)
#endif

/* Seguridad binaria: el contenido puede incluir bytes 0, y ninguna operacion
   se detiene en ellos. Toda funcion que recibe un `const char*` tiene una
   gemela `_len` con largo explicito, para cuando ese argumento tambien pueda
   contener ceros. Las versiones sin `_len` son envoltorios que llaman a
   strlen y son igual de seguras respecto del contenido del SafeString. */

/* ------------------------------------------------------------------ */
/* Ciclo de vida                                                       */
/* ------------------------------------------------------------------ */

SafeString ss_new(void);                            /* string vacio             */
SafeString ss_from(const char* cstr);               /* crea a partir de un char* */
SafeString ss_clone(const SafeString* s);           /* copia independiente      */
SafeString ss_from_len(const char* datos, size_t len);  /* admite bytes 0 */
void       ss_free(SafeString* s);
void       ss_clear(SafeString* s);
bool       ss_reserve(SafeString* s, size_t min_capacidad);
bool       ss_shrink(SafeString* s);   /* devuelve la capacidad sobrante */

/* Entrega el buffer interno al que llama, que pasa a ser su dueño y debe
   liberarlo con ss_mem_free(). `s` queda vacio y reutilizable.
   Sirve para hablar con APIs que esperan un char* que ellas van a liberar.
   Devuelve NULL solo si falta memoria o `s` es invalido. */
char*      ss_release(SafeString* s);

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
bool ss_set_len(SafeString* s, const char* datos, size_t len);
bool ss_append(SafeString* s, const char* text);
bool ss_append_len(SafeString* s, const char* text, size_t len);
bool ss_append_char(SafeString* s, char c);
bool ss_append_ss(SafeString* s, const SafeString* otro);
bool ss_appendf(SafeString* s, const char* fmt, ...) SS_PRINTF_FMT(2, 3);
bool ss_vappendf(SafeString* s, const char* fmt, va_list ap);
bool ss_insert(SafeString* s, size_t pos, const char* text);
bool ss_insert_len(SafeString* s, size_t pos, const char* datos, size_t len);
void ss_trim(SafeString* s);

/* Reemplaza TODAS las apariciones de `viejo` por `nuevo`.
   Devuelve true aunque no haya encontrado nada. `viejo` vacio es un error:
   buscarlo encontraria un resultado en cada posicion, sin avanzar nunca. */
bool ss_replace_all(SafeString* s, const char* viejo, const char* nuevo);
bool ss_replace_all_len(SafeString* s, const char* viejo, size_t len_viejo,
                        const char* nuevo, size_t len_nuevo);

/* ------------------------------------------------------------------ */
/* Consultas                                                           */
/* ------------------------------------------------------------------ */

bool   ss_equals(const SafeString* a, const SafeString* b);
/* Orden lexicografico al estilo strcmp: <0, 0 o >0. Sirve para qsort. */
int    ss_cmp(const SafeString* a, const SafeString* b);
bool   ss_equals_cstr(const SafeString* s, const char* cstr);
bool   ss_equals_len(const SafeString* s, const char* datos, size_t len);
size_t ss_index_of(const SafeString* s, const char* buscado);   /* SS_NPOS si no esta */
size_t ss_index_of_len(const SafeString* s, const char* buscado, size_t len);
bool   ss_contains(const SafeString* s, const char* buscado);
bool   ss_contains_len(const SafeString* s, const char* buscado, size_t len);
bool   ss_starts_with(const SafeString* s, const char* prefijo);
bool   ss_starts_with_len(const SafeString* s, const char* prefijo, size_t len);
bool   ss_ends_with(const SafeString* s, const char* sufijo);
bool   ss_ends_with_len(const SafeString* s, const char* sufijo, size_t len);

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
SafeStringList ss_split_len(const SafeString* s, const char* sep, size_t len_sep);

/* Une las partes de una lista intercalando `sep` y deja el texto en `out`.
   Es la operacion inversa de ss_split: unir con el mismo separador con que se
   dividio devuelve el original. `sep` puede ser "" (une sin separador).
   El contenido previo de `out` se reemplaza; en caso de fallo queda vacio. */
bool        ss_join(const SafeStringList* lista, const char* sep, SafeString* out);
bool        ss_join_len(const SafeStringList* lista, const char* sep, size_t len_sep,
                        SafeString* out);

void        ss_list_free(SafeStringList* lista);
/* Acceso seguro: devuelve "" si el indice no existe, nunca NULL. */
const char* ss_list_cstr(const SafeStringList* lista, size_t i);

/* ================================================================== */
/* Vistas: texto prestado, sin reservar memoria                        */
/* ================================================================== */

/*
 * Un SafeView apunta a texto que vive en otra parte: dentro de un
 * SafeString, dentro de un literal, dentro de un buffer que leiste. NO es
 * dueño de nada, no reserva y no se libera.
 *
 * Es lo que en Rust es &str y en C++ es string_view, y sirve para lo mismo:
 * dividir, recortar, comparar y buscar sin copiar un solo byte.
 *
 *     SafeView campos[8];
 *     size_t n = ss_split_view(ss_view(&linea), sv("," ), campos, 8);
 *     if (sv_equals_cstr(campos[0], "A120")) ...      // cero reservas
 *
 * CONTRATO DE VIDA UTIL, y es lo unico delicado de toda la libreria:
 * una vista deja de ser valida en cuanto el SafeString del que salio crece,
 * se modifica o se libera, porque el buffer pudo moverse. Usalas dentro del
 * bloque donde las creaste y no las guardes en una estructura que sobreviva
 * al texto. Si necesitas quedarte con el dato, materializalo:
 * ss_from_view() copia y te devuelve un SafeString que si es dueño.
 *
 * El resto de la libreria sigue siendo a prueba de balas. Esta parte es
 * rapida y pide criterio. Es el mismo trato que ofrecen string_view y &str.
 */
typedef struct
{
    const char* ptr;
    size_t      len;
} SafeView;

/* Una vista no valida / agotada. */
#ifdef __cplusplus
#  define SV_NULA (SafeView{ NULL, 0 })
#else
#  define SV_NULA ((SafeView){ NULL, 0 })
#endif

/* Para imprimir: printf(SV_FMT "\n", SV_ARG(campo));
   Una vista no termina en '\0', asi que %s no sirve. */
#define SV_FMT     "%.*s"
#define SV_ARG(v)  (int)(v).len, (v).ptr

/* Constructores */
SafeView sv(const char* cstr);                       /* desde un literal      */
SafeView sv_len(const char* datos, size_t len);      /* desde bytes crudos    */
SafeView ss_view(const SafeString* s);               /* todo el SafeString    */
SafeView ss_view_slice(const SafeString* s, size_t inicio, size_t fin);

/* Consultas: ninguna reserva memoria */
size_t   sv_len_of(SafeView v);
bool     sv_is_empty(SafeView v);
bool     sv_equals(SafeView a, SafeView b);
bool     sv_equals_cstr(SafeView v, const char* cstr);
int      sv_cmp(SafeView a, SafeView b);
size_t   sv_index_of(SafeView heno, SafeView aguja);  /* SS_NPOS si no esta */
bool     sv_contains(SafeView heno, SafeView aguja);
bool     sv_starts_with(SafeView v, SafeView prefijo);
bool     sv_ends_with(SafeView v, SafeView sufijo);
SafeView sv_slice(SafeView v, size_t inicio, size_t fin);
SafeView sv_trim(SafeView v);

/* Convierte a entero sin copiar a un buffer temporal. `ok` puede ser NULL.
   Acepta signo y espacios alrededor; cualquier otra cosa deja ok en false. */
long     sv_to_long(SafeView v, bool* ok);

/* Divide `texto` por `sep` escribiendo hasta `max` vistas en `salida`.
   Devuelve cuantos campos hay EN TOTAL: si el resultado es mayor que `max`,
   la linea tenia mas campos de los que cabian y solo se escribieron `max`. */
size_t   ss_split_view(SafeView texto, SafeView sep, SafeView* salida, size_t max);

/* Version incremental, sin arreglo y sin tope de campos:

       SafeView resto = ss_view(&linea), campo;
       while (sv_next(&resto, sv(","), &campo))
           ...
*/
bool     sv_next(SafeView* resto, SafeView sep, SafeView* campo);

/* Puentes de vuelta al mundo que si es dueño de su memoria */
SafeString ss_from_view(SafeView v);
bool       ss_append_view(SafeString* s, SafeView v);
bool       ss_set_view(SafeString* s, SafeView v);

/* ------------------------------------------------------------------ */
/* Asignador de memoria                                                */
/* ------------------------------------------------------------------ */

/* Por defecto la libreria usa realloc/free. Con esto puedes darle los tuyos:
   una arena, un pool, el asignador de un motor de juego o de un embebido.

   El asignador es POR HILO. Cada hilo arranca con realloc/free y lo que
   configures solo afecta al hilo que llama, asi que no hay carrera al
   configurarlo ni estado global compartido. La contrapartida: un SafeString
   creado en un hilo debe liberarse en un hilo con el mismo asignador. Si
   pasas strings entre hilos, dale a todos el mismo (o usa el de por defecto).

   La version _ex recibe un contexto que se pasa a cada llamada; es lo que
   necesitas para una arena, que tiene estado propio. Pasar NULL a
   ss_set_allocator restaura los de C. */
typedef void* (*SafeReallocFn)(void* ctx, void* p, size_t n);
typedef void  (*SafeFreeFn)(void* ctx, void* p);

void ss_set_allocator(void* (*realloc_fn)(void*, size_t), void (*free_fn)(void*));
void ss_set_allocator_ex(SafeReallocFn realloc_fn, SafeFreeFn free_fn, void* ctx);

/* Variante que ademas recibe el tamaño ANTERIOR del bloque. safestr ya lo
   conoce (es s->capacity), asi que pasarlo es gratis para la libreria y le
   ahorra al asignador tener que guardarlo el mismo en una cabecera por
   bloque. Para una arena eso son 8 bytes menos por reserva. tam_viejo es 0
   cuando p es NULL. */
typedef void* (*SafeReallocFn2)(void* ctx, void* p, size_t tam_viejo, size_t tam_nuevo);
void ss_set_allocator_ex2(SafeReallocFn2 realloc_fn, SafeFreeFn free_fn, void* ctx);

/* Libera memoria devuelta por ss_release usando el asignador configurado. */
void ss_mem_free(void* p);

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */

void ss_to_upper(SafeString* s);
void ss_to_lower(SafeString* s);

/* Quita UN salto de linea final (\n o \r\n) si lo hay. A diferencia de
   ss_trim, no toca espacios ni el principio. */
void ss_chomp(SafeString* s);

/* Borra el rango [inicio, fin) sin reasignar. */
bool ss_remove(SafeString* s, size_t inicio, size_t fin);

/* Saca y devuelve el ultimo caracter; '\0' si estaba vacio. */
char ss_pop(SafeString* s);

/* Como ss_appendf pero reemplazando el contenido en vez de agregar. */
bool ss_setf(SafeString* s, const char* fmt, ...) SS_PRINTF_FMT(2, 3);
bool ss_vsetf(SafeString* s, const char* fmt, va_list ap);

/* FNV-1a. Sirve para usar strings como clave de una tabla hash propia.
   No es criptografico y no resiste entradas elegidas por un atacante. */
uint32_t ss_hash(const SafeString* s);
uint64_t ss_hash64(const SafeString* s);
uint32_t sv_hash(SafeView v);
uint64_t sv_hash64(SafeView v);

/* ------------------------------------------------------------------ */
/* Liberacion automatica al salir del bloque (GCC y Clang)             */
/* ------------------------------------------------------------------ */

/*     SS_AUTO SafeString s = ss_from("no hay que liberarla a mano");
   En compiladores sin el atributo cleanup, SS_AUTO no hace nada y hay que
   llamar a ss_free igual, asi que uselo solo si no le importa esa asimetria. */
#if defined(__GNUC__) || defined(__clang__)
static inline void ss_auto_free_(SafeString* s) { ss_free(s); }
#  define SS_AUTO __attribute__((cleanup(ss_auto_free_)))
#else
#  define SS_AUTO
#endif

/* ------------------------------------------------------------------ */
/* API previa (se mantiene por compatibilidad)                         */
/* ------------------------------------------------------------------ */

bool ss_from_cstr(SafeString* s, const char* cstr);
bool ss_find(const SafeString* s, const char* buscado, size_t* pos);
bool ss_substring(const SafeString* s, size_t inicio, size_t fin, SafeString* out);

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* SAFESTR_H */
