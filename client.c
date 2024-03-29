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
#include <arpa/inet.h>
uint8_t client_flags;
uint8_t rx_buffer[msg_size], tx_buffer[msg_size];
static uint8_t *tx_buffs[MAX_CLIENTS];
static uint8_t *rx_buffs[MAX_CLIENTS];
static uint32_t my_balance;
static uint32_t counted_clients;
static uint32_t pid;
struct client_queue *queue;
struct blockchain *block_chain;       // my copy of the blockchain. Acts as the head
struct blockchain *next_block;        // next block to be committed
struct blockchain *rec_finished;      // last block that was committed. Can be head if nothing queued to be committed
uint32_t active_clients[MAX_CLIENTS]; // holds pids of active clients
int writer_socket_no[MAX_CLIENTS];
static pthread_mutex_t blockchain_lock;
static pthread_t *read_threads[MAX_CLIENTS];
static pthread_t *write_threads[MAX_CLIENTS];
int lamp_clock = 0, sockfd = 0, reply_count = 0, newsock = 0;
pthread_t listening_thread, sending_thread;
int my_connect_socket, my_port, client_socket;
socklen_t clilength;
struct sockaddr_in my_addr, clients_addr;
void error(const char *msg)
{
    perror(msg);
    exit(0);
}
struct args{
    int socket;
    uint8_t* buffer;
    uint32_t others_id;
};
struct args client_args;
/*
@brief clean up of file upon SIGKILL.
Cancels threads and cleans up blockchain
*/
void cleanup()
{
    close(sockfd);
    printf("CLEANING UP\n");
    pthread_cancel(listening_thread);
    pthread_cancel(sending_thread);
    for(int i = 0; i < MAX_CLIENTS; i++){
        
        if(read_threads[i] != NULL){
            pthread_cancel(*read_threads[i]);
            free(read_threads[i]);
            
            }
        free(tx_buffs[i]);
        free(rx_buffs[i]);
        if(active_clients[i] != 0){
            close(writer_socket_no[i]);
        }
    }
    struct blockchain *block_ptr = block_chain;
    while (block_ptr != NULL)
    {
        struct blockchain *next_ptr = block_ptr->prev; // get next one
        free(block_ptr);                               // free the malloc'd data
        block_ptr = next_ptr;                          // go to next pointer
    }
    exit(0);
}

/*
@brief recompute hash of all items active in queue
@note All committed blocks do not have to be recomputed
*/
void recompute_hash()
{
    struct blockchain *cur_blk = block_chain;
    // printf("calcing\n");
    while (cur_blk != NULL)
    {
        if (cur_blk != rec_finished)
        {
            if (cur_blk->prev == NULL)
            {
                cur_blk = cur_blk->prev;
                continue;
            }
            if (cur_blk->prev != NULL)
            {
                // printf("PERFORMING SHA\n");
                calc_sha_256(cur_blk->prev_hash, cur_blk->prev, sizeof(struct blockchain));
            }
            // printf("finished one calc\n");
            cur_blk = cur_blk->prev;
        }
        else
            break;
    }
    // printf("finished recompute\n");
    return;
}

