#include "safestr.h"
#include <stdio.h>

int main(void)
{
    SafeString s = ss_from("   Hola ");

    ss_trim(&s);
    ss_append(&s, ", mundo");
    ss_append_char(&s, '!');
    ss_appendf(&s, " Van %d caracteres... por ahora.", (int) ss_len(&s));

    if (!ss_ok(&s))
    {
        fprintf(stderr, "sin memoria\n");
        return 1;
    }

    printf("%s\n", ss_cstr(&s));
    printf("largo: %zu, capacidad: %zu\n", ss_len(&s), s.capacity);
    printf("contiene 'mundo': %s\n", ss_contains(&s, "mundo") ? "si" : "no");

    SafeString trozo = ss_slice(&s, 0, 4);
    printf("primeras 4 letras: %s\n", ss_cstr(&trozo));

    ss_free(&trozo);
    ss_free(&s);
    return 0;
}
