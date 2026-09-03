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

void test_ss_new(void)
{
    printf("test_ss_new\n");
    SafeString s = ss_new();
    ASSERT(s.data == NULL, "ss_new: data arranca en NULL");
    ASSERT(s.length == 0, "ss_new: length arranca en 0");
    ASSERT(s.capacity == 0, "ss_new: capacity arranca en 0");
}

void test_ss_append(void)
{
    printf("test_ss_append\n");
    SafeString s = ss_new();

    ASSERT(ss_append(&s, "hola") == true, "append basico devuelve true");
    ASSERT(s.length == 4, "length despues de un append");
    ASSERT(strcmp(s.data, "hola") == 0, "contenido correcto tras un append");

    ASSERT(ss_append(&s, " mundo") == true, "segundo append devuelve true");
    ASSERT(s.length == 10, "length acumula correctamente");
    ASSERT(strcmp(s.data, "hola mundo") == 0, "contenido correcto tras varios appends");

    ASSERT(ss_append(&s, NULL) == false, "append con text NULL devuelve false");
    ASSERT(s.length == 10, "length no cambia si append con NULL fallo");

    ASSERT(ss_append(NULL, "x") == false, "append con s NULL devuelve false");

    ASSERT(ss_append(&s, "") == true, "append de string vacio no falla");
    ASSERT(s.length == 10, "length no cambia tras append vacio");

    ASSERT(s.capacity >= s.length + 1, "capacity siempre alcanza para length + terminador");

    ss_free(&s);
}

void test_ss_free(void)
{
    printf("test_ss_free\n");
    SafeString s = ss_new();
    ss_append(&s, "algo");

    ss_free(&s);
    ASSERT(s.data == NULL, "free: data queda en NULL");
    ASSERT(s.length == 0, "free: length queda en 0");
    ASSERT(s.capacity == 0, "free: capacity queda en 0");

    ss_free(&s); // segunda vez, no deberia romper nada
    ASSERT(s.data == NULL, "doble free: sigue en NULL sin crashear");

    ss_free(NULL); // tampoco deberia romper
    ASSERT(1, "ss_free(NULL) no crashea");
}

void test_ss_from_cstr(void)
{
    printf("test_ss_from_cstr\n");
    SafeString s = ss_new(); // <-- Corrección
    ASSERT(ss_from_cstr(&s, "construido") == true, "from_cstr basico devuelve true");
    ASSERT(strcmp(s.data, "construido") == 0, "contenido correcto");
    ASSERT(s.length == 10, "length correcta");
    ss_free(&s);

    SafeString s2 = ss_new(); // <-- Corrección
    ASSERT(ss_from_cstr(&s2, NULL) == false, "from_cstr con cstr NULL devuelve false");

    ASSERT(ss_from_cstr(NULL, "x") == false, "from_cstr con s NULL devuelve false");

    SafeString s3 = ss_new(); // <-- Corrección
    ASSERT(ss_from_cstr(&s3, "") == true, "from_cstr con string vacio devuelve true");
    ASSERT(s3.length == 0, "length 0 para string vacio");
    ss_free(&s3);
}

void test_ss_equals(void)
{
    printf("test_ss_equals\n");
    SafeString a = ss_new(), b = ss_new(), c = ss_new();
    ss_from_cstr(&a, "hola");
    ss_from_cstr(&b, "hola");
    ss_from_cstr(&c, "chau");

    ASSERT(ss_equals(&a, &b) == true, "strings iguales");
    ASSERT(ss_equals(&a, &c) == false, "strings distinto contenido");

    SafeString d = ss_new();
    ss_from_cstr(&d, "ho");
    ASSERT(ss_equals(&a, &d) == false, "strings distinta longitud");

    SafeString vacio1 = ss_new(), vacio2 = ss_new();
    ASSERT(ss_equals(&vacio1, &vacio2) == true, "dos strings vacios son iguales");

    ASSERT(ss_equals(&a, NULL) == false, "equals con b NULL devuelve false");
    ASSERT(ss_equals(NULL, &a) == false, "equals con a NULL devuelve false");

    ss_free(&a); ss_free(&b); ss_free(&c); ss_free(&d);
    ss_free(&vacio1); ss_free(&vacio2);
}

void test_ss_find(void)
{
    printf("test_ss_find\n");
    SafeString s = ss_new();
    ss_from_cstr(&s, "el gato subio al techo");
    size_t pos;

    ASSERT(ss_find(&s, "gato", &pos) == true, "encuentra substring existente");
    ASSERT(pos == 3, "posicion correcta");

    ASSERT(ss_find(&s, "perro", &pos) == false, "no encuentra substring inexistente");

    ASSERT(ss_find(&s, "", &pos) == true, "busqueda vacia siempre 'encontrada'");
    ASSERT(pos == 0, "busqueda vacia da posicion 0");

    ASSERT(ss_find(NULL, "x", &pos) == false, "find con s NULL devuelve false");
    ASSERT(ss_find(&s, NULL, &pos) == false, "find con buscado NULL devuelve false");
    ASSERT(ss_find(&s, "x", NULL) == false, "find con pos NULL devuelve false");

    SafeString vacio = ss_new();
    ASSERT(ss_find(&vacio, "algo", &pos) == false, "find en string vacio no encuentra texto no vacio");

    ss_free(&s); ss_free(&vacio);
}

