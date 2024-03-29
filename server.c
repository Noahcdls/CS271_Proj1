#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include "client_server_define.h"
#include "blockchain.h"
#include <arpa/inet.h>
// static pthread_t read_thread, write_thread;
static int port_counter;
static pthread_t *read_threads[MAX_CLIENTS];
static pthread_t *write_threads[MAX_CLIENTS];
static pthread_t server_int;
static uint8_t *tx_buffs[MAX_CLIENTS];
static uint8_t *rx_buffs[MAX_CLIENTS];
int client_disconnect[MAX_CLIENTS];
struct args *read_args, *write_args;
int sockfd = 0;
struct args
{
    int socket;
    uint8_t *buffer;
    uint32_t id;
    uint32_t loc;
};
struct bank_history{
    struct block trans;
    uint8_t status;
    struct bank_history * prev;
};
struct bank_history * newest_trans;

/*
@brief make a msg to be added onto the queue
@param buffer the buffer you want to copy over into your message
@param pid ID of receiving client
*/
void *make_msg(uint8_t *buffer, uint32_t pid)
{
    struct message_queue *new_msg = malloc(sizeof(struct message_queue));
    new_msg->id = pid;
    memcpy(new_msg->msg, buffer, msg_size); // copy message
    return new_msg;
}

/*
@brief Adds a client to the server,
returning the clients PID
@note Need to send back client IDs for self ID
*/
void *add_client()
{
    pthread_mutex_lock(&bank_lock); // can only add someone to the server if no one has the lock to prevent issues
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_ids[i] == NULL)
        {
            struct client *new_client = malloc(sizeof(struct client)); // get a new client in memory
            //printf("NEW CLIENT: %u\n", next_id);
            client_ids[i] = new_client; // add pointer to list
            // printf("client_id: %u\n", client_ids[i]);
            client_ids[i]->id = next_id; // allocate the next_id
            client_ids[i]->balance = 10; // iniital value in account
            client_ids[i]->loc = i;
            //printf("location: %u\n", client_ids[i]->loc);
            if(read_threads[i] != NULL){
                free(read_threads[i]);
            }
            if(write_threads[i] != NULL){
                free(write_threads[i]);
            }
            read_threads[i] = malloc(sizeof(pthread_t)); // add pthread malloc
            write_threads[i] = malloc(sizeof(pthread_t));
            // client_ids[i]->time = 0;     // iniital time in account
            next_id++; // increment the next available id. Goes by first come first serve on priority
            client_count++;
            pthread_mutex_unlock(&bank_lock); // release
            return new_client;
        }
    }
    pthread_mutex_unlock(&bank_lock); // release
    return NULL;
}

/*
@brief removes a client from the network
freeing up the location in the client array
@param pid the ID of the current client to remove
*/
void remove_client(uint32_t pid)
{
    pthread_mutex_lock(&bank_lock); // can only remove someone to the server if no one has the lock to prevent issues
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_ids[i] != NULL && client_ids[i]->id == pid)
        {
            printf("CLEANING CLIENT FOR REUSE\n");
            free(client_ids[i]);
            client_ids[i] = NULL; // reset id
            client_count--;       // decrement active users
            free(read_threads[i]);
            free(write_threads[i]);
            read_threads[i] = NULL;
            write_threads[i] = NULL;
            printf("CLIENT %u REMOVED\n", pid);
            pthread_mutex_unlock(&bank_lock); // release
            return;
        }
    }
    pthread_mutex_unlock(&bank_lock); // release
}

/*
@brief Push message onto msg_queue
@param msg a pointer to the message you would like to push
onto the queue
*/
void msg_push(struct message_queue *msg)
{
    //printf("WAITING FOR LOCK\n");
    pthread_mutex_lock(&msg_lock);
    //printf("GOT LOCK!\n");
    if (msg_queue == NULL)
    {
        msg_queue = msg;
        msg_tail = msg;
        msg->next_msg = NULL;
    }
    else
    {
        msg_tail->next_msg = msg;
        msg->next_msg = NULL;
        msg_tail = msg;
    }
    pthread_mutex_unlock(&msg_lock);
}

/*
@brief Pop a message on the msg
*/
void msg_pop()
{
    //printf("ENTERING POP\n");
    pthread_mutex_lock(&msg_lock);
    if (msg_queue == NULL)
    {
        pthread_mutex_unlock(&msg_lock);
        //printf("EXITING POP\n");
        return;
    }

    struct message_queue *old_head = msg_queue; // take the old head and remove it
    msg_queue = old_head->next_msg;             // set next head
    free(old_head);                             // remove old head from memory
    if (msg_queue == NULL)                      // set tail to NULL if front is NULL
        msg_tail = NULL;
    pthread_mutex_unlock(&msg_lock);
    //printf("EXITING POP\n");
    return;
}

