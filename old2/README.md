# safestr: Dynamic String Manipulation Library

**Video Demo:** `<URL_DE_TU_VIDEO_YOUTUBE_O_OTRO>`

## Description

`safestr` is a lightweight, safe and dynamic string library for C. It solves a
problem that every beginner in C runs into sooner or later: manually juggling
fixed-size `char` buffers. Buffer overflows, memory leaks, dangling pointers
and out-of-bounds reads are the normal outcome when the size of the data is not
known in advance — user input, file contents, network payloads.

The library wraps the string inside a `SafeString` struct that owns its own
heap memory and grows on demand. The caller never calls `malloc`, `realloc` or
`strcpy` directly, and never has to compute a capacity by hand.

Beyond memory safety, the API is built around a second goal: **being pleasant to
use**. Constructors return values instead of taking out-parameters, reads never
return `NULL`, formatted text can be appended with `printf` syntax, and
allocation failures are recorded in a sticky flag so a chain of operations can
be checked once at the end instead of after every call.

```c
#include "safestr.h"
#include <stdio.h>

int main(void)
{
    SafeString s = ss_from("  Hola ");

    ss_trim(&s);
    ss_appendf(&s, ", %s! Tienes %d mensajes.", "Ana", 3);

    if (!ss_ok(&s))                    // una sola comprobacion para todo
    {
        fprintf(stderr, "sin memoria\n");
        return 1;
    }

    printf("%s (%zu chars)\n", ss_cstr(&s), ss_len(&s));
    ss_free(&s);
}
```

```
Hola, Ana! Tienes 3 mensajes. (29 chars)
```

## Internal architecture

```c
typedef struct {
    char*  data;      // buffer en el heap
    size_t length;    // caracteres, sin contar el '\0'
    size_t capacity;  // bytes reservados, incluyendo el '\0'
    bool   error;     // true si alguna reserva de memoria fallo
} SafeString;
```

When an operation needs more room than `capacity` provides, the internal
`ss_grow` function doubles the allocation until it fits. This exponential
growth gives amortised *O(1)* cost for repeated appends: 20 000 consecutive
appends in the test suite trigger only a handful of `realloc` calls.

### Invariants

These four rules hold at all times, and they are what make the library hard to
misuse:

1. **If `data` is not `NULL`, the buffer is always null-terminated at
   `length`.** No operation can leave a half-written string behind.
2. **`ss_cstr()` never returns `NULL`.** An empty or freshly created string
   yields `""`, so `printf("%s", ss_cstr(&s))` is always safe. Reading `s.data`
   directly is still possible but not recommended, because it *can* be `NULL`.
3. **A `SafeString` created with `ss_new()` or `SS_INIT` is immediately
   valid.** Any function, including `ss_free()`, can be called on it. `ss_free()`
   is idempotent and leaves the struct ready to be reused.
4. **The `error` flag is sticky.** Once an allocation fails, every subsequent
   write becomes a no-op and the string stays readable and empty. Argument
   errors (`NULL` pointers, out-of-range indices) return `false` but do *not*
   set the flag — the string remains fully usable.

### Design decisions worth explaining

**Why an error flag instead of only return values.** Checking the return value
of every single append makes calling code unreadable, and in practice people
skip the checks. The sticky flag lets a program do ten operations and ask
`ss_ok()` once, without any risk of silently working on a corrupted string.

**Why `ss_cstr()` instead of reading `s.data`.** A pointer that is sometimes
`NULL` and sometimes valid is exactly the kind of trap the library is supposed
to remove. An accessor that always returns a printable string removes a whole
class of segfaults.

**Why self-referencing arguments are handled explicitly.** `ss_append(&s, s.data)`
looks harmless, but `realloc` may move the buffer, leaving the `text` argument
dangling — a use-after-free that AddressSanitizer flags immediately. Every
mutating function now checks whether the incoming pointer lives inside its own
buffer and either recomputes it after the growth or works on a temporary copy
(`ss_insert`, where the shift would otherwise overwrite the source before it is
read).

