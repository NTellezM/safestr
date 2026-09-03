# safestr-extras

Dos módulos header-only para [safestr](.), enfocados en **dónde queda la
memoria**, no en qué hace la librería.

- **`safestr_arena.h`** — asignador de arena. Se conecta al hook que safestr
  ya expone, así que el código existente mejora sin cambiar una línea.
- **`safestr_split.h`** — división en partes con una sola reserva, en vez de
  una por campo.

Ninguno toca `safestr.c` ni `safestr.h`, ni requiere cambios en
`amalgamate.sh`. Son archivos que se copian al proyecto y se incluyen.

---

## Por qué

`safestr` es sólida en el manejo de punteros: invariantes documentadas,
error pegajoso, `SS_AUTO`, `SafeView`. Lo que no controla es la geografía de
la memoria, y ahí es donde se pierde el rendimiento.

Dividir un CSV de 200.000 campos con `ss_split` hace 200.001 reservas
separadas. En un heap limpio quedan casi contiguas por casualidad; en un
proceso que lleva horas corriendo quedan repartidas entre los huecos, y todo
lo que venga después salta por toda la RAM.

Medido con `make bench` en dos máquinas distintas, 200.000 campos:

| | heap limpio | heap fragmentado | reservas |
|---|---|---|---|
| `ss_split` | 0.0097 s | 0.0251 s | 200.001 |
| `ss_split` + arena | — | 0.0066 s | 1 |
| `ss_split_packed` | 0.0044 s | 0.0040 s | 1 |

Lo que más importa no es el mejor tiempo sino la **estabilidad**:
`ss_split` se degrada 2.6× cuando el heap está fragmentado, mientras que la
arena y el split empaquetado apenas se mueven. El resultado deja de depender
del estado del proceso.

Los tiempos absolutos varían mucho entre máquinas —en un contenedor más
lento las mismas cifras fueron 3 a 4 veces mayores— pero esa forma se
replicó en ambas. La única diferencia cualitativa entre las dos corridas: en
la máquina más rápida la arena también le gana al `malloc` con heap limpio,
mientras que en la lenta perdía contra él. Si mides en tu entorno y te da lo
segundo, no es un error.

---

## Uso

### Arena

```c
#include "safestr.h"
#include "safestr_arena.h"

SsArena a;
ss_arena_init(&a, 1 << 20);      /* trozos de 1 MB */
ss_arena_activar(&a);            /* desde aqui, safestr usa la arena */

SafeStringList partes = ss_split(&texto, ",");
/* ... trabajar ... */
ss_list_free(&partes);           /* liberar ANTES de desactivar */

ss_arena_desactivar();
ss_arena_free(&a);               /* libera todo de una vez */
```

Cuándo **no** usarla: si los objetos tienen tiempos de vida distintos y
necesitas liberar unos mientras otros siguen vivos. La arena solo recicla el
último bloque entregado; con vidas mezcladas gasta más memoria que `malloc`.

### División empaquetada

```c
#include "safestr_split.h"

SafeSplit campos = ss_split_packed(&linea, ",");
if (sv_equals_cstr(ss_packed_view(&campos, 0), "A120"))
    printf("%s\n", ss_packed_cstr(&campos, 1));
ss_split_packed_free(&campos);
```

Los campos son `SafeView` de solo lectura que apuntan a un único bloque, y
además quedan terminados en `\0`, así que sirven tanto para las funciones
`sv_*` como para `printf("%s", ...)`.

Cuándo **no** usarla: si necesitas modificar o hacer crecer las partes. Y
ten en cuenta que el bloque entero vive mientras viva cualquier campo: para
quedarte con unos pocos de un CSV grande, usa `ss_packed_take()`.

### Las dos juntas

Con una arena activa, `ss_split_packed` también reserva en la arena. La
regla de siempre: liberar con el mismo asignador con que se reservó.

---

## Estructura