/*
@brief Loop for server to read client msgs and add msgs to the queue
@param newsockfd the socket of the client
@param buffer the read buffer you plan to use
@param pid the ID of the client
@note Must use parameters due to forking and each child
process requires its own buffer, socket, and client ID to be
dynamically made in run time
*/
void *server_read(void *arguments)
{
    // printf("entering server read\n");
    struct args *arg = arguments;
    uint32_t newsockfd = arg->socket;
    uint8_t *buffer = arg->buffer;
    uint32_t pid = arg->id;
    uint32_t loc = arg->loc;
    while (1)
    {

        int msg = read(newsockfd, buffer, msg_size);
        printf("GOT A MESSAGE %u\n", *buffer);
        if (msg == 0)
            break;
        switch (*buffer)
        {
        case BALANCE: // send balance back to client. Sent back right away
            printf("ENTERING BALANCE\n");
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (client_ids[i] != NULL && client_ids[i]->id == pid)
                {
                    printf("SENDING BALANCE\n");
                    memcpy(buffer + 1, &(client_ids[i]->balance), sizeof(uint32_t));
                    printf("CLIENT %u BALANCE: $%u\n", pid, client_ids[i]->balance);
                    write(newsockfd, buffer, sizeof(uint8_t) + sizeof(uint32_t));
                    break;
                }
            }
            break;
        case REQ:                                 // send req to all clients
            for (int i = 0; i < MAX_CLIENTS; i++) // add req to all clients to msg queue to send out
            {
                if (client_ids[i] != NULL && client_ids[i]->id != pid)
                {                                                                        // send messages to valid clients
                    struct message_queue *req_msg = make_msg(buffer, client_ids[i]->id); // make a message with the buffer content
                    msg_push(req_msg);
                }
            }
            break;
        case REPLY: // add reply msg to queue after req
            struct message_queue *reply_msg = make_msg(buffer, *((uint32_t *)(buffer + 1)));
            msg_push(reply_msg);
            break;
        case RELEASE: // block chain has been accessed and can be committed as an action
            // same behavior as req
            for (int i = 0; i < MAX_CLIENTS; i++) // add req to all clients to msg queue to send out
            {
                if (client_ids[i] != NULL && client_ids[i]->id != pid)
                {                                                                        // send messages to valid clients
                    struct message_queue *req_msg = make_msg(buffer, client_ids[i]->id); // make a message with the buffer content
                    msg_push(req_msg);
                }
            }
            break;
        case COMMIT: // commit a transaction to the bank
            pthread_mutex_lock(&bank_lock);
            struct blockchain *commit_chain = (struct blockchain *)(buffer + 1);
            struct bank_history * new_block = malloc(sizeof(struct bank_history));
            memcpy(&(new_block->trans), &(commit_chain->transaction), sizeof(struct block));
            new_block->prev = newest_trans;
            new_block->status = commit_chain->status;
            newest_trans = new_block;
            pthread_mutex_unlock(&bank_lock);
            if(new_block->status == FAILED){
                break;
            }
            else{
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (client_ids[i] != NULL)
                {
                    if (client_ids[i]->id == commit_chain->transaction.sender)
                        client_ids[i]->balance -= commit_chain->transaction.amount;
                    if (client_ids[i]->id == commit_chain->transaction.recvr)
                        client_ids[i]->balance += commit_chain->transaction.amount;
                }
            }
            }
            break;
        default: // do nothing
            break;
        }
    }
    close(newsockfd);
    printf("REMOVE CLIENT %u FROM READ END\n", pid);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_ids[i] != NULL && client_ids[i]->id != pid)
        { // send messages to all active clients saying this client is gone
            struct message_queue *client_msg = malloc(sizeof(struct message_queue));
            client_msg->id = client_ids[i]->id;
            client_msg->msg[0] = CLIENT_REMOVED;
            memcpy((uint32_t *)(client_msg->msg + 1), &pid, sizeof(uint32_t));
            msg_push(client_msg);
        }
    }
    // printf("FINISHED REMOVE CLIENT FROM READ END\n");
    client_disconnect[loc] = 1;
    return NULL;
}