**Why overflow is checked.** `s->length + len + 1` can wrap around on `size_t`,
turning a huge request into a tiny allocation and a heap overflow. Every size
computation is guarded, and the doubling loop stops before it can overflow.

## API reference

### Life cycle

| Function | Description |
| --- | --- |
| `SafeString ss_new(void)` | Returns an empty, valid string. |
| `SafeString ss_from(const char* cstr)` | Builds a string from a C string. Returns a string with `error` set if `cstr` is `NULL` or the allocation fails. |
| `SafeString ss_clone(const SafeString* s)` | Independent copy. |
| `void ss_free(SafeString* s)` | Releases the buffer and resets the struct. Safe on `NULL` and safe to call twice. |
| `void ss_clear(SafeString* s)` | Empties the content but keeps the allocated capacity. |
| `bool ss_reserve(SafeString* s, size_t min_capacidad)` | Pre-allocates capacity to avoid repeated growth. |

`SS_INIT` is available as a brace initialiser: `SafeString s = SS_INIT;`.

### Reading

| Function | Description |
| --- | --- |
| `const char* ss_cstr(const SafeString* s)` | Null-terminated view of the content. Never `NULL`. |
| `size_t ss_len(const SafeString* s)` | Length in characters. |
| `bool ss_is_empty(const SafeString* s)` | True when the length is zero. |
| `bool ss_ok(const SafeString* s)` | False if an allocation has failed at any point. |

### Writing

| Function | Description |
| --- | --- |
| `bool ss_set(SafeString* s, const char* cstr)` | Replaces the whole content, reusing the existing buffer. |
| `bool ss_append(SafeString* s, const char* text)` | Concatenates a C string, growing if needed. |
| `bool ss_append_len(SafeString* s, const char* text, size_t len)` | Appends exactly `len` bytes; works with non-terminated data. |
| `bool ss_append_char(SafeString* s, char c)` | Appends one character. |
| `bool ss_append_ss(SafeString* s, const SafeString* otro)` | Appends another `SafeString`. |
| `bool ss_appendf(SafeString* s, const char* fmt, ...)` | Appends formatted text using `printf` syntax; the required size is computed automatically, so there is no truncation and no temporary buffer. |
| `bool ss_vappendf(SafeString* s, const char* fmt, va_list ap)` | `va_list` variant, for building your own variadic wrappers. |
| `bool ss_insert(SafeString* s, size_t pos, const char* text)` | Inserts at an index, shifting the rest to the right. Returns `false` if `pos > length`. |
| `void ss_trim(SafeString* s)` | Removes leading and trailing whitespace in place. |
| `bool ss_replace_all(SafeString* s, const char* viejo, const char* nuevo)` | Replaces every occurrence. An empty needle is an error: it would match at every position without ever advancing. |

### Reading files

| Function | Description |
| --- | --- |
| `bool ss_read_line(FILE* f, SafeString* linea)` | Reads one whole line of any length, without the trailing newline; `\r\n` is handled like `\n`. Returns `false` only at end of input, so it works directly as a `while` condition. |

```c
SafeString linea = ss_new();
while (ss_read_line(stdin, &linea))
    printf("%s\n", ss_cstr(&linea));
ss_free(&linea);
```

This is the reason the library exists in one function: the caller never picks a
buffer size, so a 50 000-character line goes through exactly like a 5-character
one.

### Splitting

| Function | Description |
| --- | --- |
| `SafeStringList ss_split(const SafeString* s, const char* sep)` | Splits on every occurrence of `sep`. Empty fields are preserved. An empty or `NULL` separator returns a list with `error` set. |
| `bool ss_join(const SafeStringList* lista, const char* sep, SafeString* out)` | Joins the parts with `sep` in between. Splitting and joining on the same separator round-trips back to the original. |
| `void ss_list_free(SafeStringList* lista)` | Frees the whole list, items included. |
| `const char* ss_list_cstr(const SafeStringList* lista, size_t i)` | Safe access; returns `""` for an index that does not exist. |

```c
SafeStringList campos = ss_split(&linea, ",");
for (size_t i = 0; i < campos.count; i++)
    printf("[%s]", ss_list_cstr(&campos, i));
ss_list_free(&campos);
```

