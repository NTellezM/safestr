/* Una unidad de traduccion que usa SOLO una parte de cada header:
   es el caso normal de un usuario, y el que destapa las funciones
   estaticas no usadas. */
#include <stdio.h>
#include "safestr.h"
#include "safestr_arena.h"
#include "safestr_split.h"
int main(void){
    SafeString s = ss_from("a,b,c");
    SafeSplit p = ss_split_packed(&s, ",");
    printf("%zu campos, el segundo es %s\n", p.count, ss_packed_cstr(&p, 1));
    ss_split_packed_free(&p);
    ss_free(&s);
    return 0;
}
