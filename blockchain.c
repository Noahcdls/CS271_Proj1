#include "blockchain.h"


/*
 * @brief Append a block in the block chain to another block
 * @param insert the block you want to insert
 * @param head the head of the block chain you want to append to
 *
 * @note Computes SHA-256 as part of process
 *
 */
void append_block(struct blockchain* insert, struct blockchain* head);
{
    if(insert == NULL || head == NULL){
        return;
    }
    insert->prev = head;
    calc_sha_256(insert->prev_hash, head, sizeof(struct blockchain));
}    
