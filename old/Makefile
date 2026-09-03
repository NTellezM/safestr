CC = gcc
CFLAGS = -Wall -Wextra -g -fsanitize=address

SRC = safestr.c
TEST1_SRC = tests/test_safestr.c
TEST2_SRC = tests/test2.c
TARGET1 = tests/test_safestr
TARGET2 = tests/test2

.PHONY: all test clean

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(SRC) $(TEST1_SRC)
	$(CC) $(CFLAGS) -o $@ $^

$(TARGET2): $(SRC) $(TEST2_SRC)
	$(CC) $(CFLAGS) -o $@ $^

test: all
	./$(TARGET1)
	./$(TARGET2)

clean:
	rm -f $(TARGET1) $(TARGET2)