```
safestr-extras/
  README.md
  Makefile
  LICENSE
  safestr.h            fuentes originales, sin modificar
  safestr.c
  safestr_arena.h      arena
  safestr_split.h      division empaquetada
  test/
    test_extras.c        104 comprobaciones de ambos modulos
    test_arena_hilos.c   8 hilos, mitad con arena, mitad con malloc
    uso_parcial.c        una TU que usa solo parte de la API
  bench/
    bench_arena.c        arena contra malloc, heap limpio y fragmentado
    bench_split.c        correctitud y rendimiento del split empaquetado
    bench_grow.c         crecimiento en sitio contra copia
    bench_grande.c       por encima de SS_MAX_PREALLOC
  propuesta/
    PROPUESTA-asignador.md   cambio de API sugerido para safestr
    asignador.patch          el parche, aplica limpio con patch -p1
    safestr_arena2.h         prototipo de arena sin cabecera
    duelo.c                  las dos arenas en el mismo proceso
    alineacion.c             verifica la alineacion por tamaño
    test_a2_hilos.c          la arena sin cabecera con TSan
  ci/
    ci-extras.yml        pasos para pegar en el ci.yml existente
```

---

## Cómo correrlo

Requiere un compilador de C con sanitizers (gcc o clang), `g++` para la
comprobación de C++, y `make`.

```sh
make check           # tests con AddressSanitizer y UndefinedBehaviorSanitizer
make check-threads   # test de hilos con ThreadSanitizer, y con ASan
make check-parcial   # uso parcial de la API, con -Werror
make check-std       # C99, C11, C17 y C++17, todos con -Werror
make check-all       # todo lo anterior
make bench           # los cuatro benchmarks
make ejemplo         # programa de demostracion (ver mas abajo)
make propuesta       # aplica el parche en build/ y corre lo que depende de el
make clean
```

Para usar otro compilador: `make check CC=clang`.

### Si `make check-threads` falla con "unexpected memory mapping"

```
FATAL: ThreadSanitizer: unexpected memory mapping 0x64acfae7f000-0x64acfae80000
```

No es un problema del código: es un choque entre ThreadSanitizer y el ASLR
de alta entropía de los kernels recientes. Ubuntu 24.04 y derivados subieron
`vm.mmap_rnd_bits` a 32 y TSan ya no puede predecir dónde cae el mapa de
memoria. Falla igual con un `int main(void){return 0;}` vacío compilado con
`-fsanitize=thread`.

El Makefile ya lo maneja: usa `setarch -R`, que desactiva la aleatorización
solo para ese proceso. Si tu sistema no trae `setarch`, hay dos salidas:

```sh
sudo sysctl -w vm.mmap_rnd_bits=28          # temporal, hasta reiniciar
echo 'vm.mmap_rnd_bits=28' | sudo tee -a /etc/sysctl.d/99-tsan.conf   # permanente
```

Para comprobar el valor actual: `cat /proc/sys/vm/mmap_rnd_bits`.

`make propuesta` no toca los fuentes: copia todo a `build/parcheado/`,
aplica el parche ahí y compila. `make clean` lo borra.

### Qué esperar

`make check-all` termina con `104 comprobaciones, 0 fallas` en cada uno de
los cuatro estándares, sin warnings y sin fugas.

Los tiempos de `make bench` varían mucho según la máquina y la carga. Lo
que debería mantenerse es la **forma**: la arena y el split empaquetado
apenas se mueven entre heap limpio y heap fragmentado, mientras que
`ss_split` a secas se degrada varias veces.

---

## Ejemplo de uso

`ejemplo/bodega.c` procesa un CSV de movimientos de bodega y resume el total
por pasillo. Es el caso para el que estos módulos están pensados: muchas
líneas, muchos campos, todo de solo lectura, todo con la misma vida útil.

```sh
make ejemplo                              # genera datos y los resume
./build/bodega generar 50000 > mov.csv
./build/bodega resumir mov.csv
```

Todo se compila desde la **raíz** del repositorio, no desde `ejemplo/`:
`bodega.c` necesita `safestr.c` y los headers, que están un nivel arriba.
Hay un `ejemplo/Makefile` que delega en la raíz, así que `make` ahí adentro
también funciona. Sin él, `make bodega` dispararía la regla implícita de GNU
make (`cc bodega.c -o bodega`, sin `-I..` y sin `safestr.c`) y fallaría con
`safestr.h: No existe el archivo`.

Para compilarlo a mano:

```sh
cc -std=c17 -O2 -I.. ejemplo/bodega.c safestr.c -o bodega     # desde la raiz
cc -std=c17 -O2 -I..  bodega.c ../safestr.c -o bodega         # desde ejemplo/
```

