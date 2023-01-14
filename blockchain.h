#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sha-256.h"

struct block{
    uint32_t sender;
    uint32_t recvr;
    uint32_t amount;
};

struct timestamp{
    uint32_t time;
    uint32_t client;
};

struct blockchain{
    struct block transaction;
    struct blockchain* prev;
    struct timestamp lamp_stamp;//total lamport time stamp
    uint8_t prev_hash[32];//32*8bit = 256 bit for SHA_256
    uint8_t my_hash[32];
};

void append_block(struct blockchain* insert, struct blockchain* head);
void compute_hash(struct blockchain* blk);

