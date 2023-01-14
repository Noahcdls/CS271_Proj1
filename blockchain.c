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
    compute_hash(insert);
    calc_sha_256(insert->prev_hash, &(head->transaction), sizeof(struct block));
    insert->prev = head;
}   

/*
@brief compute individual hash of a block
@param blk the block you want to find the hash of
@note does not return a value. Stores in the my hash field
*/
void compute_hash(struct blockchain* blk){
    calc_sha_256(blk->my_hash, &(blk->transaction), sizeof(struct block));
}