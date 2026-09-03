#include <cs50.h>
#include <stdio.h>
#include "safestr.h"

int main(void)
{
    // 1. Capturar entrada del usuario
    string input = get_string("Ingresa un texto con espacios a los lados: ");

    // 2. Inicializar y cargar en la estructura dinámica
    SafeString mensaje = ss_new();
    ss_from_cstr(&mensaje, input);

    // 3. Procesar el texto
    ss_trim(&mensaje);
    ss_insert(&mensaje, 0, "Procesado: [");
    ss_append(&mensaje, "]");

    // 4. Imprimir resultados
    printf("\n%s\n", mensaje.data);
    printf("Longitud: %zu | Capacidad: %zu\n", mensaje.length, mensaje.capacity);

    // 5. Liberar memoria
    ss_free(&mensaje);

    return 0;
}