/*
@brief Loop for server to wait for msgs in the queue to be sent
to corresponding recipient
@param newsockfd socket of client
@param buffer tx buffer used
@param pid ID of client associated with forked process of server
*/
void *server_send(void *arguments)
{
    struct args *arg = arguments;
    uint32_t newsockfd = arg->socket;
    uint8_t *buffer = arg->buffer;
    uint32_t pid = arg->id;
    uint32_t loc = arg->loc;
    while (client_disconnect[loc] == 0) // while the read thread is not exiting
    {
        // if(msg_queue != NULL)
        //     printf("MESSAGE ID: %u MY ID: %u\n\n", msg_queue->id, pid);
        // else
        //     printf("EMPTY QUEUE \n");
        pthread_mutex_lock(&msg_lock);
        if (msg_queue != NULL && (msg_queue->id == pid))
        { // look to see if a message has my client's id
            char msg_type [32];
            switch(msg_queue->msg[0]){
                case ABORT:
                    sprintf(msg_type, "ABORT");
                    break;
                case BALANCE:
                    sprintf(msg_type, "BALANCE");
                    break;
                case REQ:
                    sprintf(msg_type, "REQUEST");
                    break;
                case REPLY:
                    sprintf(msg_type, "REPLY");
                    break;
                case COMMIT:
                    sprintf(msg_type, "COMMIT");
                    break;
                case RELEASE:
                    sprintf(msg_type, "RELEASE");
                    break;
                case CLIENT_ADDED:
                    sprintf(msg_type, "CLIENT ADDED");
                    break;
                case CLIENT_REMOVED:
                    sprintf(msg_type, "CLIENT REMOVED");
                    break;
            }
            printf("SENDING %s MESSAGE TO CLIENT %u\n", msg_type, pid);
            memcpy(buffer, msg_queue->msg, msg_size); // copy over data before send
            write(newsockfd, buffer, msg_size);       // write to correct socket
            pthread_mutex_unlock(&msg_lock);
            msg_pop();
            // printf("Pop sucessful %u\n", pid);
        }
        else
        {
            pthread_mutex_unlock(&msg_lock);
        }
    }
    printf("CLIENT %u dropped\n", pid);
    client_disconnect[loc] = 0;
    remove_client(pid);
    printf("EXITING\n");
    return NULL;
}

/*
@brief server user interface to track balances
*/
void *server_interface(void *arguments)
{
    while (1)
    {
        printf("WELCOME TO BANK INTERFACE\nPLEASE PRESS 1 TO GET ALL BALANCES\nPRESS 2 FOR BANK HISTORY\n\n");
        uint8_t interface_buff[32];
        fgets(interface_buff, 32, stdin);
        int value = atoi(interface_buff);
        if (value == 1)
        {
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (client_ids[i] != NULL)
                    printf("CLIENT %u\tBALANCE: $%u\n", client_ids[i]->id, client_ids[i]->balance);
            }
            printf("\n");
        }
        else if(value == 2){
            struct bank_history * hist = newest_trans;
            pthread_mutex_lock(&bank_lock);
            printf("\n");
            while(hist != NULL){
                printf("SENDER:%u RECEIVER:%u AMOUNT:%u TIME:%u STATUS:%s\n",
                hist->trans.sender, hist->trans.recvr, hist->trans.amount, hist->trans.lampstamp.time,
                hist->status == FAILED ? "FAILED" : hist->status == SUCCESS ? "SUCCESS" : "IN PROGRESS");
                hist = hist->prev;
            }
            pthread_mutex_unlock(&bank_lock);
            printf("\n");
        }
    }
}

/*
@brief Initialize server by init mutexes and resetting client list and id values
*/
void server_init()
{ // reset ids for clients and set starting pid value
    newest_trans = NULL;
    pthread_mutex_init(&bank_lock, 0);
    pthread_mutex_init(&msg_lock, 0);
    pthread_mutex_lock(&bank_lock);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        client_ids[i] = NULL;
        tx_buffs[i] = malloc(msg_size); // make malloc'd data for data buffers
        rx_buffs[i] = malloc(msg_size);
        read_threads[i] = NULL;
        write_threads[i] = NULL;
        client_disconnect[i] = 0;
    }
    next_id = 1;
    read_args = malloc(sizeof(struct args));
    write_args = malloc(sizeof(struct args));
    pthread_mutex_unlock(&bank_lock);
}

