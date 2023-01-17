#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "client_server_define.h"
#include "blockchain.h"
#include <signal.h>

uint8_t rx_buffer[msg_size], tx_buffer[msg_size];
uint32_t my_balance;
struct client_queue *queue;
struct blockchain *block_chain;  // my copy of the blockchain. Acts as the head
struct blockchain *next_block;   // next block to be committed
struct blockchain *rec_finished; // last block that was committed. Can be head if nothing queued to be committed

int clock = 0, sockfd = 0, reply_count = 0;
pthread_t listening_thread, sending_thread;

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

/*
@brief clean up of file upon SIGKILL.
Cancels and joins threads
as well as closes socket
*/
void cleanup()
{
    pthread_cancel(listening_thread);
    pthread_cancel(sending_thread);
    pthread_join(listening_thread, 0);
    pthread_join(sending_thread, 0);
    close(sockfd);
}

/*
@brief A priority queue to add clients who are requesting transactions to occur
in the bank
@param req_client a pointer to the client we want to add.
@note uses total Lamport time to determine ordering of queue
*/
void add_client_to_queue(struct client *req_client) // priority queue for adding clients to local queue
{
    if (req_client == NULL)
        return;
    struct client_queue *nextQueue = malloc(sizeof(struct client_queue));
    nextQueue->curr_client = req_client;
    nextQueue->next_client = NULL;
    nextQueue->prev_client = NULL;

    struct client_queue *cmpr_client = queue;

    if (queue == NULL)
    {
        queue = nextQueue;
        return;
    }

    while (1)
    {
        if (req_client->time > cmpr_client->curr_client->time) // request occurs after cmpr
        {
            if (cmpr_client->next_client == NULL) // add if we have reached end of queue
            {
                cmpr_client->next_client = nextQueue;
                nextQueue->prev_client = cmpr_client;
                return;
            }
        }
        else if (req_client->time == cmpr_client->curr_client->time) // req and cmpr share time. check id
        {
            if (req_client->id < cmpr_client->curr_client->id) // only add if id is less/higher priority
            {
                nextQueue->prev_client = cmpr_client->prev_client;   // update next client ahead of nextQueue
                if (nextQueue->prev_client != NULL)                  // only change if prev actually exists
                    nextQueue->prev_client->next_client = nextQueue; // update client ahead to know nextQueue is next
                nextQueue->next_client = cmpr_client;                // next client after nextQueue is the comparison
                cmpr_client->prev_client = nextQueue;                // client ahead of comparison is nextQueue
                if (cmpr_client == queue)                            // add to head if replaced
                    queue = nextQueue;
                return;
            }
        }
        else if (req_client->time < cmpr_client->curr_client->time) // req occurs before cmpr. add into list
        {
            nextQueue->prev_client = cmpr_client->prev_client;   // update next client ahead of nextQueue
            if (nextQueue->prev_client != NULL)                  // only change if prev actually exists
                nextQueue->prev_client->next_client = nextQueue; // update client ahead to know nextQueue is next
            nextQueue->next_client = cmpr_client;                // next client after nextQueue is the comparison
            cmpr_client->prev_client = nextQueue;                // client ahead of comparison is nextQueue
            if (cmpr_client == queue)                            // add to head if replaced
                queue = nextQueue;
            return;
        }
        else
        {
            cmpr_client = cmpr_client->next_client; // go to next client in queue
        }
    }
}

/*
@brief recompute hash of all items active in queue
@note All committed blocks do not have to be recomputed
*/
void recompute_hash(){
    struct blockchain * cur_blk = block_chain;
    while(cur_blk != rec_finished){
        calc_sha_256(cur_blk->prev_hash, cur_blk->prev, sizeof(struct blockchain));
        cur_blk = cur_blk->prev;
    }
}


/*
@brief add a block to the blockchain
@param req_chain the block requested to be added into the chain
*/
void add_blockchain(struct blockchain *req_chain) // priority queue for adding clients to local queue
{
    if (req_chain == NULL)
        return;

    if (block_chain == NULL)
    {
        block_chain = req_chain;
        if (req_chain->status == IN_PROG)
            next_block = req_chain; // the chain was empty so we add this as the next block given it's incomplete
        else
            rec_finished = req_chain; // then already finished and since empty chain, we make it the most recently finished
        
        return;
    }
    if (next_block == NULL)
    {                                  // If nothing is in the queue, this means we can append to the front
        req_chain->prev = block_chain; // get prev
        if (req_chain->status == IN_PROG)
        {
            next_block = req_chain; // if uncompleted, it gets set up as the next block to process
        }
        else
        {
            calc_sha_256(req_chain->prev_hash, req_chain->prev, sizeof(struct blockchain)); // completed so we can add the hash
            rec_finished = req_chain;
        }
        block_chain = req_chain; // set new head
        recompute_hash();
        return;
    }

    struct blockchain *cur_blk = block_chain;
    struct blockchain *old_cur = NULL;
    while (cur_blk != rec_finished) // until no more waiting blocks
    {

        if (req_chain->transaction.lampstamp.time > cur_blk->transaction.lampstamp.time) // request occurs after cur_blk so append
        {
            req_chain->prev = cur_blk;
            if(old_cur != NULL){
                old_cur->prev = req_chain;//old cur is in queue. will recompute hash before return
            }
            if(cur_blk == block_chain)
                block_chain = req_chain;//new head of the chain
            recompute_hash();
            return;
        }
        else if (req_chain->transaction.lampstamp.time == cur_blk->transaction.lampstamp.time) // req and cur share time. check id
        {
            if (req_chain->transaction.lampstamp.client < cur_blk->transaction.lampstamp.client) // only add if id is less/higher priority
            {
                old_cur = cur_blk;
                cur_blk = cur_blk->prev;//advance to the next block
            }
            else{//same time but lower priority = append
                req_chain->prev = cur_blk;
                if(old_cur != NULL)
                    old_cur->prev = req_chain;
                if(cur_blk == block_chain)
                    req_chain = block_chain;
                recompute_hash();
                return;
            }
        }
        else if (req_chain->transaction.lampstamp.time < cur_blk->transaction.lampstamp.time) // req occurs before cur. Go deeper in queue
        {
            old_cur = cur_blk;
            cur_blk = cur_blk->prev; // go to next client in queue
        }
        else
        {
            old_cur = cur_blk;
            cur_blk = cur_blk->prev; // go to next client in queue
        }
    }
    // cur_blk is now equal to rec_finished
    req_chain->prev = cur_blk; // req chain can now become the next block in the queue
    if(next_block != NULL)
        next_block->prev = req_chain;
    next_block = req_chain;
    if(block_chain == cur_blk)
        block_chain = req_chain; 
    recompute_hash();
    return;
}