void test_ss_substring(void)
{
    printf("test_ss_substring\n");
    SafeString s = ss_new();
    ss_from_cstr(&s, "el gato subio al techo");

    SafeString sub = ss_new();
    ASSERT(ss_substring(&s, 3, 7, &sub) == true, "substring normal devuelve true");
    ASSERT(strcmp(sub.data, "gato") == 0, "contenido correcto del substring");
    ss_free(&sub);

    SafeString sub_vacio = ss_new();
    ASSERT(ss_substring(&s, 5, 5, &sub_vacio) == true, "substring vacio (inicio==fin) devuelve true");
    ASSERT(sub_vacio.length == 0, "substring vacio tiene length 0");
    ss_free(&sub_vacio);

    SafeString sub_malo = ss_new();
    ASSERT(ss_substring(&s, 10, 3, &sub_malo) == false, "rango invertido devuelve false");
    ASSERT(ss_substring(&s, 0, 999, &sub_malo) == false, "rango fuera de limite devuelve false");
    ASSERT(ss_substring(&s, 0, 5, NULL) == false, "substring con out NULL devuelve false");
    ASSERT(ss_substring(NULL, 0, 5, &sub_malo) == false, "substring con s NULL devuelve false");

    ss_free(&s); ss_free(&sub_malo);
}

void test_ss_trim(void)
{
    printf("test_ss_trim\n");

    SafeString t1 = ss_new();
    ss_from_cstr(&t1, "   hola mundo   ");
    ss_trim(&t1);
    ASSERT(strcmp(t1.data, "hola mundo") == 0, "trim quita espacios de ambos lados");
    ASSERT(t1.length == 10, "length correcta tras trim");

    SafeString t2 = ss_new();
    ss_from_cstr(&t2, "   ");
    ss_trim(&t2);
    ASSERT(t2.length == 0, "trim de puro espacio deja length 0");

    SafeString t3 = ss_new();
    ss_from_cstr(&t3, "sin_espacios");
    ss_trim(&t3);
    ASSERT(strcmp(t3.data, "sin_espacios") == 0, "trim sin espacios no cambia el contenido");

    ss_trim(NULL); // no deberia crashear
    ASSERT(1, "ss_trim(NULL) no crashea");

    SafeString t4 = ss_new();
    ss_trim(&t4); // string vacio (length 0), no deberia crashear
    ASSERT(t4.length == 0, "ss_trim sobre string vacio no crashea y sigue en 0");

    ss_free(&t1); ss_free(&t2); ss_free(&t3); ss_free(&t4);
}

void test_ss_insert(void)
{
    printf("test_ss_insert\n");
    SafeString s = ss_new();
    ss_from_cstr(&s, "mundo");

    ASSERT(ss_insert(&s, 0, "hola ") == true, "insertar al principio devuelve true");
    ASSERT(strcmp(s.data, "hola mundo") == 0, "contenido tras insertar al principio");
    ASSERT(s.length == 10, "longitud correcta");

    ASSERT(ss_insert(&s, 5, "cruel ") == true, "insertar en el medio devuelve true");
    ASSERT(strcmp(s.data, "hola cruel mundo") == 0, "contenido tras insertar en el medio");

    ASSERT(ss_insert(&s, s.length, "!") == true, "insertar al final devuelve true");
    ASSERT(strcmp(s.data, "hola cruel mundo!") == 0, "contenido tras insertar al final");

    ASSERT(ss_insert(&s, 999, "x") == false, "insertar fuera de limites devuelve false");
    ASSERT(ss_insert(NULL, 0, "x") == false, "insertar con s NULL devuelve false");
    ASSERT(ss_insert(&s, 0, NULL) == false, "insertar con texto NULL devuelve false");

    ASSERT(ss_insert(&s, 0, "") == true, "insertar texto vacio devuelve true");
    ASSERT(strcmp(s.data, "hola cruel mundo!") == 0, "contenido no cambia al insertar vacio");

    ss_free(&s);

    SafeString vacia = ss_new();
    ASSERT(ss_insert(&vacia, 0, "texto") == true, "insertar en string vacio devuelve true");
    ASSERT(strcmp(vacia.data, "texto") == 0, "contenido correcto en string vacio");
    ss_free(&vacia);
}

int main(void)
{
    test_ss_new();
    test_ss_append();
    test_ss_free();
    test_ss_from_cstr();
    test_ss_equals();
    test_ss_find();
    test_ss_substring();
    test_ss_trim();
    test_ss_insert();

    printf("\n%d/%d tests pasaron\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