```
pasillo        unidades  movimientos       prom
---------- ------------ ------------ ----------
PAS11            110180         4240       26.0
PAS03            109938         4238       25.9
...
50000 lineas, 12 pasillos, 1276153 unidades
arena: 25 bloques en 1 trozo(s), 6250 KB pedidos, 1 free al final
```

25 bloques para 50.000 líneas de tres campos cada una, y un `free`. Sin
arena y con `ss_split`, serían más de 200.000 reservas.

**El ejemplo también documenta un error real.** La primera versión guardaba
el nombre del pasillo como un `SafeView` que apuntaba al bloque de la línea.
Compilaba sin warnings, pasaba los sanitizers, y daba mal: un único pasillo
con las 50.000 líneas. El bloque se devuelve a la arena al final de cada
vuelta y se reutiliza, así que todas las vistas terminaban mirando la misma
memoria reciclada.

Es exactamente el contrato de vida útil que documenta `safestr.h`, y vale la
pena tenerlo presente porque **ninguna herramienta lo detecta**: no es un
acceso inválido, es memoria legítimamente reutilizada. La regla: si el dato
tiene que sobrevivir al bloque donde nació, materialízalo con
`ss_from_view()`.

---

## Integrar en el CI

El workflow vive en `.github/workflows/ci.yml` y corre en cada push: Linux
con gcc y clang, macOS, Windows con MSVC, y un job aparte que verifica que
el parche de la propuesta siga aplicando limpio.

`ci/ci-extras.yml` es otra cosa: pasos sueltos para pegar dentro del
`ci.yml` del repositorio original de safestr, si algún día estos módulos se
integran ahí. No se usa en este repositorio.

Tres notas:

- El paso `uso_parcial` con `-Werror` (y `/W4 /WX` en MSVC) es el que
  atrapa cualquier función `static` sin usar que se cuele en los headers.
  Por eso todo en ellos es `static inline`: con `static` a secas, un usuario
  que incluya el header y use solo parte de la API se come un warning por
  cada función que no llame, y con `/WX` eso es un error en **su** proyecto.
- El test de hilos usa pthreads y no corre en Windows. Si hace falta esa
  cobertura, hay que reescribirlo con `CreateThread`.
- Los runners de GitHub Actions con `ubuntu-latest` pueden tener el mismo
  problema de ASLR con ThreadSanitizer. Si el job falla con "unexpected
  memory mapping", el paso necesita `setarch $(uname -m) -R` delante del
  binario, igual que el Makefile.

**MSVC está verificado.** El CI compila y corre los tests, el uso parcial
con `/W4 /WX`, la versión C++ y los dos ejemplos con `cl`. Lo que eso cubre
y ningún otro entorno prueba: la aritmética con `uintptr_t`, el bit de marca
en la cabecera de la arena, y la rama `__declspec(thread)` del asignador por
hilo de safestr.

Lo único que sigue sin cobertura en Windows es el test de hilos, porque usa
pthreads.

---

## La propuesta upstream

`propuesta/` contiene un cambio sugerido a `safestr`: agregar
`ss_set_allocator_ex2`, una variante del hook que además recibe el tamaño
anterior del bloque. La librería ya lo conoce (`s->capacity`), así que
pasarlo es gratis, y le ahorra al asignador tener que guardarlo en una
cabecera por bloque.

El efecto es mayor de lo que parece: la cabecera es un `size_t`, así que
obliga a alinear todo a 8 y a redondear cada tamaño. Sin ella, la alineación
se calcula desde el tamaño pedido —en C `sizeof` siempre es múltiplo de
`alignof`, así que un bloque de 13 bytes solo admite alineación 1— y el
sobrecosto por bloque pasa de **50% a cero**.

En tiempo la versión sin cabecera es algo más lenta en la mediana y bastante
más estable en el peor caso. **No es una optimización de velocidad**, y la
propuesta lo dice explícitamente: el argumento es memoria y predictibilidad.

El parche son 8 líneas en `safestr.h` y 28 en `safestr.c`, es aditivo, y no
cambia la ABI de ninguna estructura pública.

---

## Licencia

MIT, la misma de safestr.
