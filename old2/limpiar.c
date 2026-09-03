/*
 * limpiar - lee texto por stdin y lo deja presentable.
 *
 * Quita espacios sobrantes, salta lineas vacias y comentarios (#), y numera
 * lo que queda.
 *
 * Las lineas pueden medir lo que sea: ss_read_line acumula hasta el salto.
 *
 *   cc -std=c11 -Wall -Wextra limpiar.c safestr.c -o limpiar
 *   ./limpiar < archivo.txt
 */

#include "safestr.h"

#include <stdio.h>

int main(void)
{
    SafeString linea = ss_new();
    size_t numero = 0, leidas = 0, mas_larga = 0;

    while (ss_read_line(stdin, &linea))
    {
        leidas++;

        if (ss_len(&linea) > mas_larga)
            mas_larga = ss_len(&linea);

        ss_trim(&linea);

        if (ss_is_empty(&linea) || ss_starts_with(&linea, "#"))
            continue;

        if (!ss_ok(&linea))
        {
            fprintf(stderr, "limpiar: sin memoria\n");
            ss_free(&linea);
            return 1;
        }

        printf("%3zu | %s\n", ++numero, ss_cstr(&linea));
    }

    fprintf(stderr, "\n%zu lineas leidas, %zu utiles, la mas larga tenia %zu caracteres\n",
            leidas, numero, mas_larga);

    ss_free(&linea);
    return 0;
}
