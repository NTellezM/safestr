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
    ASSERT(ss_ok(&s), "ss_new: sin error");
    ASSERT(strcmp(ss_cstr(&s), "") == 0, "ss_cstr de string recien creado devuelve \"\"");
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

    ss_free(&s);
    ASSERT(s.data == NULL, "doble free: sigue en NULL sin crashear");

    ss_free(NULL);
    ASSERT(1, "ss_free(NULL) no crashea");

    ASSERT(ss_append(&s, "reutilizable") == true, "el struct se puede reutilizar tras free");
    ASSERT(ss_equals_cstr(&s, "reutilizable"), "contenido correcto tras reutilizar");
    ss_free(&s);
}

void test_ss_from_cstr(void)
{
    printf("test_ss_from_cstr\n");
    SafeString s = ss_new();
    ASSERT(ss_from_cstr(&s, "construido") == true, "from_cstr basico devuelve true");
    ASSERT(strcmp(s.data, "construido") == 0, "contenido correcto");
    ASSERT(s.length == 10, "length correcta");
    ss_free(&s);

    SafeString s2 = ss_new();
    ASSERT(ss_from_cstr(&s2, NULL) == false, "from_cstr con cstr NULL devuelve false");

    ASSERT(ss_from_cstr(NULL, "x") == false, "from_cstr con s NULL devuelve false");

    SafeString s3 = ss_new();
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

    ASSERT(ss_equals_cstr(&a, "hola") == true, "equals_cstr con contenido igual");
    ASSERT(ss_equals_cstr(&a, "chau") == false, "equals_cstr con contenido distinto");
    ASSERT(ss_equals_cstr(&vacio1, "") == true, "equals_cstr de string vacio contra \"\"");

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

    ASSERT(ss_index_of(&s, "gato") == 3, "index_of devuelve la posicion");
    ASSERT(ss_index_of(&s, "perro") == SS_NPOS, "index_of devuelve SS_NPOS si no esta");
    ASSERT(ss_contains(&s, "subio") == true, "contains encuentra");
    ASSERT(ss_contains(&s, "vaca") == false, "contains no encuentra");
    ASSERT(ss_starts_with(&s, "el ") == true, "starts_with correcto");
    ASSERT(ss_starts_with(&s, "gato") == false, "starts_with negativo");
    ASSERT(ss_ends_with(&s, "techo") == true, "ends_with correcto");
    ASSERT(ss_ends_with(&s, "gato") == false, "ends_with negativo");
    ASSERT(ss_starts_with(&s, "texto mucho mas largo que el original") == false,
           "starts_with con prefijo mas largo que el string");

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
    ASSERT(sub_vacio.data != NULL, "substring vacio deja un buffer usable, no NULL");
    ASSERT(strcmp(sub_vacio.data, "") == 0, "substring vacio contiene la cadena vacia");
    ss_free(&sub_vacio);

    SafeString sub_malo = ss_new();
    ASSERT(ss_substring(&s, 10, 3, &sub_malo) == false, "rango invertido devuelve false");
    ASSERT(ss_substring(&s, 0, 999, &sub_malo) == false, "rango fuera de limite devuelve false");
    ASSERT(ss_substring(&s, 0, 5, NULL) == false, "substring con out NULL devuelve false");
    ASSERT(ss_substring(NULL, 0, 5, &sub_malo) == false, "substring con s NULL devuelve false");

    /* reutilizar un `out` que ya tenia contenido no debe filtrar memoria */
    SafeString reutilizado = ss_from("basura previa que ocupa heap");
    ASSERT(ss_substring(&s, 3, 7, &reutilizado) == true, "substring sobre un out con contenido");
    ASSERT(ss_equals_cstr(&reutilizado, "gato"), "el out se reemplaza correctamente");
    ss_free(&reutilizado);

    ss_free(&s); ss_free(&sub_malo);
}

