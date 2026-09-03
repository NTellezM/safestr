#include "../safestr.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(condicion, mensaje) do { \
    tests_run++; \
    if (condicion) { \
        tests_passed++; \
    } else { \
        printf("  FALLO: %s (linea %d)\n", mensaje, __LINE__); \
    } \
} while (0)

void test_estres_memoria(void)
{
    printf("test_estres_memoria\n");
    SafeString s = ss_new();
    bool exito = true;
    
    // Forzar redimensionamientos masivos continuos
    for (int i = 0; i < 10000; i++) {
        if (!ss_append(&s, "test_")) exito = false;
    }
    ASSERT(exito == true, "10000 appends consecutivos sin fallar por memoria");
    ASSERT(s.length == 50000, "Longitud exacta tras estres masivo");
    ASSERT(s.capacity >= 50001, "Capacidad cubre la longitud final");
    ss_free(&s);
}

void test_abuso_nulos(void)
{
    printf("test_abuso_nulos\n");
    SafeString s = ss_new();
    size_t pos;

    // Ejecutar todas las funciones con NULL para asegurar que la libreria no colapse
    ASSERT(ss_append(NULL, "texto") == false, "ss_append: struct nulo");
    ASSERT(ss_append(&s, NULL) == false, "ss_append: texto nulo");
    ASSERT(ss_from_cstr(NULL, "texto") == false, "ss_from_cstr: struct nulo");
    ASSERT(ss_from_cstr(&s, NULL) == false, "ss_from_cstr: texto nulo");
    ASSERT(ss_equals(NULL, &s) == false, "ss_equals: a nulo");
    ASSERT(ss_equals(&s, NULL) == false, "ss_equals: b nulo");
    ASSERT(ss_find(NULL, "a", &pos) == false, "ss_find: struct nulo");
    ASSERT(ss_find(&s, NULL, &pos) == false, "ss_find: texto nulo");
    ASSERT(ss_find(&s, "a", NULL) == false, "ss_find: puntero pos nulo");
    
    SafeString out;
    ASSERT(ss_substring(NULL, 0, 1, &out) == false, "ss_substring: struct nulo");
    ASSERT(ss_substring(&s, 0, 1, NULL) == false, "ss_substring: struct out nulo");

    ASSERT(ss_insert(NULL, 0, "a") == false, "ss_insert: struct nulo");
    ASSERT(ss_insert(&s, 0, NULL) == false, "ss_insert: texto nulo");

    ss_trim(NULL); 
    ss_free(NULL); 
    ASSERT(true, "ss_trim y ss_free procesan NULL silenciosamente sin segmentation fault");
    
    ss_free(&s);
}

void test_limites_indices(void)
{
    printf("test_limites_indices\n");
    SafeString s = ss_new();
    ss_from_cstr(&s, "limites");

    ASSERT(ss_insert(&s, 99999, "fuera") == false, "ss_insert: indice muy superior a length");
    
    SafeString out = ss_new();
    ASSERT(ss_substring(&s, 5, 3, &out) == false, "ss_substring: indice inicio mayor que fin");
    ASSERT(ss_substring(&s, 0, 99999, &out) == false, "ss_substring: indice fin superior a length");
    ASSERT(ss_substring(&s, s.length, s.length, &out) == true, "ss_substring: inicio y fin en el limite maximo exacto");
    ASSERT(out.length == 0, "ss_substring: rango en el limite maximo retorna vacio");

    ss_free(&s);
    ss_free(&out);
}

void test_trim_caracteres_escape(void)
{
    printf("test_trim_caracteres_escape\n");
    SafeString s = ss_new();
    
    // Tabulaciones, saltos de linea y retornos de carro
    ss_from_cstr(&s, " \t \n \r \v \f ");
    ss_trim(&s);
    ASSERT(s.length == 0, "ss_trim: limpieza total de caracteres de escape");

    ss_from_cstr(&s, "\n\tcentro\r\n");
    ss_trim(&s);
    ASSERT(strcmp(s.data, "centro") == 0, "ss_trim: aísla texto entre escapes");

    ss_free(&s);
}

int main(void)
{
    test_estres_memoria();
    test_abuso_nulos();
    test_limites_indices();
    test_trim_caracteres_escape();

    printf("\nTESTS AGRESIVOS: %d/%d pasaron\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}