/*
@brief add a block to the blockchain
@param req_chain the block requested to be added into the chain
*/
void add_blockchain(struct blockchain *req_chain) // priority queue for adding clients to local queue
{
    printf("ADDING TO CHAIN\n");
    pthread_mutex_lock(&blockchain_lock);
    if (req_chain == NULL)
    {
        pthread_mutex_unlock(&blockchain_lock);
        return;
    }
    // printf("CHAIN BEFORE ADDING\n");

    if (block_chain == NULL)
    {
        block_chain = req_chain;
        block_chain->prev = NULL;
        if (req_chain->status == IN_PROG)
        {
            next_block = req_chain;
        } // the chain was empty so we add this as the next block given it's incomplete
        else
        {
            rec_finished = req_chain;
        } // then already finished and since empty chain, we make it the most recently finished
        pthread_mutex_unlock(&blockchain_lock);
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
        pthread_mutex_unlock(&blockchain_lock);
        return;
    }
    struct blockchain *cur_blk = block_chain;
    struct blockchain *old_cur = NULL;
    while (cur_blk != rec_finished) // until no more waiting blocks
    {

        if (req_chain->transaction.lampstamp.time > cur_blk->transaction.lampstamp.time) // request occurs after cur_blk so append
        {
            req_chain->prev = cur_blk;
            if (old_cur != NULL)
            {
                old_cur->prev = req_chain; // old cur is in queue. will recompute hash before return
            }
            if (cur_blk == block_chain)
                block_chain = req_chain; // new head of the chain
            recompute_hash();
            pthread_mutex_unlock(&blockchain_lock);
            return;
        }
        else if (req_chain->transaction.lampstamp.time == cur_blk->transaction.lampstamp.time) // req and cur share time. check id
        {
            if (req_chain->transaction.lampstamp.client < cur_blk->transaction.lampstamp.client) // only add if id is less/higher priority
            {
                old_cur = cur_blk;
                cur_blk = cur_blk->prev; // advance to the next block
                continue;
            }
            else
            { // same time but lower priority = append
                req_chain->prev = cur_blk;
                if (old_cur != NULL)
                {
                    old_cur->prev = req_chain;
                }
                if (cur_blk == block_chain)
                {
                    block_chain = req_chain;
                }
                recompute_hash();
                pthread_mutex_unlock(&blockchain_lock);
                return;
            }
        }
        else if (req_chain->transaction.lampstamp.time < cur_blk->transaction.lampstamp.time) // req occurs before cur. Go deeper in queue
        {
            old_cur = cur_blk;
            cur_blk = cur_blk->prev; // go to next client in queue
        }
        // else
        // {
        //     old_cur = cur_blk;
        //     cur_blk = cur_blk->prev; // go to next client in queue
        // }
    }
    // cur_blk is now equal to rec_finished
    req_chain->prev = cur_blk; // req chain can now become the next block in the queue
    if (next_block != NULL)
        next_block->prev = req_chain;
    next_block = req_chain;
    if (block_chain == cur_blk) // update head if we are already there
        block_chain = req_chain;
    recompute_hash();
    pthread_mutex_unlock(&blockchain_lock);
    return;
}

/*
@brief Change status of next block to FINISHED or ABORT
*/
void block_finished(uint8_t status)
{
    pthread_mutex_lock(&blockchain_lock);
    if (next_block == NULL)
    {
        pthread_mutex_unlock(&blockchain_lock);
        return;
    }
    next_block->status = status;
    recompute_hash();
    rec_finished = next_block;
    if (rec_finished == block_chain)
    { // nothing left in queue
        next_block = NULL;
        pthread_mutex_unlock(&blockchain_lock);
        return;
    }
    struct blockchain *new_next = block_chain;
    while (new_next != NULL)
    {
        if (new_next->prev == rec_finished)
        {
            next_block = new_next;
            pthread_mutex_unlock(&blockchain_lock);
            return;
        }
        new_next = new_next->prev;
    }
    pthread_mutex_unlock(&blockchain_lock);
    return;
}

void *make_blockchain(uint32_t amount, uint32_t my_id, uint32_t recv_id)
{
    struct blockchain *new_block = malloc(sizeof(struct blockchain));
    new_block->status = IN_PROG;
    new_block->transaction.sender = my_id;
    new_block->transaction.recvr = recv_id;
    new_block->transaction.amount = amount;
    new_block->transaction.lampstamp.client = my_id;
    new_block->transaction.lampstamp.time = lamp_clock;
    printf("TIME OF NEW BLOCK: %u\n", lamp_clock);
    lamp_clock++;
    printf("CLOCK UPDATED TO: %u\n", lamp_clock);
    return new_block;
}

void *new_blockchain()
{
    struct blockchain *new_block = malloc(sizeof(struct blockchain));
    return new_block;
}

void copy_blk(struct block *blk_src, struct blockchain *chain_dest)
{
    memcpy(&(chain_dest->transaction), copy_blk, sizeof(struct block));
}

void copy_blockchain(struct blockchain *src_chain, struct blockchain *dest_chain)
{
    memcpy(dest_chain, src_chain, sizeof(struct blockchain));
}


