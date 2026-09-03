# Comandos

Referencia de todo lo que se puede correr en este repositorio. Cada comando
está verificado; los que dependen del entorno están marcados.

Todo se ejecuta desde la **raíz** del repositorio salvo donde se indique.

---

## Lo mínimo

```sh
make check          # tests con AddressSanitizer y UndefinedBehaviorSanitizer
```

Salida esperada: `104 comprobaciones, 0 fallas`.

---

## Tests

| comando | qué hace |
|---|---|
| `make check` | 104 comprobaciones con ASan y UBSan, incluida detección de fugas |
| `make check-std` | la misma suite en C99, C11, C17 y C++17, con `-Werror` |
| `make check-parcial` | una TU que usa solo parte de la API, con `-Werror` |
| `make check-threads` | 8 hilos con ThreadSanitizer, y de nuevo con ASan |
| `make check-all` | los cuatro anteriores, en ese orden |

Con otro compilador:

```sh
make check CC=clang
make check-all CC=clang
```

Con otras banderas:

```sh
make check CFLAGS="-O0 -g"
make check-std CC=clang CXX=clang++
```

### Qué cubre cada uno

`check-parcial` es el menos obvio y el más importante para quien use la
librería: compila una unidad de traducción que incluye los headers pero
llama a poco de la API. Con `-Werror` atrapa cualquier función `static` sin
usar que se cuele, que en MSVC `/W4 /WX` sería un error en el proyecto del
usuario, no en este. Por eso todo en los headers es `static inline`.

`check-threads` corre el mismo test dos veces: con ThreadSanitizer para
detectar carreras, y con AddressSanitizer porque los dos no se pueden
combinar en una sola compilación.

---

## Benchmarks

```sh
make bench          # corre los cuatro seguidos
```

Para correr uno solo, después de `make bench` los binarios quedan en
`build/`:

```sh
./build/bench_arena      # arena contra malloc, heap limpio y fragmentado
./build/bench_split      # correctitud y rendimiento del split empaquetado
./build/bench_grow       # crecimiento en sitio contra copia
./build/bench_grande     # strings por encima de SS_MAX_PREALLOC
```

Los tiempos absolutos varían mucho entre máquinas. Lo que debería
mantenerse es la forma: la arena y el split empaquetado apenas se mueven
entre heap limpio y fragmentado, mientras que `ss_split` a secas se degrada
varias veces.

---

## Ejemplo de uso

```sh
make ejemplo        # compila, genera 50.000 movimientos y los resume
```

El binario queda en `build/bodega` y tiene dos modos:

```sh
./build/bodega generar 50000 > mov.csv     # datos de prueba a stdout
./build/bodega resumir mov.csv             # resume desde un archivo
./build/bodega resumir < mov.csv           # o desde stdin
./build/bodega generar 1000 | ./build/bodega resumir    # encadenado
```

El generador usa semilla fija: la misma cantidad de líneas da siempre el
mismo resultado, en cualquier máquina. 50.000 movimientos dan 1.276.153
unidades en 12 pasillos.

Desde `ejemplo/` también funciona (delega en la raíz):

```sh
cd ejemplo && make
```

A mano, sin make:

```sh
cc -std=c17 -O2 -I. ejemplo/bodega.c safestr.c -o bodega      # desde la raiz
cd ejemplo && cc -std=c17 -O2 -I.. bodega.c ../safestr.c -o bodega
```

---

## Propuesta upstream

```sh
make propuesta      # aplica el parche en build/ y corre lo que depende de el
```

No toca los fuentes: copia todo a `build/parcheado/`, aplica el parche ahí y
compila. `make clean` lo borra.

Corre tres cosas: la verificación de alineación por tamaño, el duelo entre
la arena con cabecera y la que no la tiene, y el test de hilos de la arena
sin cabecera con TSan.

Para aplicar el parche de verdad sobre tus fuentes:

```sh
patch -p1 --dry-run < propuesta/asignador.patch    # ver si aplica limpio
patch -p1 < propuesta/asignador.patch              # aplicarlo
patch -p1 -R < propuesta/asignador.patch           # revertirlo
```

---

## Limpiar

```sh
make clean                  # borra build/
cd ejemplo && make clean    # ademas borra bodega y los .csv sueltos
```

---

## Usarlo en tu proyecto

Los dos módulos son header-only y no requieren build system. Copia los
archivos y agrega los includes:

```sh
cp safestr_arena.h safestr_split.h /ruta/a/tu/proyecto/
```

```c
#include "safestr.h"
#include "safestr_arena.h"      /* arena */
#include "safestr_split.h"      /* division empaquetada */
```

