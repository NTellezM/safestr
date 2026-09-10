# Este directorio no se compila solo.
#
# bodega.c, plantilla.c y cronograma.c necesitan safestr.c y los headers, que
# viven en la raiz del repositorio. Sin este archivo, `make bodega` aqui adentro
# dispara la regla implicita de GNU make (cc bodega.c -o bodega), sin -I.. y sin
# safestr.c, y falla con "safestr.h: No existe el archivo".
#
# Todo lo de aqui delega en el Makefile de la raiz. Lo normal es correr:
#
#     cd .. && make ejemplo
#
# Para compilarlo a mano, sin make:
#
#     cc -std=c17 -O2 -I.. bodega.c ../safestr.c -o bodega

# Sin reglas implicitas. Cubre los nombres que este archivo no declara:
# `make loquesea` responde "No rule to make target", que es la verdad, en vez
# de intentar `cc loquesea.c -o loquesea` sin -I.. ni safestr.c y fallar con
# cincuenta errores del enlazador que no dicen nada.
MAKEFLAGS += --no-builtin-rules
.SUFFIXES:

RAIZ = ..

.PHONY: all bodega plantilla cronograma ejemplo clean

all ejemplo bodega plantilla cronograma:
	@$(MAKE) --no-print-directory -C $(RAIZ) ejemplo

clean:
	@$(MAKE) --no-print-directory -C $(RAIZ) clean
	@rm -f bodega plantilla cronograma mov.csv demo_config.ini demo_plantilla.txt
