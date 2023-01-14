CC=gcc

all: server client

server: server.c
	$(CC) -o server server.c

client: client.c
	$(CC) -o client client.c

sha256: sha-256.c
	$(CC) -c -o sha-256 sha-256.c

hash_test: hash_test.c sha-256.o
	$(CC) -o hash_test hash_test.c sha-256.o



clean:
	rm client server