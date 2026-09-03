/*
 * plantilla.c - ejemplo practico de safestr
 *
 * Rellena una plantilla de texto con variables leidas de un archivo de
 * configuracion. Es el "sobre molde" que uno termina escribiendo en
 * cualquier proyecto: generar un correo, un archivo de despliegue, un
 * reporte.
 *
 *     config.ini                    plantilla.txt
 *     -----------                   -------------
 *     # comentario                  Hola {{nombre}},
 *     nombre = Nelson               tu pedido {{pedido}} sale de {{bodega}}.
 *     pedido = ORD-4471
 *     bodega = PAS03-C12
 *
 *     ./plantilla config.ini plantilla.txt
 *     -> Hola Nelson,
 *        tu pedido ORD-4471 sale de PAS03-C12.
 *
 * Modos:
 *     ./plantilla <config> <plantilla>      rellena y escribe a stdout
 *     ./plantilla --check <config> <plant>  solo revisa, no escribe
 *     ./plantilla --demo                    crea archivos de ejemplo y corre
 *
 * Que ejercita de la API
 * ----------------------
 *   ss_read_line   leer lineas de cualquier largo, reutilizando el buffer
 *   ss_index_of    encontrar el '=' de cada linea de config
 *   ss_view_slice  partir clave y valor SIN copiar
 *   sv_trim        limpiar espacios sobre la vista, tampoco copia
 *   ss_from_view   materializar lo que tiene que sobrevivir a la linea
 *   ss_append_view construir la salida por segmentos, en una sola pasada
 *   ss_starts_with saltar comentarios
 *   ss_hash        indice rapido por clave, sin comparar cadenas de mas
 *   SS_AUTO        liberacion automatica al salir del bloque
 *
 * Compilar (desde la raiz del repositorio):
 *     cc -std=c17 -O2 -I. ejemplo/plantilla.c safestr.c -o plantilla
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "safestr.h"

#define MAX_VARS 128

typedef struct
{
    SafeString clave;
    SafeString valor;
    uint32_t   hash;      /* evita comparar cadenas cuando no coinciden */
} Variable;

typedef struct
{
    Variable v[MAX_VARS];
    size_t   n;
} Config;

/* ------------------------------------------------------------------ */
/* Leer el archivo de configuracion                                    */
/* ------------------------------------------------------------------ */

/* Formato tolerante: "clave = valor", espacios opcionales, lineas vacias
   y comentarios con # o ; se ignoran. Una linea sin '=' es un error que se
   reporta con su numero, no una que se traga en silencio. */
static bool leer_config(FILE* f, Config* c, long* errores)
{
    SS_AUTO SafeString linea = ss_new();
    long n_linea = 0;
    *errores = 0;

    while (ss_read_line(f, &linea))
    {
        n_linea++;
        ss_trim(&linea);

        if (ss_is_empty(&linea)) continue;
        if (ss_starts_with(&linea, "#")) continue;
        if (ss_starts_with(&linea, ";")) continue;

        size_t igual = ss_index_of(&linea, "=");
        if (igual == SS_NPOS)
        {
            fprintf(stderr, "config linea %ld: falta '=' en \"%s\"\n",
                    n_linea, ss_cstr(&linea));
            (*errores)++;
            continue;
        }

        /* Clave y valor como vistas: cero reservas hasta aqui. */
        SafeView clave = sv_trim(ss_view_slice(&linea, 0, igual));
        SafeView valor = sv_trim(ss_view_slice(&linea, igual + 1, ss_len(&linea)));

        if (sv_is_empty(clave))
        {
            fprintf(stderr, "config linea %ld: clave vacia\n", n_linea);
            (*errores)++;
            continue;
        }

        if (c->n == MAX_VARS)
        {
            fprintf(stderr, "config linea %ld: mas de %d variables\n",
                    n_linea, MAX_VARS);
            (*errores)++;
            break;
        }

        /* Las vistas mueren con `linea`, que se reutiliza en la vuelta
           siguiente. Lo que tiene que sobrevivir se materializa. */
        Variable* var = &c->v[c->n++];
        var->clave = ss_from_view(clave);
        var->valor = ss_from_view(valor);
        var->hash  = ss_hash(&var->clave);

        if (!ss_ok(&var->clave) || !ss_ok(&var->valor))
        {
            fprintf(stderr, "sin memoria leyendo la config\n");
            return false;
        }
    }

    return true;
}

static void liberar_config(Config* c)
{
    for (size_t i = 0; i < c->n; i++)
    {
        ss_free(&c->v[i].clave);
        ss_free(&c->v[i].valor);
    }
    c->n = 0;
}

/* ------------------------------------------------------------------ */
/* Rellenar la plantilla                                               */
/* ------------------------------------------------------------------ */

/* Devuelve el valor de una clave, o NULL si no esta definida.
   El hash descarta la mayoria de los candidatos sin comparar texto. */
static const SafeString* buscar(const Config* c, SafeView clave)
{
    uint32_t h = sv_hash(clave);
    for (size_t i = 0; i < c->n; i++)
        if (c->v[i].hash == h && sv_equals(ss_view(&c->v[i].clave), clave))
            return &c->v[i].valor;
    return NULL;
}

/* Recorre el texto UNA vez, construyendo la salida a medida que avanza.
 *
 * La version obvia -reemplazar cada marcador dentro del texto original- es
 * cuadratica: cada sustitucion copia el texto entero. Con 20.000 marcadores
 * eso tardaba 474 ms. Construyendo la salida por segmentos es lineal: 6 ms.
 *
 * Los marcadores sin definir se reportan y se dejan tal cual en la salida,
 * en vez de desaparecer: una plantilla a medio rellenar en silencio es peor
 * que uno visible.
 */
