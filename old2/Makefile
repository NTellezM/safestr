# ---------------------------------------------------------------------------
# safestr - Makefile
#
#   make            compila la libreria estatica (libsafestr.a)
#   make check      compila y ejecuta todos los tests con sanitizers
#   make demo       compila main.c / demo.c si existen
#   make clean      borra todo lo generado
#   make install    instala en $(PREFIX) (por defecto /usr/local)
#   make help       muestra esta ayuda
#
# Variables sobreescribibles desde la linea de comandos, por ejemplo:
#   make CC=clang OPT=-O3
#   make check SAN=            (desactiva los sanitizers)
# ---------------------------------------------------------------------------

CC       ?= cc
AR       ?= ar
RM       ?= rm -f

CSTD     ?= -std=c11
WARN     ?= -Wall -Wextra -Wpedantic
OPT      ?= -O2
CFLAGS   ?= $(CSTD) $(WARN) $(OPT) -g
CPPFLAGS ?= -I.
LDLIBS   ?=

# Sanitizers: solo se usan al compilar los tests y la demo.
SAN      ?= -fsanitize=address,undefined -fno-omit-frame-pointer

PREFIX   ?= /usr/local

# ---------------------------------------------------------------------------

LIB_SRC  := safestr.c
LIB_OBJ  := $(LIB_SRC:.c=.o)
LIB_DEP  := $(LIB_SRC:.c=.d)
LIB      := libsafestr.a
HEADER   := safestr.h

TEST_SRC := $(wildcard test/*.c)
TEST_BIN := $(TEST_SRC:.c=)

DEMO_SRC := $(wildcard main.c demo.c)
DEMO_BIN := $(DEMO_SRC:.c=)

.PHONY: all lib test check demo clean install uninstall help
.DEFAULT_GOAL := all

all: lib

lib: $(LIB)

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

# Objeto de la libreria, con dependencias automaticas de las cabeceras.
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

# Cada test se enlaza directamente contra el fuente de la libreria para que
# la libreria tambien quede instrumentada por los sanitizers.
test/%: test/%.c $(LIB_SRC) $(HEADER)
	$(CC) $(CFLAGS) $(SAN) $(CPPFLAGS) $< $(LIB_SRC) -o $@ $(LDLIBS)

%: %.c $(LIB_SRC) $(HEADER)
	$(CC) $(CFLAGS) $(SAN) $(CPPFLAGS) $< $(LIB_SRC) -o $@ $(LDLIBS)

test: $(TEST_BIN)

demo: $(DEMO_BIN)

check: $(TEST_BIN)
	@if [ -z "$(TEST_BIN)" ]; then echo "No hay tests en test/"; exit 1; fi
	@fallos=0; \
	for t in $(TEST_BIN); do \
		echo "==> $$t"; \
		ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./$$t || fallos=1; \
		echo ""; \
	done; \
	if [ $$fallos -eq 0 ]; then echo "Todos los tests pasaron."; else echo "Hubo tests fallidos."; fi; \
	exit $$fallos

clean:
	$(RM) $(LIB) $(LIB_OBJ) $(LIB_DEP) $(TEST_BIN) $(DEMO_BIN)
	$(RM) -r *.dSYM test/*.dSYM

install: $(LIB)
	mkdir -p $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include
	cp $(LIB) $(DESTDIR)$(PREFIX)/lib/
	cp $(HEADER) $(DESTDIR)$(PREFIX)/include/

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/lib/$(LIB) $(DESTDIR)$(PREFIX)/include/$(HEADER)

help:
	@echo "Objetivos disponibles:"
	@echo "  all / lib   compila $(LIB)"
	@echo "  test        compila los tests de test/"
	@echo "  check       compila y ejecuta los tests con ASan + UBSan"
	@echo "  demo        compila main.c o demo.c si existen"
	@echo "  clean       borra binarios y objetos"
	@echo "  install     copia la libreria y la cabecera a \$$PREFIX ($(PREFIX))"
	@echo ""
	@echo "Variables: CC, CFLAGS, CPPFLAGS, OPT, WARN, SAN, PREFIX"

-include $(LIB_DEP)
