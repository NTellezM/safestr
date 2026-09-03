#include <cs50.h>
#include <stdio.h>
#include "safestr.h"

int main(void)
{
    // Capturar la entrada del usuario
    string input = get_string("Ingresa tu correo electrónico: ");

    SafeString correo = ss_new();
    ss_from_cstr(&correo, input);
    ss_trim(&correo);

    size_t pos;
    // Buscar la posición del símbolo '@'
    if (ss_find(&correo, "@", &pos))
    {
        SafeString usuario = ss_new();
        SafeString dominio = ss_new();

        // Extraer el usuario (desde el inicio hasta el '@')
        ss_substring(&correo, 0, pos, &usuario);
        
        // Extraer el dominio (desde el carácter siguiente al '@' hasta el final)
        ss_substring(&correo, pos + 1, correo.length, &dominio);

        printf("Usuario : %s\n", usuario.data);
        printf("Dominio : %s\n", dominio.data);

        // Liberar la memoria de las subcadenas
        ss_free(&usuario);
        ss_free(&dominio);
    }
    else
    {
        printf("Formato de correo inválido.\n");
    }

    // Liberar la memoria de la cadena principal
    ss_free(&correo);

    return 0;
}