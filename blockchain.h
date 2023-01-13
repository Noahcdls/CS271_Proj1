#include <stdio.h>
#include <stdlib.h>

struct{
    int sender;
    int recvr;
    int amount;
} block;

struct{
    struct block transaction;
    struct blockchain* prev;
}blockchain;

int hash