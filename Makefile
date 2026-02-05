CC = gcc
CFLAGS = -Wall -pthread -lrt

all: server client

server: server.c ipc_shared.c
	$(CC) server.c ipc_shared.c -o server $(CFLAGS)

client: client.c
	$(CC) client.c -o client

clean:
	rm -f server client