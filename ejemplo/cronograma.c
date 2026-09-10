/*
 * cronograma.c - analiza un export de Primavera P6 en TSV
 *
 * Lee la tabla TASK de un XER exportado y resume, por paquete de WBS:
 * cuantas actividades hay, cuantas terminadas, y la desviacion de termino
 * contra la linea base (base_end_date vs end_date) en dias.
 *
 *     ./cronograma task.tsv
 *     ./cronograma --arena task.tsv     usa una arena
 *     ./cronograma --bench task.tsv N   repite N veces y mide
 *
 * Columnas esperadas (por nombre, en la primera linea):
 *   task_code status_code wbs_id wbs_name task_name
 *   base_start_date base_end_date start_date end_date ...
 *
 * Las fechas vienen como "2025-02-25 08:00:00" o vacias.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "safestr.h"
#include "safestr_arena.h"
#include "safestr_split.h"

#define MAX_WBS 256
#define MAX_COLS 32

typedef struct
{
    SafeString id;
    SafeString nombre;
    long n, terminadas, en_curso, no_iniciadas;
    /* La desviacion se lleva separada por estado. Promediar lo terminado
       junto con lo pendiente esconde justo lo que hay que mirar: en un
       proyecto avanzado, lo cerrado tiende a cero y todo el atraso vive
       en lo que queda. */
    long con_base_term, desv_term;      /* actividades terminadas   */
    long con_base_rest, desv_rest;      /* en curso + no iniciadas  */
    long sin_base;                      /* sin linea base o sin fin */
    long peor;                          /* mayor atraso individual  */
    SafeString peor_act;
    uint32_t   hash;        /* descarta candidatos sin comparar texto */
} Wbs;

static Wbs wbs[MAX_WBS];
static size_t n_wbs = 0;

/* "2025-02-25 08:00:00" -> dias desde una epoca arbitraria.
   Solo interesa la diferencia entre dos fechas, no la fecha absoluta. */
static bool fecha_a_dias(SafeView v, long* out)
{
    v = sv_trim(v);
    if (sv_len_of(v) < 10) return false;

    bool ok = false;
    long a = sv_to_long(sv_slice(v, 0, 4), &ok);   if (!ok) return false;
    long m = sv_to_long(sv_slice(v, 5, 7), &ok);   if (!ok) return false;
    long d = sv_to_long(sv_slice(v, 8, 10), &ok);  if (!ok) return false;
    if (m < 1 || m > 12 || d < 1 || d > 31) return false;

    /* dias julianos, formula de Fliegel-Van Flandern */
    long jm = (m - 14) / 12;
    *out = (1461 * (a + 4800 + jm)) / 4
         + (367 * (m - 2 - 12 * jm)) / 12
         - (3 * ((a + 4900 + jm) / 100)) / 4
         + d - 32075;
    return true;
}

/* Ancho en columnas de una vista UTF-8.
 *
 * Un byte de continuacion tiene la forma 10xxxxxx y no ocupa lugar propio:
 * pertenece al caracter que empezo antes. Contar bytes en vez de columnas es
 * lo que descuadra una tabla en cuanto aparece una ñ o una é.
 *
 * Cuenta puntos de codigo, no anchos tipograficos: para nombres de WBS en
 * español es exacto. Con CJK o marcas combinantes no lo seria.
 */
static size_t ancho_utf8(SafeView v)
{
    size_t n = 0, len = sv_len_of(v);
    for (size_t i = 0; i < len; i++)
        if (((unsigned char) v.ptr[i] & 0xC0) != 0x80) n++;
    return n;
}

/* Recorta a lo sumo `max` COLUMNAS sin partir un caracter UTF-8.
 *
 * safestr trabaja con bytes, no con caracteres, y con razon: no asume
 * codificacion. Pero eso significa que %.26s de printf puede cortar una
 * secuencia multibyte por la mitad. Con nombres de WBS en español
 * ("Movilizacion", "Cañerias", "Obras Electricas") pasa constantemente:
 * la salida deja de ser UTF-8 valido y cualquier cosa que la lea despues
 * se rompe.
 *
 * Se avanza byte a byte contando solo los que empiezan un caracter, y se
 * corta al llegar al que haria la columna max+1.
 */
static SafeView recortar_utf8(SafeView v, size_t max)
{
    size_t len = sv_len_of(v), col = 0, i = 0;
    while (i < len)
    {
        if (((unsigned char) v.ptr[i] & 0xC0) != 0x80)
        {
            if (col == max) break;
            col++;
        }
        i++;
    }
    return sv_slice(v, 0, i);
}

