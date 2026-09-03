#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "safestr.h"
#include "safestr_arena2.h"
int main(void){
    /* 1. la regla de alineacion es sana: verificar contra tipos reales */
    printf("n  -> alineacion exigida\n");
    for(size_t n=1;n<=16;n++) printf("%2zu -> %zu\n",n,ss_a2_alineacion_(n));

    /* 2. todo bloque entregado cumple SU alineacion */
    SsA2 a; ss_a2_init(&a,1u<<20); ss_a2_activar(&a);
    int malos=0;
    for(int i=0;i<200000;i++){
        size_t n=1+(size_t)(i%64);
        void*p=ss_a2_realloc(&a,NULL,0,n);
        if(!p||((uintptr_t)p % ss_a2_alineacion_(n))!=0) malos++;
        memset(p,0xAB,n);
    }
    printf("\nbloques mal alineados: %d\n",malos);
    printf("pedidos %zu KB, reservados %zu KB, sobrecosto %.1f%%\n",
        a.bytes_pedidos/1024,a.bytes_reservados/1024,
        100.0*((double)a.bytes_reservados-(double)a.bytes_pedidos)/(double)a.bytes_pedidos);
    ss_a2_desactivar(); ss_a2_free(&a);
    return malos!=0;}