/*
@brief cancel all threads to close the forked thread
*/
void cleanup()
{
    pthread_cancel(server_int);
    close(sockfd);
    struct bank_history * hist = newest_trans;
    while(hist != NULL){
        struct bank_history* tmp_hist = hist->prev;
        free(hist);
        hist = tmp_hist;
    }
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_ids[i] != NULL)
        {
            //printf("freed ID\n");
            free(client_ids[i]);
        } // return client ids to memory}
        //printf("freeing buffers\n");
        free(tx_buffs[i]); // return buffer memory
        free(rx_buffs[i]);

        if (write_threads[i] != NULL)
        {
            //printf("freeing write thread\n");
            pthread_cancel(*write_threads[i]);
            free(write_threads[i]);
        }
        if (read_threads[i] != NULL)
        {
            //printf("freeing read thread\n");
            pthread_cancel(*read_threads[i]);
            free(read_threads[i]);
        }
    }
    // while (msg_queue != NULL)
    //     msg_pop();
    //printf("freeing args\n");
    free(write_args);
    free(read_args);
    exit(0);
}

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{
    server_init();
    int newsockfd, portno;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int n;
    if (argc < 2)
    {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    bzero((char *)&serv_addr, sizeof(serv_addr));
    portno = atoi(argv[1]);
    port_counter = portno;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *)&serv_addr,
             sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    listen(sockfd, MAX_CLIENTS); // listen up to 64 Clients
    clilen = sizeof(cli_addr);
    int pid;
    signal(SIGINT, cleanup);
    pthread_create(&server_int, 0, &server_interface, NULL);
    while (1)
    {
        newsockfd = accept(sockfd,
                           (struct sockaddr *)&cli_addr,
                           &clilen); // accept a connection
        struct sockaddr_in* pV4Addr = (struct sockaddr_in*)&cli_addr;//Extract client IP
        struct in_addr ipAddr = pV4Addr->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop( AF_INET, &ipAddr, str, INET_ADDRSTRLEN );

        printf("Client IP: %s\n", str);

        if (newsockfd < 0)
            error("ERROR on accept");
        pthread_mutex_lock(&bank_lock);
        client_count++; // increment number of clients available
        printf("ClIENT IS CONNECTING\n");
        pthread_mutex_unlock(&bank_lock);
        // child process runs forever sending messages
        // close(sockfd); // close old socket since this forked process doesnt need it
        uint8_t rx_buff[msg_size], tx_buff[msg_size];
        struct client *client_ptr = add_client(); // add the client to the network
        memcpy(client_ptr->ip_addr, str, sizeof(str));//write in IP ADDR
        printf("MADE CLIENT DATA\n");
        if (client_ptr == NULL)
            continue;
        memcpy(tx_buff, client_ptr, sizeof(struct client)); // send the connected client his PID and initial balance
        //printf("Sending client data to client\n");
        n = write(newsockfd, tx_buff, msg_size); // write back its information so it can tell who it is
        //printf("Client received data: %u\n", client_ptr->loc);
        port_counter++;
        memcpy(tx_buff, &port_counter, sizeof(int));
        n = write(newsockfd, tx_buff, msg_size);
        n = read(newsockfd, tx_buff, msg_size);
        memcpy(&port_counter, tx_buff, sizeof(int));
        printf("UPDATED PORT COUNTER TO %d\n", port_counter);
        client_ptr->port_no = port_counter;//write port

        read_args->buffer = tx_buffs[client_ptr->loc]; // write arguments for the threads
        write_args->buffer = rx_buffs[client_ptr->loc];

        read_args->id = client_ptr->id;
        write_args->id = client_ptr->id;

        read_args->socket = newsockfd;
        write_args->socket = newsockfd;

        read_args->loc = client_ptr->loc;
        write_args->loc = client_ptr->loc;
        //printf("client loc: %u\n", client_ptr->loc);
        pthread_create(read_threads[client_ptr->loc], 0, &server_read, read_args); // run threads and then listen for another client again
        //printf("Read thread started\n");
        pthread_create(write_threads[client_ptr->loc], 0, &server_send, write_args);
        //printf("Created listen and read threads\n");
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (client_ids[i] != NULL && client_ids[i]->id != client_ptr->id)
            {
                // broadcast that a new client has joined the server
                // do not send to th new client so he doesnt duplicate his info
                //printf("BROADCASTING NEW CLIENT TO CLIENT %u\n", client_ids[i]->id);
                struct message_queue *client_msg = malloc(sizeof(struct message_queue));
                client_msg->id = client_ids[i]->id; // send to other client
                client_msg->msg[0] = CLIENT_ADDED;
                memcpy(client_msg->msg + 1, &(client_ptr->id), sizeof(uint32_t)); // add id of new client
                printf("PORT OF ACTIVE %u\n", client_ids[i]->port_no);
                memcpy((int*)(client_msg->msg + 5), &(client_ptr->port_no), sizeof(int));//send the port the new client is connected to
                memcpy(client_msg->msg+9, str, sizeof(str));//send over his IP
                msg_push(client_msg);
                sleep(1);
                // send the new client the information on who else is connected
                //printf("SENDING NEW CLIENT ACTIVE LIST\n");
                struct message_queue *client_data = malloc(sizeof(struct message_queue));
                client_data->id = client_ptr->id; // send to new client
                client_data->msg[0] = CLIENT_CONNECT;
                memcpy((uint32_t *)(client_data->msg + 1), &(client_ids[i]->id), sizeof(uint32_t)); // add id of online client
                memcpy((int*)(client_data->msg + 5), &(client_ids[i]->port_no), sizeof(int));
                memcpy(client_data->msg+9, client_ids[i]->ip_addr, sizeof(client_ids[i]->ip_addr));
                msg_push(client_data);
            }
        }
        printf("CLIENT %u IS FULLY ADDED TO THE NETWORK\n", client_ptr->id);
    }
    close(sockfd);
    return 0;
}