/* La busqueda es lineal sobre los paquetes de WBS, pero comparando primero
   el hash: con 144 paquetes, eso convierte 144 comparaciones de cadena por
   fila en 144 comparaciones de entero mas, en promedio, una de cadena.
   Es lo que domina el tiempo en cronogramas grandes, no las reservas. */
static Wbs* buscar_wbs(SafeView id, SafeView nombre)
{
    uint32_t h = sv_hash(id);
    for (size_t i = 0; i < n_wbs; i++)
        if (wbs[i].hash == h && sv_equals(ss_view(&wbs[i].id), id))
            return &wbs[i];

    if (n_wbs == MAX_WBS) return NULL;
    Wbs* w = &wbs[n_wbs++];
    memset(w, 0, sizeof *w);
    w->hash = h;
    w->id = ss_from_view(id);
    w->nombre = ss_from_view(nombre);
    w->peor_act = ss_new();
    return w;
}

/* Ubica una columna por nombre en la linea de cabecera. */
static size_t columna(const SafeSplit* cab, const char* nombre)
{
    for (size_t i = 0; i < cab->count; i++)
        if (sv_equals_cstr(sv_trim(ss_packed_view(cab, i)), nombre))
            return i;
    return SS_NPOS;
}

/* Ordena de mayor a menor desviacion de lo pendiente. Los paquetes sin
   nada pendiente van al final: ya no hay decision que tomar sobre ellos. */
static int por_desviacion_restante(const void* pa, const void* pb)
{
    const Wbs* a = (const Wbs*) pa;
    const Wbs* b = (const Wbs*) pb;
    double da = a->con_base_rest ? (double) a->desv_rest / (double) a->con_base_rest : -1e9;
    double db = b->con_base_rest ? (double) b->desv_rest / (double) b->con_base_rest : -1e9;
    if (da < db) return 1;
    if (da > db) return -1;
    return (a->peor < b->peor) - (a->peor > b->peor);
}

static int procesar(FILE* f, bool callado)
{
    SafeString linea = ss_new();

    if (!ss_read_line(f, &linea))
    {
        fprintf(stderr, "archivo vacio\n");
        ss_free(&linea);
        return 1;
    }

    SafeSplit cab = ss_split_packed(&linea, "\t");
    size_t c_estado = columna(&cab, "status_code");
    size_t c_wbs_id = columna(&cab, "wbs_id");
    size_t c_wbs_nm = columna(&cab, "wbs_name");
    size_t c_code   = columna(&cab, "task_code");
    size_t c_base   = columna(&cab, "base_end_date");
    size_t c_fin    = columna(&cab, "end_date");
    ss_split_packed_free(&cab);

    if (c_estado == SS_NPOS || c_wbs_id == SS_NPOS ||
        c_base == SS_NPOS || c_fin == SS_NPOS)
    {
        fprintf(stderr, "faltan columnas esperadas en la cabecera\n");
        ss_free(&linea);
        return 1;
    }

    long n_filas = 0, malformadas = 0;

    while (ss_read_line(f, &linea))
    {
        if (ss_is_empty(&linea)) continue;

        SafeSplit c = ss_split_packed(&linea, "\t");
        if (c.error || c.count <= c_fin) { malformadas++; ss_split_packed_free(&c); continue; }

        Wbs* w = buscar_wbs(ss_packed_view(&c, c_wbs_id),
                            c_wbs_nm == SS_NPOS ? SV_NULA : ss_packed_view(&c, c_wbs_nm));
        if (w == NULL) { ss_split_packed_free(&c); continue; }

        w->n++;
        SafeView estado = ss_packed_view(&c, c_estado);
        bool terminada = sv_equals_cstr(estado, "Completed");
        if (terminada)                                  w->terminadas++;
        else if (sv_equals_cstr(estado, "In Progress")) w->en_curso++;
        else                                            w->no_iniciadas++;

        long d_base, d_fin;
        if (fecha_a_dias(ss_packed_view(&c, c_base), &d_base) &&
            fecha_a_dias(ss_packed_view(&c, c_fin), &d_fin))
        {
            long desv = d_fin - d_base;
            if (terminada) { w->con_base_term++; w->desv_term += desv; }
            else           { w->con_base_rest++; w->desv_rest += desv; }

            if (desv > w->peor)
            {
                w->peor = desv;
                if (c_code != SS_NPOS)
                    ss_set_view(&w->peor_act, ss_packed_view(&c, c_code));
            }
        }
        else
        {
            w->sin_base++;
        }

        ss_split_packed_free(&c);
        n_filas++;
    }

    ss_free(&linea);

    if (callado) return 0;

    /* Se ordena por la desviacion de lo que QUEDA: es lo unico sobre lo que
       todavia se puede actuar. Lo terminado va como referencia. */
    qsort(wbs, n_wbs, sizeof(Wbs), por_desviacion_restante);

    printf("%-30s %-28s %4s %4s %4s %4s %9s %9s %6s %s\n",
           "WBS", "nombre", "act", "term", "curs", "nini",
           "term prom", "rest prom", "peor", "actividad");
    for (int i = 0; i < 30; i++) putchar('-');
    putchar(' ');
    for (int i = 0; i < 28; i++) putchar('-');
    printf(" ---- ---- ---- ---- --------- --------- ------ ---------\n");

    long tot = 0, sin_base = 0, peor_global = 0;
    long ct = 0, dt = 0, cr = 0, dr = 0;

    for (size_t i = 0; i < n_wbs; i++)
    {
        Wbs* w = &wbs[i];
        SafeView nombre = recortar_utf8(ss_view(&w->nombre), 28);

        printf("%-30.30s " SV_FMT "%*s %4ld %4ld %4ld %4ld",
               ss_cstr(&w->id), SV_ARG(nombre),
               (int)(28 - ancho_utf8(nombre)), "",
               w->n, w->terminadas, w->en_curso, w->no_iniciadas);

        if (w->con_base_term)
            printf(" %8.1fd", (double) w->desv_term / (double) w->con_base_term);
        else
            printf(" %9s", "-");

        if (w->con_base_rest)
            printf(" %8.1fd", (double) w->desv_rest / (double) w->con_base_rest);
        else
            printf(" %9s", "-");

        printf(" %5ldd %s\n", w->peor, ss_cstr(&w->peor_act));

        tot += w->n;
        sin_base += w->sin_base;
        ct += w->con_base_term; dt += w->desv_term;
        cr += w->con_base_rest; dr += w->desv_rest;
        if (w->peor > peor_global) peor_global = w->peor;
    }

    printf("\n%ld actividades en %zu paquetes de WBS", tot, n_wbs);
    if (malformadas) printf(", %ld lineas descartadas", malformadas);
    printf("\n\n");

    printf("desviacion contra linea base:\n");
    if (ct) printf("  terminadas            %4ld act  %7.1f dias promedio\n",
                   ct, (double) dt / (double) ct);
    if (cr) printf("  en curso / pendientes %4ld act  %7.1f dias promedio\n",
                   cr, (double) dr / (double) cr);
    if (ct + cr) printf("  todas                 %4ld act  %7.1f dias promedio\n",
                        ct + cr, (double)(dt + dr) / (double)(ct + cr));

    if (sin_base)
        printf("\n%ld actividades sin linea base o sin fecha de termino: fuera del calculo\n",
               sin_base);

    printf("mayor atraso individual: %ld dias\n", peor_global);

    if (cr && ct && (double) dr / (double) cr > 3.0 * ((double) dt / (double) ct + 1.0))
        printf("\nEl promedio general esconde el problema: lo terminado cerro cerca de\n"
               "la linea base y todo el atraso esta en las %ld actividades que quedan.\n",
               cr);
    return 0;
}

