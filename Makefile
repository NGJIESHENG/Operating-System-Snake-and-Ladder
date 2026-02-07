CC = gcc
CFLAGS = -Wall -pthread -lrt

all: server client

server: server.c
	$(CC) server.c -o server $(CFLAGS)

client: client.c
	$(CC) client.c -o client $(CFLAGS)

clean:
	rm -f server client