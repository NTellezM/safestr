# safestr-extras - arena y division empaquetada para safestr
#
#   make check          tests con AddressSanitizer y UndefinedBehaviorSanitizer
#   make check-threads  tests de hilos con ThreadSanitizer
#   make check-parcial  uso parcial de la API sin un solo warning
#   make check-std      la misma suite en C99, C11, C17 y C++17, con -Werror
#   make check-all      todo lo anterior
#   make bench          los cuatro benchmarks
#   make ejemplo        programa de demostracion sobre un CSV de bodega
#   make propuesta      aplica el parche en build/ y corre lo que depende de el
#   make clean

CC      ?= cc
CXX     ?= g++
CFLAGS  ?= -O2 -g
WARN     = -Wall -Wextra -Wpedantic
STD      = -std=c17
INC      = -I.
POSIX    = -D_POSIX_C_SOURCE=200809L

SAN      = -fsanitize=address,undefined -fno-omit-frame-pointer
TSAN     = -fsanitize=thread

# ThreadSanitizer choca con el ASLR de alta entropia de los kernels
# recientes (vm.mmap_rnd_bits = 32 en Ubuntu 24.04 y derivados). Falla con
# "FATAL: ThreadSanitizer: unexpected memory mapping" incluso en un programa
# vacio: no es un problema del codigo que se este probando.
#
# setarch -R desactiva la aleatorizacion solo para ese proceso, sin tocar
# la configuracion del sistema. Si no esta disponible, se corre directo.
#
# La alternativa permanente, si prefieres:
#     sudo sysctl -w vm.mmap_rnd_bits=28

FUENTE   = safestr.c
BUILD    = build

.PHONY: all check check-threads check-parcial check-std check-all bench ejemplo propuesta clean

all: check

$(BUILD):
	@mkdir -p $(BUILD)

# ------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------

check: $(BUILD)
	$(CC) $(STD) -O1 -g $(WARN) $(SAN) $(INC) test/test_extras.c $(FUENTE) -o $(BUILD)/test_san
	./$(BUILD)/test_san

check-threads: $(BUILD)
	$(CC) $(STD) -O1 -g $(TSAN) $(INC) test/test_arena_hilos.c $(FUENTE) \
	    -o $(BUILD)/hilos_tsan -lpthread
	@if command -v setarch >/dev/null 2>&1; then \
	    echo "setarch $$(uname -m) -R ./$(BUILD)/hilos_tsan"; \
	    setarch $$(uname -m) -R ./$(BUILD)/hilos_tsan; \
	else \
	    echo "./$(BUILD)/hilos_tsan   (sin setarch: puede fallar por ASLR)"; \
	    ./$(BUILD)/hilos_tsan; \
	fi
	$(CC) $(STD) -O1 -g $(SAN) $(INC) test/test_arena_hilos.c $(FUENTE) \
	    -o $(BUILD)/hilos_asan -lpthread
	./$(BUILD)/hilos_asan

# Una unidad de traduccion que usa solo parte de la API. Con -Werror atrapa
# cualquier funcion `static` sin usar que se cuele en los headers, que en
# MSVC /W4 /WX seria un error en el proyecto del usuario.
check-parcial: $(BUILD)
	$(CC) $(STD) $(WARN) -Werror $(CFLAGS) $(INC) test/uso_parcial.c $(FUENTE) \
	    -o $(BUILD)/uso_parcial
	./$(BUILD)/uso_parcial

check-std: $(BUILD)
	@for std in c99 c11 c17; do \
	    printf "%-14s " "$$std"; \
	    $(CC) -std=$$std $(WARN) -Werror $(CFLAGS) $(INC) \
	        test/test_extras.c $(FUENTE) -o $(BUILD)/t_$$std || exit 1; \
	    ./$(BUILD)/t_$$std || exit 1; \
	done
	@printf "%-14s " "c++17"
	@$(CXX) -std=c++17 -Wall -Wextra $(CFLAGS) -x c++ $(INC) \
	    test/test_extras.c $(FUENTE) -o $(BUILD)/t_cpp
	@./$(BUILD)/t_cpp

check-all: check check-parcial check-std check-threads

# ------------------------------------------------------------------
# Benchmarks
# ------------------------------------------------------------------

bench: $(BUILD)
	@for b in bench_arena bench_split bench_grow bench_grande; do \
	    $(CC) $(STD) -O2 $(POSIX) $(INC) bench/$$b.c $(FUENTE) -o $(BUILD)/$$b || exit 1; \
	done
	@echo "=== bench_arena: arena contra malloc, heap limpio y fragmentado ==="
	@./$(BUILD)/bench_arena
	@echo; echo "=== bench_split: division empaquetada ==="
	@./$(BUILD)/bench_split
	@echo; echo "=== bench_grow: crecimiento en sitio ==="
	@./$(BUILD)/bench_grow
	@echo; echo "=== bench_grande: por encima de SS_MAX_PREALLOC ==="
	@./$(BUILD)/bench_grande

# ------------------------------------------------------------------
# Ejemplo de uso
# ------------------------------------------------------------------

ejemplo: $(BUILD)
	$(CC) $(STD) $(CFLAGS) $(WARN) -Werror $(INC) ejemplo/bodega.c $(FUENTE) \
	    -o $(BUILD)/bodega
	@echo
	@echo "=== 50.000 movimientos de prueba, resumidos por pasillo ==="
	@./$(BUILD)/bodega generar 50000 | ./$(BUILD)/bodega resumir

# ------------------------------------------------------------------
# Propuesta upstream: necesita el parche aplicado
# ------------------------------------------------------------------

$(BUILD)/parcheado: $(BUILD) propuesta/asignador.patch safestr.h safestr.c
	@rm -rf $(BUILD)/parcheado
	@mkdir -p $(BUILD)/parcheado
	@cp safestr.h safestr.c safestr_arena.h safestr_split.h $(BUILD)/parcheado/
	@cp propuesta/safestr_arena2.h propuesta/duelo.c propuesta/alineacion.c \
	    propuesta/test_a2_hilos.c $(BUILD)/parcheado/
	@cd $(BUILD)/parcheado && patch -p1 -s < ../../propuesta/asignador.patch
	@echo "parche aplicado en $(BUILD)/parcheado"

propuesta: $(BUILD)/parcheado
	@cd $(BUILD)/parcheado && \
	  echo "--- alineacion por tamaño ---" && \
	  $(CC) $(STD) -O2 $(WARN) -I. alineacion.c safestr.c -o alineacion && ./alineacion && \
	  echo && echo "--- duelo: con cabecera contra sin cabecera ---" && \
	  $(CC) $(STD) -O2 $(POSIX) -I. duelo.c safestr.c -o duelo && ./duelo && \
	  echo && echo "--- hilos con la arena sin cabecera (TSan) ---" && \
	  $(CC) $(STD) -O1 -g $(TSAN) -I. test_a2_hilos.c safestr.c -o hilos -lpthread && \
	  { command -v setarch >/dev/null 2>&1 && setarch $$(uname -m) -R ./hilos || ./hilos; }

clean:
	rm -rf $(BUILD)
