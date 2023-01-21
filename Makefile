CC=gcc

all: server client

server: server.o blockchain.o sha-256.o
	$(CC) server.o blockchain.o sha-256.o -o server

client: client.o blockchain.o sha-256.o
	$(CC) client.o blockchain.o sha-256.o -o client

server.o: server.c
	$(CC) -c server.c

client.o: client.c
	$(CC) -c client.c

blockchain.o: blockchain.c blockchain.h
	$(CC) -c blockchain.c

sha-256.o: sha-256.c sha-256.h
	$(CC) -c sha-256.c

hash_test: hash_test.c sha-256.o
	$(CC) -o hash_test hash_test.c sha-256.o



clean:
	rm client server hash_test *.o