/*
@brief check if the client given is connected to the server
@param client_id the ID of the client you are checking
*/
uint32_t check_valid_client(uint32_t client_id)
{
    if (client_id == 0)
        return 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (client_id == active_clients[i])
            return 1;
    return 0;
}
int connect_client(char* client_ip, int* port){
    printf("THE PORT NO I RECEIVED IS %u\n", *port);
    struct sockaddr_in serv_addr; // ip addr
    struct hostent *server;       // struct for host info
    char buffer[256];
    int tmp_sock = socket(AF_INET, SOCK_STREAM, 0); // get socket file descriptor us TCP standard
    if (tmp_sock < 0)                           // failure
        error("ERROR opening socket");
    server = gethostbyname(client_ip); // get host name
    if (server == NULL)
    { // failure
        fprintf(stderr, "ERROR, no such host\n");
        return -1;
    }
    // printf("GOT SERVER NAME\n");
    bzero((char *)&serv_addr, sizeof(serv_addr)); // zero out server address with given size
    serv_addr.sin_family = AF_INET;               // TCP
    bcopy((char *)server->h_addr,
          (char *)&serv_addr.sin_addr.s_addr,
          server->h_length); // copy address
    serv_addr.sin_port = htons(*port);
    // printf("h_addr: %s\n", inet_ntoa(serv_addr.sin_addr));
    if (connect(tmp_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR connecting"); // connect or FAILURE    
    printf("FINISHED CONNECTION WITH CLIENT!\n");
    return tmp_sock;
}

/*
@brief Same as client read but for connection between clients. Does not perform clean up on exit
*/
void *client_p2p_read(void *socket_fd)
{
    struct args * arguments = socket_fd;
    int socket = arguments->socket;
    int conn_id = arguments->others_id;
    uint8_t * my_buffer = arguments->buffer;
    int n;
    while (1)
    {
        n = read(socket, my_buffer, msg_size);
        if (n == 0)
            break;
        switch (my_buffer[0])
        {
        case ABORT:                      // transaction failed
            next_block->status = FAILED; // we only receive this after we get lock so next block is our block
            client_flags |= BAL_RCVD;    // flag sender that we have received FAILED from the bank.
            printf("TRANSACTION ABORTED\n");
            break;
        case BALANCE: // response from bank to update the local balance so we can pick an amount to request
            pthread_mutex_lock(&blockchain_lock);
            int *works = memcpy(&my_balance, ((uint32_t *)(my_buffer + 1)), sizeof(uint32_t)); // copy over balance
            printf("BALANCE RECEIVED: $%u\n", my_balance);
            client_flags |= BAL_RCVD; // flag sender that we have received bank balance and updated our local knowledge of the value
            pthread_mutex_unlock(&blockchain_lock);
            break;
        case REQ:                                            // received a request to send
            printf("USING P2P CONNECTION\n");
            struct blockchain *req_chain = new_blockchain(); // make a new blockchain member
            struct blockchain *rcv_chain = (struct blockchain *)(my_buffer + 1);
            // printf("copying chain\n");
            copy_blockchain(rcv_chain, req_chain); // copy the blockchain member that was sent over.
            // printf("adding copy %u\n", req_chain->status);
            printf("REQUEST RECEIVED FROM CLIENT %u\n", rcv_chain->transaction.sender);
            add_blockchain(req_chain); // add it to our list. This will overwrite the prev ptr so it will be completely local after this
            my_buffer[0] = REPLY;
            memcpy((uint32_t *)(my_buffer + 1), &(req_chain->transaction.sender), sizeof(uint32_t));
            memcpy((uint32_t *)(my_buffer + 5), &(pid), sizeof(uint32_t));
            write(socket, my_buffer, msg_size); // send off the reply right away. No need to use write thread
            pthread_mutex_lock(&bank_lock);
            lamp_clock = (lamp_clock > req_chain->transaction.lampstamp.time) ? (lamp_clock + 1) : (req_chain->transaction.lampstamp.time + 1);
            printf("CLOCK UPDATED TO: %u\n", lamp_clock);
            pthread_mutex_unlock(&bank_lock);
            break;
        case REPLY:
            reply_count++; // increase our reply count
            printf("RECEIVED REPLY FROM CLIENT %u\n", *((uint32_t *)(my_buffer + 5)));
            if (reply_count == counted_clients)
            {                             // if we recieved the amount of messages we anticipated, flag
                client_flags |= REP_RCVD; // flag sender that we have received all replies
                reply_count = 0;
            }
            break;
        case RELEASE:
            printf("THE LOCK HAS BEEN RELEASED\n");
            block_finished(my_buffer[1]);
            client_flags |= LCK_RELEASE; // flag sender to try again for the lock
            break;
        case CLIENT_ADDED:
            printf("A NEW CLIENT HAS JOINED THE NETWORK\n");
            if (client_count < MAX_CLIENTS)
            {
                client_count++;
                for (int i = 0; i < MAX_CLIENTS; i++)
                {
                    if (active_clients[i] == 0)
                    {
                        active_clients[i] = *((uint32_t *)(my_buffer + 1));
                        break;
                    }
                }
            }
            break;
        case CLIENT_REMOVED:
            client_count--;
            uint32_t removed_client = *((uint32_t *)(my_buffer + 1));
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (active_clients[i] == removed_client)
                {
                    active_clients[i] = 0;
                    break;
                }
            }
        }
    }
    printf("CLIENT DISCONNECTING\n");
    //close(socket);
    return NULL;
}
/*
@brief thread function to listen for messages and update the client on what to do
@param socket the socket the client is reading from
*/
void *client_read(void *socket_fd)
{
    int socket = *((int *)socket_fd);
    int n;
    while (1)
    {
        n = read(socket, rx_buffer, msg_size);
        if (n == 0)
            break;
        switch (rx_buffer[0])
        {
        case ABORT:                      // transaction failed
            next_block->status = FAILED; // we only receive this after we get lock so next block is our block
            client_flags |= BAL_RCVD;    // flag sender that we have received FAILED from the bank.
            printf("TRANSACTION ABORTED\n");
            break;
        case BALANCE: // response from bank to update the local balance so we can pick an amount to request
            pthread_mutex_lock(&blockchain_lock);
            int *works = memcpy(&my_balance, ((uint32_t *)(rx_buffer + 1)), sizeof(uint32_t)); // copy over balance
            printf("BALANCE RECEIVED: $%u\n", my_balance);
            client_flags |= BAL_RCVD; // flag sender that we have received bank balance and updated our local knowledge of the value
            pthread_mutex_unlock(&blockchain_lock);
            break;
        case REQ:                                            // received a request to send
            struct blockchain *req_chain = new_blockchain(); // make a new blockchain member
            struct blockchain *rcv_chain = (struct blockchain *)(rx_buffer + 1);
            // printf("copying chain\n");
            copy_blockchain(rcv_chain, req_chain); // copy the blockchain member that was sent over.
            // printf("adding copy %u\n", req_chain->status);
            printf("REQUEST RECEIVED FROM CLIENT %u\n", rcv_chain->transaction.sender);
            add_blockchain(req_chain); // add it to our list. This will overwrite the prev ptr so it will be completely local after this
            rx_buffer[0] = REPLY;
            memcpy((uint32_t *)(rx_buffer + 1), &(req_chain->transaction.sender), sizeof(uint32_t));
            memcpy((uint32_t *)(rx_buffer + 5), &(pid), sizeof(uint32_t));
            write(socket, rx_buffer, msg_size); // send off the reply right away. No need to use write thread
            lamp_clock = (lamp_clock > req_chain->transaction.lampstamp.time) ? (lamp_clock + 1) : (req_chain->transaction.lampstamp.time + 1);
            printf("CLOCK UPDATED TO: %u\n", lamp_clock);
            break;
        case REPLY:
            reply_count++; // increase our reply count
            printf("RECEIVED REPLY FROM CLIENT %u\n", *((uint32_t *)(rx_buffer + 5)));
            if (reply_count == counted_clients)
            {                             // if we recieved the amount of messages we anticipated, flag
                client_flags |= REP_RCVD; // flag sender that we have received all replies
                reply_count = 0;
            }
            break;
        case RELEASE:
            printf("THE LOCK HAS BEEN RELEASED\n");
            block_finished(rx_buffer[1]);
            client_flags |= LCK_RELEASE; // flag sender to try again for the lock
            break;
        case CLIENT_ADDED:
            if (client_count < MAX_CLIENTS)
            {
                client_count++;
                for (int i = 0; i < MAX_CLIENTS; i++)
                {   
                    if (active_clients[i] == 0)
                    {
                        active_clients[i] = *((uint32_t *)(rx_buffer + 1));
                        read_threads[i] = malloc(sizeof(pthread_t));
                        writer_socket_no[i] = accept(my_connect_socket,
                                         (struct sockaddr *)&clients_addr,
                                         &clilength);//wait on new client to connect
                        if (writer_socket_no[i] < 0)
                            error("ERROR on accept");
                        printf("A CLIENT %u HAS CONNECTED TO MY SOCKET!\n", active_clients[i]);
                        pthread_mutex_lock(&bank_lock);
                        client_args.buffer = rx_buffs[i];
                        client_args.others_id = active_clients[i];
                        client_args.socket = writer_socket_no[i];
                        pthread_create(read_threads[i], 0, &client_p2p_read, &client_args);
                        pthread_mutex_unlock(&bank_lock);
                        break;
                    }
                }
            }
            break;
        case CLIENT_CONNECT:
            printf("CONNECTING TO OTHER CLIENT\n");
            if (client_count < MAX_CLIENTS)
            {
                client_count++;
                for (int i = 0; i < MAX_CLIENTS; i++)
                {
                    if (active_clients[i] == 0)
                    {
                        active_clients[i] = *((uint32_t *)(rx_buffer + 1));
                        read_threads[i] = malloc(sizeof(pthread_t));
                        printf("IP:%s PORT:%u\n", rx_buffer+9, *(int*)(rx_buffer+5));
                        writer_socket_no[i] = connect_client((char*)(rx_buffer+9), (int*)(rx_buffer+5));
                        if (writer_socket_no[i] < 0)
                            error("ERROR on connect");
                        printf("I HAVE CONNECTED TO CLIENT %u!\n", active_clients[i]);
                        pthread_mutex_lock(&bank_lock);
                        client_args.buffer = rx_buffs[i];
                        client_args.others_id = active_clients[i];
                        client_args.socket = writer_socket_no[i];
                        pthread_create(read_threads[i], 0, &client_p2p_read, &client_args);
                        pthread_mutex_unlock(&bank_lock);
                        break;
                    }
                }
            }
            break;
        case CLIENT_REMOVED:
            printf("REMOVING CLIENT DATA\n");
            client_count--;
            uint32_t removed_client = *((uint32_t *)(rx_buffer + 1));
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (active_clients[i] == removed_client)
                {
                    active_clients[i] = 0;
                    close(writer_socket_no[i]);
                    writer_socket_no[i] = 0;
                    if(read_threads[i] != NULL){
                    pthread_cancel(*read_threads[i]);
                    free(read_threads[i]);}
                    read_threads[i] = NULL;
                    printf("FINISHED REMOVING DATA\n");
                    break;
                }
            }
        }
    }
    printf("LEAVING SERVER\n");
    pthread_cancel(sending_thread);
    struct blockchain *block_ptr = block_chain;
    while (block_ptr != NULL)
    {
        struct blockchain *next_ptr = block_ptr->prev; // get next one
        free(block_ptr);                               // free the malloc'd data
        block_ptr = next_ptr;                          // go to next pointer
    }
    close(sockfd);
    exit(0);
    return NULL;
}
/*
@brief The thread function for the client to send data
@param socket the socket of the server we are operating on
*/
void *client_send(void *socket_fd)
{
    uint32_t socket = *((int *)socket_fd);
    printf("INITIAL BALANCE: $10\n");
    while (1)
    {
        printf("WELCOME CLIENT %u\n", pid); // AVAILABLE COMMANDS:\n1. BALANCE\n2. SEND\n3. PRINT BLOCKCHAIN", pid);
        printf("AVAILABLE COMMANDS:\n1. BALANCE\n2. SEND\n3. PRINT BLOCKCHAIN\n4. PRINT OTHER ACTIVE CLIENTS\n\n");
        fgets(tx_buffer, msg_size, stdin);
        printf("\n");
        uint8_t option = (uint8_t)atoi(tx_buffer);
        switch (option)
        {
        case 1:
            printf("CLOCK TIME: %u\n", lamp_clock);
            lamp_clock++;
            printf("CLOCK UPDATED TO: %u\n", lamp_clock);
            tx_buffer[0] = BALANCE;
            client_flags &= ~BAL_RCVD;
            write(socket, tx_buffer, msg_size);
            while ((client_flags & BAL_RCVD) != BAL_RCVD)
                ; // wait for flag saying the balance has been received
            pthread_mutex_lock(&bank_lock);
            printf("MY BALANCE IS $%u\n\n", my_balance);
            client_flags &= ~BAL_RCVD; // clear balance receive flag
            pthread_mutex_unlock(&bank_lock);
            break;
        case 2:
            printf("WHO DO YOU WANT TO SEND TO?\n"); // start SEND process
            fgets(tx_buffer, msg_size, stdin);
            if (!check_valid_client(atoi(tx_buffer)))
            {
                printf("NOT A VALID CLIENT\n");
                break;
            }
            uint32_t rec_client = atoi(tx_buffer);
            printf("\nHOW MUCH WOULD YOU LIKE TO SEND?\n");
            fgets(tx_buffer, msg_size, stdin);
            uint32_t trans_amount = atoi(tx_buffer);
            // printf("\nmaking new transaction to blockchain\n");
            struct blockchain *new_trans = make_blockchain(trans_amount, pid, rec_client);
            // printf("adding to block chain\n");
            add_blockchain(new_trans);
            tx_buffer[0] = REQ; // send out a request
            memcpy((struct blockchain *)(tx_buffer + 1), new_trans, sizeof(struct blockchain));
            counted_clients = client_count; // the scenario we will build is not quickly adding or removing clients so no need to lock
            reply_count = 0;
            client_flags &= ~REP_RCVD;          // clear reply flags
            sleep(2);                           // delay for other clients to send
            for(int i = 0; i < MAX_CLIENTS; i++){
                if(active_clients[i] != 0){
                    write(writer_socket_no[i], tx_buffer, msg_size); // send out req to all clients
                }
            }
            printf("WAITING FOR REPLIES FROM %u CLIENTS\n", counted_clients);
            while (!(client_flags)&REP_RCVD)
                ;     // wait until all replies are received
            sleep(3); // wait a few seconds after recieve to simulate delay
            printf("ALL REPLIES RECEIVED\n");
            while (next_block->transaction.lampstamp.client != pid)
                ;
            // pthread_mutex(&bank_lock);
            tx_buffer[0] = BALANCE;
            client_flags &= ~BAL_RCVD;
            write(socket, tx_buffer, msg_size);//send out bank info request
            while (!(client_flags & BAL_RCVD))
                ;
            client_flags &= ~BAL_RCVD;
            printf("MY BALANCE BEFORE TRANSACTION IS $%u\n", my_balance);
            if (my_balance < trans_amount)
            {
                tx_buffer[0] = COMMIT;
                next_block->status = FAILED;
                memcpy(tx_buffer + 1, next_block, sizeof(struct blockchain));
                write(socket, tx_buffer, msg_size);
                tx_buffer[0] = RELEASE;
                tx_buffer[1] = FAILED;
                block_finished(FAILED);
                for(int i = 0; i < MAX_CLIENTS; i++){
                    if(active_clients[i] != 0){
                      write(writer_socket_no[i], tx_buffer, msg_size); // send out req to all clients
                    }
                }
                printf("TRANSACTION FAILED\n");
            }
            else
            {
                tx_buffer[0] = COMMIT;
                next_block->status = SUCCESS;
                memcpy(tx_buffer + 1, next_block, sizeof(struct blockchain));
                write(socket, tx_buffer, msg_size);
                block_finished(SUCCESS);
                tx_buffer[0] = RELEASE;
                tx_buffer[1] = SUCCESS;
                for(int i = 0; i < MAX_CLIENTS; i++){
                    if(active_clients[i] != 0){
                      write(writer_socket_no[i], tx_buffer, msg_size); // send out release to all clients
                    }
                }
                my_balance -= trans_amount;
                printf("TRANSACTION SUCCESSFUL\n");
            }
            printf("MY BALANCE AFTER TRANSACTION IS $%u\n\n", my_balance);
            break;
        case 3:
            printf("CLOCK TIME: %u\n", lamp_clock);
            lamp_clock++;
            printf("CLOCK UPDATED TO: %u\n\n", lamp_clock);
            struct blockchain *cur_blk = block_chain;
            while (cur_blk != NULL)
            {
                printf("SENDER:%u RECEIVER:%u AMOUNT:%u TIME:%u STATUS:%s\n",
                       cur_blk->transaction.sender,
                       cur_blk->transaction.recvr,
                       cur_blk->transaction.amount,
                       cur_blk->transaction.lampstamp.time,
                       cur_blk->status == IN_PROG   ? "IN PROG"
                       : cur_blk->status == SUCCESS ? "SUCCESS"
                                                    : "ABORT");
                printf("HASH: ");
                for (int i = 0; i < 32; i++)
                {
                    printf("%02x", cur_blk->prev_hash[i]);
                }
                printf("\n\n");
                cur_blk = cur_blk->prev;
            }
            printf("\n\n");
            break;
        case 4:
            printf("CLOCK TIME: %u\n", lamp_clock);
            lamp_clock++;
            printf("CLOCK UPDATED TO: %u\n", lamp_clock);
            printf("ACTIVE CLIENTS:\n");
            for (int i = 0; i < MAX_CLIENTS; i++)
                if (active_clients[i] != 0)
                    printf("CLIENT %u\n", active_clients[i]);
            printf("\n\n");
            break;
        default:
            printf("NOT A VALID OPTION\n");
            break;
        }
    }
    return NULL;
}


