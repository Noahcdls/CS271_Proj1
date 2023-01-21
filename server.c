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

// static pthread_t read_thread, write_thread;
static pthread_t *read_threads[MAX_CLIENTS];
static pthread_t *write_threads[MAX_CLIENTS];
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
            printf("new client: %u\n", new_client);
            client_ids[i] = new_client; // add pointer to list
            printf("client_id: %u\n", client_ids[i]);
            client_ids[i]->id = next_id; // allocate the next_id
            client_ids[i]->balance = 10; // iniital value in account
            client_ids[i]->loc = i;
            printf("location: %u\n", client_ids[i]->loc);
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
        if (client_ids[i]->id == pid)
        {
            free(client_ids[i]);
            client_ids[i] = NULL; // reset id
            client_count--;       // decrement active users
            free(read_threads[i]);
            free(write_threads[i]);
            read_threads[i] = NULL;
            write_threads[i] = NULL;
            printf("Client %u removed\n", pid);
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
    pthread_mutex_lock(&msg_lock);
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
@brief Pop a message on the msg queue
*/
void msg_pop()
{
    if(msg_queue == NULL){
        return;
    }
    pthread_mutex_lock(&msg_lock);
    struct message_queue *old_head = msg_queue; // take the old head and remove it
    msg_queue = old_head->next_msg;             // set next head
    free(old_head);                             // remove old head from memory
    if (msg_queue == NULL)                      // set tail to NULL if front is NULL
        msg_tail = NULL;
    pthread_mutex_unlock(&msg_lock);
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
    printf("entering server read\n");
    struct args *arg = arguments;
    uint32_t newsockfd = arg->socket;
    uint8_t *buffer = arg->buffer;
    uint32_t pid = arg->id;
    uint32_t loc = arg->loc;
    while (1)
    {

        int msg = read(newsockfd, buffer, msg_size);
        if (msg == 0)
            break;
        switch (*buffer)
        {
        case BALANCE: // send balance back to client. Sent back right away
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (client_ids[i]->id == pid)
                {
                    memcpy(buffer + 1, &(client_ids[i]->balance), sizeof(uint32_t));
                    printf("Balance: %u\n", client_ids[i]->balance);
                    write(newsockfd, buffer, sizeof(uint8_t) + sizeof(uint32_t));
                    break;
                }
            }
            break;
        case REQ:                                 // send req to all clients
            for (int i = 0; i < MAX_CLIENTS; i++) // add req to all clients to msg queue to send out
            {
                if (client_ids[i] != NULL)
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
                if (client_ids[i] != NULL)
                {                                                                        // send messages to valid clients
                    struct message_queue *req_msg = make_msg(buffer, client_ids[i]->id); // make a message with the buffer content
                    msg_push(req_msg);
                }
            }
            break;
        case COMMIT: // commit a transaction to the bank
            struct blockchain *commit_chain = (struct blockchain *)(buffer + 1);
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
            break;
        default: // do nothing
            break;
        }
    }
    client_disconnect[loc] = 1;
    close(newsockfd);
    remove_client(pid);
    pthread_mutex_lock(&msg_lock);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_ids[i] != NULL)
        { // send messages to all active clients saying this client is gone
            struct message_queue *client_msg = malloc(sizeof(struct message_queue));
            client_msg->id = client_ids[i]->id;
            client_msg->msg[0] = CLIENT_REMOVED;
            memcpy(client_msg->msg + 1, &pid, sizeof(uint32_t));
            msg_push(client_msg);
        }
    }
    pthread_mutex_unlock(&msg_lock);
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
        if (msg_queue != NULL && (msg_queue->id == pid))
        { // look to see if a message has my client's id
            printf("Sending a message for client %u\n", pid);
            pthread_mutex_lock(&msg_lock);
            memcpy(buffer, msg_queue->msg, msg_size); // copy over data before send
            write(newsockfd, buffer, msg_size);       // write to correct socket
            pthread_mutex_unlock(&msg_lock);
            msg_pop();
        }
    }
    client_disconnect[loc] = 0;
    return NULL;
}

/*
@brief Initialize server by init mutexes and resetting client list and id values
*/
void server_init()
{ // reset ids for clients and set starting pid value
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
    close(sockfd);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_ids[i] != NULL){
            printf("freed ID\n");
            free(client_ids[i]); }// return client ids to memory}
        printf("freeing buffers\n");
        free(tx_buffs[i]);       // return buffer memory
        free(rx_buffs[i]);

        if (write_threads[i] != NULL)
        {
            printf("freeing write thread\n");
            pthread_cancel(*write_threads[i]);
            free(write_threads[i]);
        }
        if (read_threads[i] != NULL)
        {
            printf("freeing read thread\n");
            pthread_cancel(*read_threads[i]);
            free(read_threads[i]);
        }
    }
    // while (msg_queue != NULL)
    //     msg_pop();
    printf("freeing args\n");
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
    while (1)
    {
        newsockfd = accept(sockfd,
                           (struct sockaddr *)&cli_addr,
                           &clilen); // accept a connection
        if (newsockfd < 0)
            error("ERROR on accept");
        pthread_mutex_lock(&bank_lock);
        client_count++; // increment number of clients available
        printf("Client is connecting\n");
        pthread_mutex_unlock(&bank_lock);
        // child process runs forever sending messages
        // close(sockfd); // close old socket since this forked process doesnt need it
        uint8_t rx_buff[msg_size], tx_buff[msg_size];
        struct client *client_ptr = add_client(); // add the client to the network
        printf("Made client data\n");
        if (client_ptr == NULL)
            continue;
        memcpy(tx_buff, client_ptr, sizeof(struct client)); // send the connected client his PID and initial balance
        printf("Sending client data to client\n");
        n = write(newsockfd, tx_buff, msg_size); // write back its information so it can tell who it is
        printf("Client received data: %u\n", client_ptr->loc);

        read_args->buffer = tx_buffs[client_ptr->loc]; // write arguments for the threads
        write_args->buffer = rx_buffs[client_ptr->loc];

        read_args->id = client_ptr->id;
        write_args->id = client_ptr->id;

        read_args->socket = newsockfd;
        write_args->socket = newsockfd;

        read_args->loc = client_ptr->loc;
        write_args->loc = client_ptr->loc;
        printf("client loc: %u\n", client_ptr->loc);
        pthread_create(read_threads[client_ptr->loc], 0, &server_read, read_args); // run threads and then listen for another client again
        printf("Read thread started\n");
        pthread_create(write_threads[client_ptr->loc], 0, &server_send, write_args);
        printf("Created listen and read threads\n");
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (client_ids[i] != NULL)
            { // broadcast a new client has joined the server
                printf("making a message for client %u\n", client_ids[i]->id);
                struct message_queue *client_msg = malloc(sizeof(struct message_queue));
                client_msg->id = client_ids[i]->id;
                client_msg->msg[0] = CLIENT_ADDED;
                memcpy(client_msg->msg + 1, &(client_ptr->id), sizeof(uint32_t));
                msg_push(client_msg);
            }
        }
        printf("Pushed client added to clients\n");
    }
    close(sockfd);
    return 0;
}