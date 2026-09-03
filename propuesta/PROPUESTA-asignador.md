# Propuesta: pasar el tamaño anterior al asignador

**Resumen.** Agregar `ss_set_allocator_ex2`, una variante del hook de
asignador que además recibe el tamaño anterior del bloque. La librería ya
lo conoce (`s->capacity`), así que pasarlo no le cuesta nada, y le ahorra
al asignador tener que guardarlo él mismo en una cabecera por bloque.

En una carga de strings cortos eso elimina un **50% de sobrecosto de
memoria**. No mejora la velocidad; el argumento es memoria y
predictibilidad.

---

## El problema

El header ya invita a conectar una arena:

> *"Con esto puedes darle los tuyos: una arena, un pool, el asignador de un
> motor de juego o de un embebido."*

Pero la firma actual no alcanza para escribir una arena eficiente:

```c
typedef void* (*SafeReallocFn)(void* ctx, void* p, size_t n);
```

Cuando `p != NULL`, el asignador tiene que copiar el contenido viejo al
bloque nuevo, y para eso necesita saber **cuánto** copiar. Como la firma no
se lo dice, no le queda más que guardar el tamaño él mismo: una cabecera de
`sizeof(size_t)` delante de cada reserva.

Esa cabecera tiene un segundo costo, menos obvio y mayor que el primero. Al
ser un `size_t`, obliga a que **todos** los bloques queden alineados a 8, y
por lo tanto a redondear cada tamaño hacia arriba. Un campo de texto de 13
bytes termina ocupando 24.

Sin cabecera, la alineación exigible se puede calcular a partir del tamaño
pedido: como en C `sizeof` de cualquier tipo es múltiplo de su `alignof`, un
bloque de `n` bytes solo puede alojar tipos cuya alineación divide a `n`. La
alineación necesaria es el bit más bajo de `n`, con tope en `alignof(max_align_t)`.
Es exactamente la garantía que da `malloc`, ni más ni menos, y para largos
impares deja el relleno en cero.

## La propuesta

```c
typedef void* (*SafeReallocFn2)(void* ctx, void* p,
                                size_t tam_viejo, size_t tam_nuevo);

void ss_set_allocator_ex2(SafeReallocFn2 realloc_fn, SafeFreeFn free_fn,
                          void* ctx);
```

`tam_viejo` es 0 cuando `p` es `NULL`.

Los dos sitios que hacen crecer un buffer ya tienen el dato a mano:

```c
- char* nuevo_buffer = (char*) ss_realloc_fn(s->data, nueva_capacidad);
+ char* nuevo_buffer = (char*) ss_realloc_fn2(s->data, s->capacity, nueva_capacidad);
```

## Compatibilidad

Es aditivo. `SafeReallocFn`, `ss_set_allocator` y `ss_set_allocator_ex`
siguen exactamente igual, y quien no llame a la variante nueva no nota
ninguna diferencia. Internamente `ss_realloc_fn` pasa a ser un envoltorio de
`ss_realloc_fn2` con `tam_viejo = 0`.

El hook nuevo es por hilo, como el actual, y `ss_set_allocator(NULL, NULL)`
lo limpia junto con los demás.

Tamaño del parche: 8 líneas en `safestr.h`, 28 líneas tocadas en
`safestr.c`. Sin cambios en `amalgamate.sh` ni en la ABI de las estructuras
públicas.

## Números

Dividir un CSV de 200.000 campos cortos (3125 KB de datos reales), con el
heap fragmentado, alternando las dos arenas en el mismo proceso, 9
repeticiones:

| máquina | arena | mejor | mediana | peor | memoria usada |
|---|---|---|---|---|---|
| A | con cabecera 8 B | 0.0062 s | 0.0076 s | 0.0271 s | 4688 KB |
| A | sin cabecera | 0.0083 s | 0.0088 s | 0.0118 s | **3125 KB** |
| B | con cabecera 8 B | 0.0042 s | 0.0048 s | 0.0156 s | 4688 KB |
| B | sin cabecera | 0.0063 s | 0.0067 s | 0.0093 s | **3125 KB** |

Dos máquinas con velocidades base distintas, misma dirección en las tres
métricas: sin cabecera es peor en la mediana, mejor en el peor caso, y usa
un tercio menos de memoria.

3125 KB usados para 3125 KB pedidos: **sobrecosto cero**, contra 50% con
cabecera.

En tiempo la versión sin cabecera es algo más lenta en la mediana y bastante
más estable en el peor caso. **No se propone esto como optimización de
velocidad.** El beneficio es memoria y predictibilidad, que es justo lo que
importa en los casos que el header menciona: embebidos y motores de juego.

## Verificación

Prototipo completo en `safestr_arena2.h`, con:

- 200.000 bloques de tamaños 1 a 64: cero mal alineados respecto de la
  alineación exigible por su tamaño.
- 8 hilos (mitad con arena, mitad con `malloc` de C), 2000 vueltas cada
  uno: limpio bajo ThreadSanitizer y bajo AddressSanitizer + UBSan.
- Crecimiento intensivo (400.000 `ss_append_char`): contenido intacto, el
  tamaño anterior llega correcto en cada copia.
- Strings que cruzan el umbral y se delegan al sistema: 16 MB en 0.0023 s,
  contra 0.0080 s de `realloc` de C.

## Notas de portabilidad

Todo lo de los headers es `static inline`, no `static`. Con `static` a secas,
una unidad de traduccion que incluya el header y use solo parte de la API
genera un warning por cada funcion no usada: `-Wunused-function` en gcc y
clang, C4505 en MSVC `/W4`. Con `/WX` eso es un error de compilacion en el
proyecto de quien use la libreria, no en este.

El CI incluye un paso `uso_parcial` que compila exactamente ese caso con
`-Werror` (y `/W4 /WX` en MSVC) para que la regresion no vuelva.

Lo unico sin verificar es MSVC: la aritmetica con `uintptr_t`, el bit de
marca en la cabecera y `__declspec(thread)` nunca se compilaron fuera de
gcc. El test de hilos usa pthreads y no corre en Windows.

## Alternativa considerada y descartada

Cambiar la firma de `SafeReallocFn` en vez de agregar una segunda rompería a
todo el que ya haya conectado un asignador. Dado que la librería mantiene
una sección de *"API previa (se mantiene por compatibilidad)"*, agregar
parece más acorde con el criterio del proyecto.
