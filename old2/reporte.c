/*
 * reporte - lee un CSV de avance de tareas por stdin y arma un informe de
 *           terminal: barras de progreso, desviacion plan vs real, costos
 *           con separador de miles y totales.
 *
 *   cc -std=c11 -Wall -Wextra reporte.c safestr.c -o reporte
 *   ./reporte < avance.csv
 *   ./reporte hormigon < avance.csv     # solo tareas que contengan "hormigon"
 *
 * Formato esperado: id,tarea,plan,real,costo
 * Se aceptan campos entre comillas (que pueden contener comas), lineas que
 * empiezan con #, espacios sobrantes en cualquier campo y lineas de cualquier
 * largo. La cabecera se detecta y se salta sola.
 */

#include "safestr.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_CAMPOS   5
#define ANCHO_TAREA 26
#define ANCHO_BARRA 20

/* Colores ANSI. Se apagan solos si la salida no es una terminal, para que
   `./reporte | less` o `> archivo.txt` no se llenen de basura. */
static const char* C_RESET = "";
static const char* C_VERDE = "";
static const char* C_ROJO  = "";
static const char* C_AMBAR = "";
static const char* C_GRIS  = "";

/* ------------------------------------------------------------------ */
/* Parseo CSV con comillas: "Excavacion, sector norte" es UN campo.    */
/* Devuelve cuantos campos escribio en `campos`.                       */
/* ------------------------------------------------------------------ */
static size_t separar_csv(const SafeString* linea, SafeString campos[], size_t max)
{
    const char* p = ss_cstr(linea);
    size_t n = 0;
    bool dentro_de_comillas = false;

    ss_clear(&campos[0]);

    for (size_t i = 0; p[i] != '\0'; i++)
    {
        char c = p[i];

        if (c == '"')
        {
            if (dentro_de_comillas && p[i + 1] == '"')   /* "" es una comilla literal */
            {
                ss_append_char(&campos[n], '"');
                i++;
            }
            else
            {
                dentro_de_comillas = !dentro_de_comillas;
            }
        }
        else if (c == ',' && !dentro_de_comillas)
        {
            if (n + 1 >= max)
                break;
            ss_clear(&campos[++n]);
        }
        else
        {
            ss_append_char(&campos[n], c);
        }
    }

    for (size_t i = 0; i <= n; i++)
        ss_trim(&campos[i]);

    return n + 1;
}

/* ------------------------------------------------------------------ */
/* 1234567 -> 1.234.567, insertando de derecha a izquierda.            */
/* ------------------------------------------------------------------ */
static void formatear_miles(SafeString* numero)
{
    size_t pos = ss_len(numero);

    while (pos > 3)
    {
        pos -= 3;
        ss_insert(numero, pos, ".");
    }
}

/* ------------------------------------------------------------------ */
/* Encaja un texto en un ancho fijo: rellena con espacios o recorta.   */
/* ------------------------------------------------------------------ */
static void columna(SafeString* fila, const SafeString* texto, size_t ancho)
{
    if (ss_len(texto) > ancho)
    {
        SafeString corto = ss_slice(texto, 0, ancho - 3);
        ss_append_ss(fila, &corto);
        ss_append(fila, "...");
        ss_free(&corto);
    }
    else
    {
        ss_append_ss(fila, texto);
        for (size_t i = ss_len(texto); i < ancho; i++)
            ss_append_char(fila, ' ');
    }
}

/* Copia en minusculas, para que el filtro no distinga mayusculas. */
static SafeString minusculas(const char* texto)
{
    SafeString r = ss_from(texto);

    for (size_t i = 0; i < ss_len(&r); i++)
        r.data[i] = (char) tolower((unsigned char) r.data[i]);

    return r;
}

static void barra(SafeString* fila, long porcentaje, size_t ancho)
{
    if (porcentaje < 0)   porcentaje = 0;
    if (porcentaje > 100) porcentaje = 100;

    size_t llenos = (size_t) ((long) ancho * porcentaje / 100);

    ss_append_char(fila, '[');
    for (size_t i = 0; i < ancho; i++)
        ss_append(fila, i < llenos ? "#" : "-");
    ss_append_char(fila, ']');
}

/* ------------------------------------------------------------------ */