The whole list is owned by one struct and freed by one call. That is a
deliberate choice: a `char**` returned from a split function makes the caller
responsible for freeing each string *and* the array, which is exactly the kind
of bookkeeping this library is meant to remove.

| Input | Result |
| --- | --- |
| `"a,b,c"` | `"a"`, `"b"`, `"c"` |
| `"a,,b"` | `"a"`, `""`, `"b"` |
| `"a,"` | `"a"`, `""` |
| `""` | `""` (one empty field) |

### Queries

| Function | Description |
| --- | --- |
| `bool ss_equals(const SafeString* a, const SafeString* b)` | Length-checked comparison. |
| `bool ss_equals_cstr(const SafeString* s, const char* cstr)` | Compares against a plain C string. |
| `size_t ss_index_of(const SafeString* s, const char* buscado)` | Index of the first occurrence, or `SS_NPOS`. |
| `bool ss_contains(const SafeString* s, const char* buscado)` | Presence test. |
| `bool ss_starts_with(const SafeString* s, const char* prefijo)` | Prefix test. |
| `bool ss_ends_with(const SafeString* s, const char* sufijo)` | Suffix test. |
| `SafeString ss_slice(const SafeString* s, size_t inicio, size_t fin)` | Returns the half-open range `[inicio, fin)` as a new string; `error` is set if the range is invalid. |

### Legacy API

These three functions from the first version of the library are kept so that
existing code keeps compiling. They are thin wrappers over the functions above.

| Function | Equivalent |
| --- | --- |
| `bool ss_from_cstr(SafeString* s, const char* cstr)` | `ss_set` |
| `bool ss_find(const SafeString* s, const char* buscado, size_t* pos)` | `ss_index_of` |
| `bool ss_substring(const SafeString* s, size_t inicio, size_t fin, SafeString* out)` | `ss_slice` |

`ss_substring` frees `out` before overwriting it, so reusing an output variable
no longer leaks. Note that `out` must be an initialised `SafeString`
(`ss_new()` or `SS_INIT`), never an uninitialised local.

## Building

```
make            # builds libsafestr.a
make check      # builds and runs every test in test/ with ASan + UBSan
make clean
make install PREFIX=/usr/local
make help
```

The Makefile is portable across GCC and Clang and takes the usual overrides:

```
make CC=clang OPT=-O3
make check SAN=            # run the tests without sanitizers
```

To use the library in another project, compile `safestr.c` alongside your
sources, or link against the static library:

```
cc -I/usr/local/include mi_programa.c -L/usr/local/lib -lsafestr -o mi_programa
```

## Files

```
safestr.h              public API and documented invariants
safestr.c              implementation
test/test_safestr.c    test suite
demo.c                 30-line tour of the API
limpiar.c              stdin filter: trims, drops comments, numbers lines
reporte.c              CSV progress report with bars, totals and colours
avance.csv             sample input for reporte
Makefile               build, test and install targets
README.md              this file
```

## Testing and validation

The suite in `test/` runs 200 assertions covering the normal paths and, more
importantly, the edge cases: `NULL` arguments in every position, inverted and
out-of-range ranges, empty strings and empty needles, double frees, reuse of a
struct after `ss_free`, whitespace-only trimming, and a stress test that
performs 20 000 appends and 2 000 front insertions to exercise the growth path.

Three regression tests cover the bugs that the earlier version of the library
contained:

- `ss_append(&s, s.data)` and `ss_insert(&s, 0, s.data)` used to read memory
  that `realloc` had already released. AddressSanitizer reported this as
  `memcpy-param-overlap` / use-after-free.
- `ss_substring` overwrote its `out` parameter without freeing it, leaking the
  previous content.
- An empty substring left `data` as `NULL` while `ss_append(&s, "")` allocated
  a buffer, so whether `printf("%s", s.data)` was safe depended on how the
  string had been produced.

Everything is compiled with `-Wall -Wextra -Wpedantic` and run under
AddressSanitizer and UndefinedBehaviorSanitizer with leak detection enabled;
the suite passes with no warnings and no sanitizer reports.