int main(int argc, char *argv[])
{
    pthread_mutex_init(&blockchain_lock, 0);
    int portno, n;                // socket and port number
    struct sockaddr_in serv_addr; // ip addr
    struct hostent *server;       // struct for host info
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        active_clients[i] = 0;
        writer_socket_no[i] = 0;
        tx_buffs[i] = malloc(msg_size);
        rx_buffs[i] = malloc(msg_size);
        read_threads[i] = NULL;
    }
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
    do
    {
        n = read(sockfd, rx_buffer, msg_size); // get my info as a client
    } while (n == 0);
    pid = ((struct client *)rx_buffer)->id;
    my_balance = ((struct client *)rx_buffer)->balance;
    printf("FINISHED GETTING MY INFO\n");
    ////////////////////////////////////// Connected and got pid and balance
    do
    {
        n = read(sockfd, rx_buffer, msg_size); // read a port no
    } while (n == 0);
    printf("GOT A PORT NUMBER\n");
    memcpy(&my_port, (int *)(rx_buffer), sizeof(int)); // receive port no

    my_connect_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (my_connect_socket < 0)
        error("ERROR opening socket");
    bzero((char *)&my_addr, sizeof(my_addr));
    portno = my_port;
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(my_port);
    if (bind(my_connect_socket, (struct sockaddr *)&my_addr,
             sizeof(my_addr)) < 0)
        my_addr.sin_port = htons(++my_port);
    memcpy((int *)tx_buffer, &my_port, sizeof(int));
    write(sockfd, tx_buffer, msg_size); // write back working port no
    listen(my_connect_socket, MAX_CLIENTS);
    clilength = sizeof(clients_addr);
    printf("MADE SERVER AND LISTENING ON PORT %d\n", my_port);

    signal(SIGINT, cleanup);
    pthread_create(&listening_thread, 0, &client_read, &sockfd);
    pthread_create(&sending_thread, 0, &client_send, &sockfd);
    while (1);


    close(sockfd); // close socket
    return 0;
}