static long rellenar(SafeString* texto, const Config* c, bool solo_revisar)
{
    SafeString salida = ss_new();
    const size_t largo = ss_len(texto);
    size_t pos = 0;
    long faltantes = 0;

    while (pos < largo)
    {
        SafeView resto = ss_view_slice(texto, pos, largo);

        size_t rel = sv_index_of(resto, sv("{{"));
        if (rel == SS_NPOS)
        {
            ss_append_view(&salida, resto);
            break;
        }

        size_t abre = pos + rel;
        SafeView cola = ss_view_slice(texto, abre + 2, largo);
        size_t rel_cierra = sv_index_of(cola, sv("}}"));
        if (rel_cierra == SS_NPOS)
        {
            fprintf(stderr, "plantilla: '{{' sin cerrar en la posicion %zu\n", abre);
            faltantes++;
            ss_append_view(&salida, resto);
            break;
        }

        size_t cierra = abre + 2 + rel_cierra;
        SafeView clave = sv_trim(ss_view_slice(texto, abre + 2, cierra));

        /* lo que va antes del marcador */
        ss_append_view(&salida, ss_view_slice(texto, pos, abre));

        const SafeString* valor = buscar(c, clave);
        if (valor == NULL)
        {
            fprintf(stderr, "plantilla: variable no definida: {{" SV_FMT "}}\n",
                    SV_ARG(clave));
            faltantes++;
            /* se deja el marcador visible en la salida */
            ss_append_view(&salida, ss_view_slice(texto, abre, cierra + 2));
        }
        else
        {
            ss_append_ss(&salida, valor);
        }

        pos = cierra + 2;
    }

    if (!ss_ok(&salida))
    {
        fprintf(stderr, "sin memoria rellenando la plantilla\n");
        ss_free(&salida);
        return -1;
    }

    if (!solo_revisar)
    {
        ss_free(texto);        /* el original se descarta */
        *texto = salida;       /* y la salida toma su lugar, sin copiar */
    }
    else
    {
        ss_free(&salida);
    }

    return faltantes;
}

/* ------------------------------------------------------------------ */

static int correr(const char* ruta_config, const char* ruta_plantilla,
                  bool solo_revisar)
{
    FILE* fc = fopen(ruta_config, "r");
    if (fc == NULL) { perror(ruta_config); return 1; }

    Config c = { {{ SS_INIT, SS_INIT, 0 }}, 0 };
    long errores_config = 0;
    bool ok = leer_config(fc, &c, &errores_config);
    fclose(fc);

    if (!ok) { liberar_config(&c); return 1; }

    FILE* fp = fopen(ruta_plantilla, "r");
    if (fp == NULL) { perror(ruta_plantilla); liberar_config(&c); return 1; }

    SS_AUTO SafeString texto = ss_new();
    SS_AUTO SafeString linea = ss_new();
    bool primera = true;
    while (ss_read_line(fp, &linea))
    {
        if (!primera) ss_append_char(&texto, '\n');
        ss_append_ss(&texto, &linea);
        primera = false;
    }
    fclose(fp);

    if (!ss_ok(&texto))
    {
        fprintf(stderr, "sin memoria leyendo la plantilla\n");
        liberar_config(&c);
        return 1;
    }

    long faltantes = rellenar(&texto, &c, solo_revisar);
    if (faltantes < 0) { liberar_config(&c); return 1; }

    if (solo_revisar)
    {
        fprintf(stderr, "%zu variables definidas, %ld marcador(es) sin resolver",
                c.n, faltantes);
        if (errores_config)
            fprintf(stderr, ", %ld linea(s) de config con error", errores_config);
        fprintf(stderr, "\n");
    }
    else
    {
        printf("%s\n", ss_cstr(&texto));
    }

    liberar_config(&c);
    return (faltantes == 0 && errores_config == 0) ? 0 : 1;
}

/* ------------------------------------------------------------------ */

static int demo(void)
{
    FILE* f = fopen("demo_config.ini", "w");
    if (f == NULL) { perror("demo_config.ini"); return 1; }
    fputs("# configuracion de ejemplo\n"
          "nombre  = Nelson\n"
          "pedido  = ORD-4471\n"
          "bodega  = PAS03-C12-N02\n"
          "\n"
          "; los comentarios y las lineas vacias se ignoran\n"
          "unidades = 340\n", f);
    fclose(f);

    f = fopen("demo_plantilla.txt", "w");
    if (f == NULL) { perror("demo_plantilla.txt"); return 1; }
    fputs("Hola {{nombre}},\n"
          "\n"
          "Tu pedido {{ pedido }} ya esta preparado.\n"
          "Ubicacion: {{bodega}} ({{unidades}} unidades).\n"
          "\n"
          "-- Sistema de bodega\n", f);
    fclose(f);

    printf("=== demo_config.ini y demo_plantilla.txt creados ===\n\n");
    return correr("demo_config.ini", "demo_plantilla.txt", false);
}

static void uso(const char* prog)
{
    fprintf(stderr,
        "uso:\n"
        "  %s <config> <plantilla>           rellena y escribe a stdout\n"
        "  %s --check <config> <plantilla>   solo revisa, no escribe\n"
        "  %s --demo                         crea archivos de ejemplo y corre\n",
        prog, prog, prog);
}

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--demo") == 0)
        return demo();

    if (argc == 4 && strcmp(argv[1], "--check") == 0)
        return correr(argv[2], argv[3], true);

    if (argc == 3)
        return correr(argv[1], argv[2], false);

    uso(argv[0]);
    return 2;
}