int main(int argc, char* argv[])
{
    if (isatty(STDOUT_FILENO))
    {
        C_RESET = "\033[0m";  C_VERDE = "\033[32m"; C_ROJO = "\033[31m";
        C_AMBAR = "\033[33m"; C_GRIS  = "\033[90m";
    }

    const char* filtro = (argc > 1) ? argv[1] : NULL;
    SafeString filtro_min = minusculas(filtro != NULL ? filtro : "");

    SafeString linea  = ss_new();
    SafeString fila   = ss_new();
    SafeString costo  = ss_new();
    SafeString peor   = ss_new();          /* nombre de la tarea mas atrasada */
    SafeString campos[MAX_CAMPOS];

    for (size_t i = 0; i < MAX_CAMPOS; i++)
        campos[i] = ss_new();

    ss_reserve(&fila, 256);                /* la fila se reusa en cada vuelta */

    size_t mostradas = 0, saltadas = 0;
    long total_plan = 0, total_real = 0, total_costo = 0, peor_delta = 0;

    printf("%s  %-*s %-*s  %6s %6s %7s  %12s%s\n", C_GRIS,
           ANCHO_TAREA, "TAREA", (int) ANCHO_BARRA + 2, "AVANCE",
           "PLAN", "REAL", "DESV", "COSTO", C_RESET);

    while (ss_read_line(stdin, &linea))
    {
        ss_trim(&linea);

        if (ss_is_empty(&linea) || ss_starts_with(&linea, "#"))
            continue;

        if (separar_csv(&linea, campos, MAX_CAMPOS) < MAX_CAMPOS)
        {
            saltadas++;
            continue;
        }

        if (ss_equals_cstr(&campos[0], "id"))       /* cabecera */
            continue;

        if (filtro != NULL)
        {
            SafeString tarea_min = minusculas(ss_cstr(&campos[1]));
            bool coincide = ss_contains(&tarea_min, ss_cstr(&filtro_min));
            ss_free(&tarea_min);

            if (!coincide)
                continue;
        }

        long plan  = strtol(ss_cstr(&campos[2]), NULL, 10);
        long real  = strtol(ss_cstr(&campos[3]), NULL, 10);
        long valor = strtol(ss_cstr(&campos[4]), NULL, 10);
        long delta = real - plan;

        total_plan += plan;
        total_real += real;
        total_costo += valor;
        mostradas++;

        if (delta < peor_delta)
        {
            peor_delta = delta;
            ss_set(&peor, ss_cstr(&campos[1]));
        }

        ss_set(&costo, ss_cstr(&campos[4]));
        formatear_miles(&costo);

        const char* color = (delta >= 0) ? C_VERDE : (delta >= -5 ? C_AMBAR : C_ROJO);

        ss_clear(&fila);
        ss_append(&fila, "  ");
        columna(&fila, &campos[1], ANCHO_TAREA);
        ss_append_char(&fila, ' ');
        barra(&fila, real, ANCHO_BARRA);
        ss_appendf(&fila, " %5ld%% %5ld%% ", plan, real);
        ss_appendf(&fila, "%s%+6ld%s", color, delta, C_RESET);
        ss_appendf(&fila, "  %10s", ss_cstr(&costo));

        if (!ss_ok(&fila))
        {
            fprintf(stderr, "reporte: sin memoria\n");
            return 1;
        }

        printf("%s\n", ss_cstr(&fila));
    }

    /* ---- resumen ---- */

    ss_set(&costo, "");
    ss_appendf(&costo, "%ld", total_costo);
    formatear_miles(&costo);

    SafeString resumen = ss_new();
    ss_appendf(&resumen, "%zu tarea%s", mostradas, mostradas == 1 ? "" : "s");
    if (filtro != NULL)
        ss_appendf(&resumen, " con \"%s\"", filtro);
    if (saltadas > 0)
        ss_appendf(&resumen, ", %zu linea%s mal formada%s",
                   saltadas, saltadas == 1 ? "" : "s", saltadas == 1 ? "" : "s");

    if (mostradas > 0)
    {
        long dp = total_plan / (long) mostradas;
        long dr = total_real / (long) mostradas;
        ss_appendf(&resumen, "  |  plan %ld%%, real %ld%%", dp, dr);
        ss_appendf(&resumen, "  |  costo %s", ss_cstr(&costo));

        if (peor_delta < 0)
            ss_appendf(&resumen, "\n  %sMayor atraso:%s %s (%+ld puntos)",
                       C_ROJO, C_RESET, ss_cstr(&peor), peor_delta);
    }

    printf("%s  %s\n", C_GRIS, "");
    printf("  %s%s\n", ss_cstr(&resumen), C_RESET);

    /* ---- limpieza ---- */

    for (size_t i = 0; i < MAX_CAMPOS; i++)
        ss_free(&campos[i]);

    ss_free(&linea);
    ss_free(&fila);
    ss_free(&costo);
    ss_free(&peor);
    ss_free(&resumen);
    ss_free(&filtro_min);
    return 0;
}
