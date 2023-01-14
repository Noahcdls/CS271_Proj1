#include "blockchain.h"
#include "sha-256.h"

int main(){
    struct block testblock;
    testblock.sender = 15;
    testblock.recvr = 20;
    testblock.amount = 200;
    uint8_t hash[32];
    calc_sha_256(hash, &testblock, sizeof(struct block));
    for(int x = 0; x < 32; x++)
        printf("%x", hash[x]);
    printf("\n");
    
    
    return 0;
}