/*
@brief Change status of next block to FINISHED or ABORT
*/
void block_finished(uint8_t status){
    next_block->status = status;
    recompute_hash();
    rec_finished = next_block;
    if(rec_finished == block_chain){//nothing left in queue
        next_block = NULL;
        return;
    }
    struct blockchain* new_next = block_chain;
    while(new_next != NULL){
        if(new_next->prev == rec_finished){
            next_block = new_next;
            return;
        }
        new_next = new_next->prev;
    }
}


void *make_blockchain(uint32_t amount, uint32_t my_id, uint32_t recv_id)
{
    struct blockchain *new_block = malloc(sizeof(struct blockchain));
    new_block->status = IN_PROG;
    new_block->transaction.sender = my_id;
    new_block->transaction.recvr = recv_id;
    new_block->transaction.amount = amount;
    new_block->transaction.lampstamp.client = my_id;
    new_block->transaction.lampstamp.time = clock;
    return new_block;
}

void copy_blk(struct block *blk_src, struct blockchain *chain_dest)
{
    memcpy(&(chain_dest->transaction), copy_blk, sizeof(struct block));
}

/*
@brief pops head of the client queue
*/
void pop_queue()
{
    if (queue == NULL)
        return;
    queue->next_client->prev_client = NULL;
    struct client_queue *old_head = queue;
    queue = queue->next_client;
    free(old_head);
    return;
}

/*
@brief thread function to listen for messages and update the client on what to do
@param socket the socket the client is reading from
*/
void client_read(int socket)
{
    int n;
    n = read(socket, rx_buffer, 1024);
    if (n)
    {
        switch (rx_buffer[0])
        {
        case ABORT: // transaction failed
            req_block->status = FAILED;
        case BALANCE: // response from bank to update the local balance so we can pick an amount to request
            memcpy((uint8_t *)&my_balance, *(rx_buffer + 1), sizeof(uint32_t));
        case REQ: // received a request to send
            add_client_to_queue((struct client *)rx_buffer + 1);
        case REPLY:
            reply_count++;
        }
    }
}

void client_send()
{
}

int main(int argc, char *argv[])
{
    int portno, n;                // socket and port number
    struct sockaddr_in serv_addr; // ip addr
    struct hostent *server;       // struct for host info

    char buffer[256];
    if (argc < 3)
    {
        fprintf(stderr, "usage %s hostname port\n", argv[0]); // need ip addr and port
        exit(0);
    }
    portno = atoi(argv[2]);                   // convert port to int
    sockfd = socket(AF_INET, SOCK_STREAM, 0); // get socket file descriptor us TCP standard
    if (sockfd < 0)                           // failure
        error("ERROR opening socket");
    server = gethostbyname(argv[1]); // get host name
    if (server == NULL)
    { // failure
        fprintf(stderr, "ERROR, no such host\n");
        exit(0);
    }
    printf("GOT SERVER NAME\n");
    bzero((char *)&serv_addr, sizeof(serv_addr)); // zero out server address with given size
    serv_addr.sin_family = AF_INET;               // TCP
    bcopy((char *)server->h_addr,
          (char *)&serv_addr.sin_addr.s_addr,
          server->h_length); // copy address
    serv_addr.sin_port = htons(portno);
    // printf("h_addr: %s\n", inet_ntoa(serv_addr.sin_addr));
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR connecting"); // connect or FAILURE

    while (1)
    {

        printf("Please enter the message: "); // get a message and zero out buffer and get from stdin
        bzero(buffer, 256);
        fgets(buffer, 255, stdin);
        n = write(sockfd, buffer, strlen(buffer)); // write to socket and send
        if (n < 0)
            error("ERROR writing to socket");
        bzero(buffer, 256);
        n = read(sockfd, buffer, 255); // wait for read message
        if (n < 0)                     // failure message from read
            error("ERROR reading from socket");
        printf("%s\n", buffer); // print the message
    }

    close(sockfd); // close socket
    return 0;
}