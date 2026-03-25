# Makefile — Proyecto Chat OS 2026
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g
LDFLAGS = -lpthread

all: servidor cliente

servidor: servidor.c protocolo.h
	$(CC) $(CFLAGS) -o servidor servidor.c $(LDFLAGS)

cliente: cliente.c protocolo.h
	$(CC) $(CFLAGS) -o cliente cliente.c $(LDFLAGS)

clean:
	rm -f servidor cliente

.PHONY: all clean
