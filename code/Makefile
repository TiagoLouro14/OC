CC = gcc
CFLAGS=-Wall -Wextra
TARGET=L1CACHE

all:
	$(CC) $(CFLAGS) test.c L1CACHE.c L2CACHE.c -o $(TARGET)

clean:
	rm $(TARGET)