static void liberar(void)
{
    for (size_t i = 0; i < n_wbs; i++)
    {
        ss_free(&wbs[i].id);
        ss_free(&wbs[i].nombre);
        ss_free(&wbs[i].peor_act);
    }
    n_wbs = 0;
}

static double ahora(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char** argv)
{
    bool usar_arena = false, bench = false;
    int reps = 1;
    const char* ruta = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--arena") == 0) usar_arena = true;
        else if (strcmp(argv[i], "--bench") == 0) bench = true;
        else if (ruta == NULL) ruta = argv[i];
        else reps = atoi(argv[i]);
    }

    if (ruta == NULL)
    {
        fprintf(stderr, "uso: %s [--arena] [--bench] <task.tsv> [repeticiones]\n", argv[0]);
        return 2;
    }

    SsArena arena;
    if (usar_arena) { ss_arena_init(&arena, 1 << 20); ss_arena_activar(&arena); }

    int r = 0;
    if (bench)
    {
        double mejor = 1e9;
        for (int i = 0; i < reps; i++)
        {
            FILE* f = fopen(ruta, "r");
            if (f == NULL) { perror(ruta); return 1; }
            double t0 = ahora();
            r = procesar(f, true);
            double t = ahora() - t0;
            if (t < mejor) mejor = t;
            fclose(f);
            liberar();
            if (usar_arena) ss_arena_reset(&arena);
        }
        printf("%-8s %d repeticiones, mejor %.4f s\n",
               usar_arena ? "arena" : "malloc", reps, mejor);
    }
    else
    {
        FILE* f = fopen(ruta, "r");
        if (f == NULL) { perror(ruta); return 1; }
        r = procesar(f, false);
        fclose(f);
        liberar();
    }

    if (usar_arena) { ss_arena_desactivar(); ss_arena_free(&arena); }
    return r;
}
