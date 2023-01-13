#include <stdio.h>
#include <stdlib.h>

struct{
    uint32_t sender;
    uint32_t recvr;
    uint32_t amount;
} block;

struct{
    struct block transaction;
    struct blockchain* prev;
    uint8_t prev_hash[32];//32*8bit = 256 bit for SHA_256
}blockchain;

int hash