void test_ss_slice(void)
{
    printf("test_ss_slice\n");
    SafeString s = ss_from("el gato subio al techo");

    SafeString sub = ss_slice(&s, 3, 7);
    ASSERT(ss_ok(&sub), "slice valido no marca error");
    ASSERT(ss_equals_cstr(&sub, "gato"), "slice devuelve el trozo correcto");
    ss_free(&sub);

    SafeString malo = ss_slice(&s, 100, 200);
    ASSERT(!ss_ok(&malo), "slice fuera de rango marca error");
    ASSERT(strcmp(ss_cstr(&malo), "") == 0, "slice fallido sigue siendo legible");
    ss_free(&malo);

    ss_free(&s);
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

    ss_trim(NULL);
    ASSERT(1, "ss_trim(NULL) no crashea");

    SafeString t4 = ss_new();
    ss_trim(&t4);
    ASSERT(t4.length == 0, "ss_trim sobre string vacio no crashea y sigue en 0");

    SafeString t5 = ss_from("\t\n  con tabs y saltos \r\n");
    ss_trim(&t5);
    ASSERT(ss_equals_cstr(&t5, "con tabs y saltos"), "trim maneja tabs y saltos de linea");

    ss_free(&t1); ss_free(&t2); ss_free(&t3); ss_free(&t4); ss_free(&t5);
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

/* Regresion: pasar el propio buffer como argumento.
   Antes, realloc() dejaba `text` colgando y se leia memoria liberada. */
void test_autoreferencia(void)
{
    printf("test_autoreferencia\n");

    SafeString a = ss_from("abc");
    ASSERT(ss_append(&a, a.data) == true, "append del propio buffer devuelve true");
    ASSERT(ss_equals_cstr(&a, "abcabc"), "append del propio buffer duplica el contenido");
    ss_free(&a);

    SafeString b = ss_from("hola");
    ASSERT(ss_insert(&b, 0, b.data) == true, "insert del propio buffer devuelve true");
    ASSERT(ss_equals_cstr(&b, "holahola"), "insert del propio buffer es correcto");
    ss_free(&b);

    SafeString c = ss_from("prefijo:valor");
    ASSERT(ss_set(&c, c.data + 8) == true, "set con un puntero interno devuelve true");
    ASSERT(ss_equals_cstr(&c, "valor"), "set con puntero interno recorta bien");
    ss_free(&c);
}

void test_api_comoda(void)
{
    printf("test_api_comoda\n");

    SafeString s = ss_from("hola");
    ASSERT(ss_ok(&s), "ss_from devuelve un string valido");
    ASSERT(ss_len(&s) == 4, "ss_len funciona");
    ASSERT(!ss_is_empty(&s), "ss_is_empty false con contenido");

    ss_append_char(&s, '!');
    ASSERT(ss_equals_cstr(&s, "hola!"), "append_char agrega un caracter");

    SafeString otro = ss_from(" mundo");
    ss_append_ss(&s, &otro);
    ASSERT(ss_equals_cstr(&s, "hola! mundo"), "append_ss concatena otro SafeString");

    SafeString copia = ss_clone(&s);
    ASSERT(ss_equals(&s, &copia), "clone produce una copia igual");
    ss_append(&copia, " extra");
    ASSERT(!ss_equals(&s, &copia), "la copia es independiente del original");

    SafeString f = ss_new();
    ASSERT(ss_appendf(&f, "%s tiene %d anos y %.2f m", "Ana", 30, 1.7) == true,
           "appendf devuelve true");
    ASSERT(ss_equals_cstr(&f, "Ana tiene 30 anos y 1.70 m"), "appendf formatea correctamente");
    ss_appendf(&f, " [%c]", 'x');
    ASSERT(ss_ends_with(&f, "[x]"), "appendf acumula sobre lo anterior");

    ss_clear(&f);
    ASSERT(ss_is_empty(&f), "clear vacia el string");
    ASSERT(f.capacity > 0, "clear conserva la capacidad reservada");
    ASSERT(strcmp(ss_cstr(&f), "") == 0, "clear deja la cadena vacia legible");

    ASSERT(ss_reserve(&f, 1000) == true, "reserve devuelve true");
    ASSERT(f.capacity >= 1000, "reserve amplia la capacidad");
    ASSERT(ss_is_empty(&f), "reserve no cambia el contenido");

    SafeString declarado = SS_INIT;
    ASSERT(strcmp(ss_cstr(&declarado), "") == 0, "SS_INIT deja un string usable");
    ASSERT(ss_append(&declarado, "ok") == true, "se puede escribir sobre SS_INIT");
    ss_free(&declarado);

    SafeString roto = ss_from(NULL);
    ASSERT(!ss_ok(&roto), "ss_from(NULL) marca error");
    ASSERT(ss_append(&roto, "x") == false, "un string en error ignora las escrituras");
    ASSERT(strcmp(ss_cstr(&roto), "") == 0, "un string en error sigue siendo legible");
    ss_free(&roto);

    ss_free(&s); ss_free(&otro); ss_free(&copia); ss_free(&f);
}

void test_estres(void)
{
    printf("test_estres\n");

    SafeString s = ss_new();
    for (int i = 0; i < 20000; i++)
        ss_append(&s, "abcde");

    ASSERT(ss_len(&s) == 100000, "length correcta tras 20000 appends");
    ASSERT(ss_ok(&s), "sin errores tras el estres");
    ASSERT(s.capacity >= s.length + 1, "capacity siempre alcanza");
    ASSERT(s.data[s.length] == '\0', "el terminador sigue en su lugar");

    SafeString grande = ss_new();
    for (int i = 0; i < 2000; i++)
        ss_insert(&grande, 0, "xy");
    ASSERT(ss_len(&grande) == 4000, "length correcta tras 2000 inserts al frente");

    ss_free(&s); ss_free(&grande);
}


void test_ss_replace_all(void)
{
    printf("test_ss_replace_all\n");

    SafeString s = ss_from("hola mundo mundo");
    ASSERT(ss_replace_all(&s, "mundo", "C") == true, "replace_all devuelve true");
    ASSERT(ss_equals_cstr(&s, "hola C C"), "reemplaza todas las apariciones");
    ss_free(&s);

    SafeString borrar = ss_from("a-b-c-d");
    ASSERT(ss_replace_all(&borrar, "-", "") == true, "reemplazar por vacio borra");
    ASSERT(ss_equals_cstr(&borrar, "abcd"), "contenido tras borrar");
    ss_free(&borrar);

    /* Si se re-escanea el texto insertado, esto no termina nunca. */
    SafeString crece = ss_from("banana");
    ASSERT(ss_replace_all(&crece, "a", "aa") == true, "reemplazo que contiene al buscado");
    ASSERT(ss_equals_cstr(&crece, "baanaanaa"), "no se re-escanea lo insertado");
    ss_free(&crece);

    SafeString solapado = ss_from("aaa");
    ASSERT(ss_replace_all(&solapado, "aa", "b") == true, "reemplazo solapado");
    ASSERT(ss_equals_cstr(&solapado, "ba"), "continua despues del reemplazo");
    ss_free(&solapado);

    SafeString intacto = ss_from("uno dos");
    ASSERT(ss_replace_all(&intacto, "tres", "X") == true, "no encontrar no es error");
    ASSERT(ss_equals_cstr(&intacto, "uno dos"), "el texto queda igual");
    ASSERT(ss_replace_all(&intacto, "", "x") == false, "buscar la cadena vacia es error");
    ASSERT(ss_replace_all(&intacto, NULL, "x") == false, "viejo NULL");
    ASSERT(ss_replace_all(&intacto, "x", NULL) == false, "nuevo NULL");
    ASSERT(ss_replace_all(NULL, "x", "y") == false, "s NULL");
    ss_free(&intacto);

    SafeString vacio = ss_new();
    ASSERT(ss_replace_all(&vacio, "x", "y") == true, "replace_all en string vacio");
    ASSERT(ss_is_empty(&vacio), "sigue vacio");
    ss_free(&vacio);

    SafeString grande = ss_new();
    for (int i = 0; i < 500; i++)
        ss_append(&grande, "ab");
    ASSERT(ss_replace_all(&grande, "ab", "1234567890") == true, "reemplazo que agranda mucho");
    ASSERT(ss_len(&grande) == 5000, "length correcta tras crecer");
    ASSERT(ss_ok(&grande), "sin errores de memoria");
    ss_free(&grande);
}

void test_ss_read_line(void)
{
    printf("test_ss_read_line\n");

    FILE* f = tmpfile();
    if (f == NULL) { printf("  (no se pudo crear el archivo temporal)\n"); return; }

    fputs("primera\n", f);
    fputs("con \\r\\n de Windows\r\n", f);
    fputs("\n", f);
    for (int i = 0; i < 5000; i++)
        fputs("0123456789", f);
    fputs("\n", f);
    fputs("ultima sin salto", f);
    rewind(f);

    SafeString linea = ss_new();

    ASSERT(ss_read_line(f, &linea) == true, "lee la primera linea");
    ASSERT(ss_equals_cstr(&linea, "primera"), "sin el salto de linea");

    ASSERT(ss_read_line(f, &linea) == true, "lee la segunda");
    ASSERT(ss_equals_cstr(&linea, "con \\r\\n de Windows"), "recorta tambien el \\r");

    ASSERT(ss_read_line(f, &linea) == true, "lee la linea vacia");
    ASSERT(ss_is_empty(&linea), "una linea vacia es vacia, no fin de archivo");

    ASSERT(ss_read_line(f, &linea) == true, "lee la linea larga");
    ASSERT(ss_len(&linea) == 50000, "50000 caracteres en una sola linea");

    ASSERT(ss_read_line(f, &linea) == true, "lee la ultima sin salto");
    ASSERT(ss_equals_cstr(&linea, "ultima sin salto"), "contenido correcto");

    ASSERT(ss_read_line(f, &linea) == false, "al final devuelve false");
    ASSERT(ss_read_line(NULL, &linea) == false, "archivo NULL");
    ASSERT(ss_read_line(f, NULL) == false, "linea NULL");

    ss_free(&linea);
    fclose(f);
}

void test_ss_split(void)
{
    printf("test_ss_split\n");

    SafeString s = ss_from("uno,dos,tres");
    SafeStringList l = ss_split(&s, ",");
    ASSERT(l.error == false, "split sin error");
    ASSERT(l.count == 3, "tres partes");
    ASSERT(strcmp(ss_list_cstr(&l, 0), "uno") == 0, "primera parte");
    ASSERT(strcmp(ss_list_cstr(&l, 1), "dos") == 0, "segunda parte");
    ASSERT(strcmp(ss_list_cstr(&l, 2), "tres") == 0, "tercera parte");
    ASSERT(strcmp(ss_list_cstr(&l, 99), "") == 0, "indice fuera de rango devuelve \"\"");
    ss_list_free(&l);
    ss_free(&s);

    SafeString vacios = ss_from("a,,b,");
    SafeStringList lv = ss_split(&vacios, ",");
    ASSERT(lv.count == 4, "los campos vacios se conservan");
    ASSERT(strcmp(ss_list_cstr(&lv, 1), "") == 0, "campo del medio vacio");
    ASSERT(strcmp(ss_list_cstr(&lv, 3), "") == 0, "campo final vacio");
    ss_list_free(&lv);
    ss_free(&vacios);

    SafeString sin = ss_from("sin separador");
    SafeStringList ls = ss_split(&sin, ";");
    ASSERT(ls.count == 1, "sin separador devuelve una sola parte");
    ASSERT(strcmp(ss_list_cstr(&ls, 0), "sin separador") == 0, "esa parte es el texto entero");
    ss_list_free(&ls);
    ss_free(&sin);

    SafeString nada = ss_new();
    SafeStringList ln = ss_split(&nada, ",");
    ASSERT(ln.count == 1, "un string vacio da una parte");
    ASSERT(strcmp(ss_list_cstr(&ln, 0), "") == 0, "y esa parte es vacia");
    ss_list_free(&ln);
    ss_free(&nada);

    SafeString multi = ss_from("a<->b<->c");
    SafeStringList lm = ss_split(&multi, "<->");
    ASSERT(lm.count == 3, "separador de varios caracteres");
    ASSERT(strcmp(ss_list_cstr(&lm, 1), "b") == 0, "contenido correcto");
    ss_list_free(&lm);
    ss_free(&multi);

    SafeString err = ss_from("hola");
    SafeStringList le = ss_split(&err, "");
    ASSERT(le.error == true, "separador vacio es error");
    ss_list_free(&le);
    SafeStringList ln2 = ss_split(&err, NULL);
    ASSERT(ln2.error == true, "separador NULL es error");
    ss_list_free(&ln2);
    SafeStringList ln3 = ss_split(NULL, ",");
    ASSERT(ln3.error == true, "s NULL es error");
    ss_list_free(&ln3);
    ss_free(&err);

    ss_list_free(NULL);
    ASSERT(1, "ss_list_free(NULL) no crashea");
    ASSERT(strcmp(ss_list_cstr(NULL, 0), "") == 0, "ss_list_cstr(NULL) devuelve \"\"");

    /* muchas partes, para ejercitar la reserva del arreglo */
    SafeString largo = ss_new();
    for (int i = 0; i < 1000; i++)
        ss_append(&largo, "x|");
    SafeStringList ll = ss_split(&largo, "|");
    ASSERT(ll.count == 1001, "1000 separadores dan 1001 partes");
    ASSERT(strcmp(ss_list_cstr(&ll, 1000), "") == 0, "la ultima parte es vacia");
    ss_list_free(&ll);
    ss_free(&largo);
}

void test_ss_join(void)
{
    printf("test_ss_join\n");

    SafeString original = ss_from("uno,dos,tres");
    SafeStringList l = ss_split(&original, ",");
    SafeString unido = ss_new();

    ASSERT(ss_join(&l, ",", &unido) == true, "join devuelve true");
    ASSERT(ss_equals(&unido, &original), "unir con el mismo separador devuelve el original");

    ASSERT(ss_join(&l, " - ", &unido) == true, "join con otro separador");
    ASSERT(ss_equals_cstr(&unido, "uno - dos - tres"), "contenido correcto");

    ASSERT(ss_join(&l, "", &unido) == true, "join con separador vacio");
    ASSERT(ss_equals_cstr(&unido, "unodostres"), "une sin nada en medio");

    ASSERT(ss_join(&l, ",", NULL) == false, "join con out NULL");
    ASSERT(ss_join(NULL, ",", &unido) == false, "join con lista NULL");
    ASSERT(ss_join(&l, NULL, &unido) == false, "join con sep NULL");

    ss_list_free(&l);
    ss_free(&original);

    SafeString vacios = ss_from("a,,b");
    SafeStringList lv = ss_split(&vacios, ",");
    ASSERT(ss_join(&lv, ",", &unido) == true, "join con campos vacios");
    ASSERT(ss_equals_cstr(&unido, "a,,b"), "los campos vacios se conservan");
    ss_list_free(&lv);
    ss_free(&vacios);

    SafeString solo = ss_from("sin separadores");
    SafeStringList ls = ss_split(&solo, "|");
    ASSERT(ss_join(&ls, "|", &unido) == true, "join de una sola parte");
    ASSERT(ss_equals_cstr(&unido, "sin separadores"), "no agrega separadores de mas");
    ss_list_free(&ls);
    ss_free(&solo);

    SafeStringList lista_vacia = { NULL, 0, false };
    ASSERT(ss_join(&lista_vacia, ",", &unido) == true, "join de una lista sin partes");
    ASSERT(ss_is_empty(&unido), "resultado vacio");

    SafeStringList rota = { NULL, 0, true };
    ASSERT(ss_join(&rota, ",", &unido) == false, "join de una lista con error");

    /* ida y vuelta con 500 partes */
    SafeString largo = ss_new();
    for (int i = 0; i < 500; i++)
        ss_appendf(&largo, "%s%d", i == 0 ? "" : ";", i);
    SafeStringList ll = ss_split(&largo, ";");
    ASSERT(ll.count == 500, "500 partes");
    ASSERT(ss_join(&ll, ";", &unido) == true, "join de 500 partes");
    ASSERT(ss_equals(&unido, &largo), "split y join son inversos");
    ss_list_free(&ll);
    ss_free(&largo);

    ss_free(&unido);
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
    test_ss_slice();
    test_ss_trim();
    test_ss_insert();
    test_ss_replace_all();
    test_ss_read_line();
    test_ss_split();
    test_ss_join();
    test_autoreferencia();
    test_api_comoda();
    test_estres();

    printf("\n%d/%d tests pasaron\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