No hacen falta cambios en `amalgamate.sh`: no tocan `safestr.c` ni
`safestr.h`.

### Arena

```c
SsArena a;
ss_arena_init(&a, 1 << 20);      /* trozos de 1 MB */
ss_arena_activar(&a);

/* ... todo lo que cree safestr en este hilo sale de la arena ... */

ss_list_free(&partes);           /* liberar ANTES de desactivar */
ss_arena_desactivar();
ss_arena_free(&a);               /* un free para todo */
```

Para procesar lote tras lote reutilizando la memoria:

```c
for (;;) {
    /* ... procesar un lote ... */
    ss_arena_reset(&a);          /* devuelve el espacio sin soltar los trozos */
}
```

### División empaquetada

```c
SafeSplit campos = ss_split_packed(&linea, ",");
if (campos.error) { /* ... */ }

SafeView primero = ss_packed_view(&campos, 0);
const char* segundo = ss_packed_cstr(&campos, 1);   /* terminado en \0 */
size_t n = ss_packed_count(&campos);

SafeString mio = ss_packed_take(&campos, 2);        /* copia con vida propia */

ss_split_packed_free(&campos);
```

---

## CI

`ci/ci-extras.yml` no es un workflow completo: son pasos para pegar dentro
de los jobs que el `ci.yml` de safestr ya tiene, para compartir checkout y
matriz de compiladores. Los tests van en `test/`, así que las rutas del
archivo asumen esa ubicación.

Para validar el YAML antes de commitear:

```sh
python3 -c "import yaml; yaml.safe_load(open('ci/ci-extras.yml'))"
```

Sin salida significa que está bien. No valida que los pasos sean correctos
para GitHub Actions —es un fragmento, no un workflow— pero sí detecta
errores de sintaxis. El más fácil de cometer es dos puntos sin comillas
dentro de un `name:`, que ya rompió este archivo una vez:

```yaml
- name: Uso parcial: sin warnings     # ← rompe el YAML
- name: "Uso parcial: sin warnings"   # ← correcto
```

---

## Problemas conocidos

### `make check-threads` falla con "unexpected memory mapping"

```
FATAL: ThreadSanitizer: unexpected memory mapping 0x64acfae7f000-0x64acfae80000
```

No es un problema del código. Es un choque entre ThreadSanitizer y el ASLR
de alta entropía de los kernels recientes: Ubuntu 24.04 y derivados subieron
`vm.mmap_rnd_bits` a 32. Falla igual con un `int main(void){return 0;}`
compilado con `-fsanitize=thread`.

El Makefile ya lo maneja con `setarch -R`. Si tu sistema no lo trae:

```sh
cat /proc/sys/vm/mmap_rnd_bits                    # ver el valor actual
sudo sysctl -w vm.mmap_rnd_bits=28                # temporal
echo 'vm.mmap_rnd_bits=28' | sudo tee /etc/sysctl.d/99-tsan.conf   # permanente
```

Para correr cualquier binario de TSan a mano:

```sh
setarch $(uname -m) -R ./build/hilos_tsan
```

### `make bodega` dentro de `ejemplo/` falla con "safestr.h: No existe"

Pasaba antes de que existiera `ejemplo/Makefile`. Sin él, GNU make usa su
regla implícita (`cc bodega.c -o bodega`), sin `-I..` y sin `safestr.c`. Si
ves ese error, falta ese archivo o estás en un checkout viejo.

### macOS: los sanitizers se quejan de fugas

macOS no soporta detección de fugas en ASan:

```sh
ASAN_OPTIONS=detect_leaks=0 make check
```

Y no permite ASan y TSan en la misma corrida, que es por lo que
`check-threads` compila dos binarios separados.

---

## Verificado y no verificado

Verificado en Linux con gcc y clang, y en macOS: los 104 tests en C99, C11, C17 y C++17 con
`-Werror`, ASan, UBSan, TSan, los cuatro benchmarks y el ejemplo. El parche
aplica limpio con `patch -p1` sobre los fuentes originales y todo vuelve a
pasar sobre el árbol parcheado.

**MSVC verificado por el CI**: tests, uso parcial con `/W4 /WX`, la versión
C++ y los dos ejemplos, compilados con `cl`. Eso ejercita la aritmética con
`uintptr_t`, el bit de marca en la cabecera de la arena y la rama
`__declspec(thread)` del asignador por hilo de safestr, que en Linux nunca
se compilan.

Lo único sin cobertura en Windows es el test de hilos, que usa pthreads;
haría falta reescribirlo con `CreateThread`.
