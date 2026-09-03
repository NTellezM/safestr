#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "safestr.h"
#include "safestr_arena.h"   /* con cabecera de 8B */
#include "safestr_arena2.h"     /* sin cabecera       */
#define CAMPOS 200000
#define REPS 9
static size_t usado_con(SsArena* a){size_t u=0;for(SsArenaTrozo*t=a->actual;t;t=t->sig)u+=t->usado;return u;}
static size_t usado_sin(SsA2* a){size_t u=0;for(SsA2Trozo*t=a->actual;t;t=t->sig)u+=t->usado;return u;}
static double ahora(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec/1e9;}
static int cmpd(const void*a,const void*b){double x=*(const double*)a,y=*(const double*)b;
    return (x>y)-(x<y);}
static uint64_t H=0;
int main(void){
    SafeString csv=ss_new();
    for(int i=0;i<CAMPOS;i++){if(i)ss_append_char(&csv,',');
        ss_appendf(&csv,"PAS%02d-C%02d-N%d",i%40,i%60,i%4);}
    /* mismo heap sucio para todos */
    size_t n=600000; void**hu=malloc(n*sizeof*hu);
    for(size_t i=0;i<n;i++){void*p=malloc(48+(i%7)*16);((char*)p)[0]=1;hu[i]=p;}
    for(size_t i=0;i<n;i+=2){free(hu[i]);hu[i]=NULL;}

    double con[REPS],sin_[REPS],res_con=0,res_sin=0,ped_con=0,ped_sin=0;
    for(int r=0;r<REPS;r++){
        /* --- con cabecera --- */
        SsArena a; ss_arena_init(&a,4u<<20); ss_arena_activar(&a);
        double t0=ahora(); SafeStringList l=ss_split(&csv,","); con[r]=ahora()-t0;
        for(size_t i=0;i<l.count;i++)H^=ss_hash64(&l.items[i]);
        res_con=(double)usado_con(&a); ped_con=(double)a.bytes_pedidos; ss_list_free(&l);
        ss_arena_desactivar(); ss_arena_free(&a);
        /* --- sin cabecera --- */
        SsA2 b; ss_a2_init(&b,4u<<20); ss_a2_activar(&b);
        t0=ahora(); SafeStringList m=ss_split(&csv,","); sin_[r]=ahora()-t0;
        for(size_t i=0;i<m.count;i++)H^=ss_hash64(&m.items[i]);
        res_sin=(double)usado_sin(&b); ped_sin=(double)b.bytes_pedidos; ss_list_free(&m);
        ss_a2_desactivar(); ss_a2_free(&b);
    }
    qsort(con,REPS,sizeof(double),cmpd); qsort(sin_,REPS,sizeof(double),cmpd);
    printf("            %10s %10s %10s\n","mejor","mediana","peor");
    printf("con cab 8B  %9.4fs %9.4fs %9.4fs   reservado %.0f KB\n",
        con[0],con[REPS/2],con[REPS-1],res_con/1024);
    printf("sin cabecera%9.4fs %9.4fs %9.4fs   reservado %.0f KB\n",
        sin_[0],sin_[REPS/2],sin_[REPS-1],res_sin/1024);
    printf("\nmemoria: %.0f%% menos\n",100.0*(res_con-res_sin)/res_con);
    for(size_t i=0;i<n;i++)free(hu[i]); free(hu); ss_free(&csv);
    printf("(centinela %llx = 0 significa que ambas produjeron lo mismo)\n",
        (unsigned long long)H);
    return 0;}
