#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include "client_server_define.h"
#include "blockchain.h"

static pthread_t read_thread, write_thread;
struct args{
    int socket;
    uint8_t * buffer;
    uint32_t id;

};
/*
@brief Initialize server by init mutexes and resetting client list and id values
*/
void server_init()
{ // reset ids for clients and set starting pid value
    pthread_mutex_init(&bank_lock, 0);
    pthread_mutex_init(&msg_lock, 0);
    pthread_mutex_lock(&bank_lock);
    for (int i = 0; i < MAX_CLIENTS; i++)
        client_ids[i] = NULL;
    next_id = 1;
    pthread_mutex_unlock(&bank_lock);
}

/*
@brief cancel all threads to close the forked thread
*/
void cleanup(){
    pthread_cancel(read_thread);
    pthread_cancel(write_thread);
    for(int i = 0; i < MAX_CLIENTS; i++){
        free(client_ids[i]);//return client ids to memory
    }
}

/*
@brief Adds a client to the server, 
returning the clients PID
@note Need to send back client IDs for self ID
*/
void * add_client()
{
    pthread_mutex_lock(&bank_lock); // can only add someone to the server if no one has the lock to prevent issues
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_ids[i] == NULL)
        {
            struct client * new_client = malloc(sizeof(struct client));//get a new client in memory
            client_ids[i] = new_client;//add pointer to list
            client_ids[i]->id = next_id; // allocate the next_id
            client_ids[i]->balance = 10; // iniital value in account
            // client_ids[i]->time = 0;     // iniital time in account
            next_id++;                  // increment the next available id. Goes by first come first serve on priority
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
            client_ids[i] = NULL;             // reset id
            client_count--;                   // decrement active users
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
void * server_read(void * arguments)
{

    struct args * arg = arguments;
    uint32_t newsockfd = arg->socket;
    uint8_t * buffer = arg->buffer;
    uint32_t pid = arg->id;
    while (1)
    {

        int msg = read(newsockfd, buffer, msg_size);
        switch (*buffer)
        {
        case BALANCE://send balance back to client. Sent back right away
            for(int i = 0; i < MAX_CLIENTS; i++){
                if(client_ids[i]->id == pid){
                    memcpy(buffer + 1, &(client_ids[i]->balance), sizeof(uint32_t));
                    write(newsockfd, buffer, sizeof(uint8_t) + sizeof(uint32_t));
                    break;
                }
            }
            break;
        case REQ://send req to all clients
            for (int i = 0; i < MAX_CLIENTS; i++)//add req to all clients to msg queue to send out
            {
                if (client_ids[i] != NULL)
                {                                                                      // send messages to valid clients
                    struct client_queue *req_msg = make_msg(buffer, client_ids[i]->id); // make a message with the buffer content
                    msg_push(req_msg);
                }
            }
            break;
        case REPLY://add reply msg to queue after req
            struct client_queue * reply_msg = make_msg(buffer, *((uint32_t *)(buffer+1)));
            msg_push(reply_msg);
            break;
        case RELEASE://block chain has been accessed and can be committed as an action
            //same behavior as req
            for (int i = 0; i < MAX_CLIENTS; i++)//add req to all clients to msg queue to send out
            {
                if (client_ids[i] != NULL)
                {                                                                      // send messages to valid clients
                    struct client_queue *req_msg = make_msg(buffer, client_ids[i]->id); // make a message with the buffer content
                    msg_push(req_msg);
                }
            }            
            break;
        case COMMIT://commit a transaction to the bank
            struct blockchain * commit_chain = (struct blockchain *)(buffer+1);
            for(int i = 0; i < MAX_CLIENTS; i++){
                if(client_ids[i]!=NULL){
                    if(client_ids[i]->id == commit_chain->transaction.sender)
                        client_ids[i]->balance -= commit_chain->transaction.amount;
                    if(client_ids[i]->id == commit_chain->transaction.recvr)
                        client_ids[i]->balance += commit_chain->transaction.amount;
                }
            }
            break;
        default://do nothing
            break;
        }
    }
    return NULL;
}

/*
@brief Loop for server to wait for msgs in the queue to be sent
to corresponding recipient
@param newsockfd socket of client
@param buffer tx buffer used
@param pid ID of client associated with forked process of server
*/
void * server_send(void * arguments)
{
    struct args * arg = arguments;
    uint32_t newsockfd = arg->socket;
    uint8_t * buffer = arg->buffer;
    uint32_t pid = arg->id;
    while (1)
    {
        if (msg_queue != NULL && (msg_queue->id == pid))
        { // look to see if a message has my client's id
            pthread_mutex_lock(&msg_lock);
            memcpy(buffer, msg_queue->msg, msg_size);   // copy over data before send
            write(newsockfd, buffer, msg_size);         // write to correct socket
            pthread_mutex_unlock(&msg_lock);
            msg_pop();
        }
    }
    return NULL;
}

/*
@brief make a msg to be added onto the queue
@param buffer the buffer you want to copy over into your message
@param pid ID of receiving client 
*/
void * make_msg(uint8_t *buffer, uint32_t pid)
{
    struct message_queue *new_msg = malloc(sizeof(struct message_queue));
    new_msg->id = pid;
    memcpy(new_msg->msg, buffer, msg_size); // copy message
    return new_msg;
}

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno;
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
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    int pid;
    while (1)
    {
        newsockfd = accept(sockfd,
                           (struct sockaddr *)&cli_addr,
                           &clilen); // accept a connection
        if (newsockfd < 0)
            error("ERROR on accept");
        pthread_mutex_lock(&bank_lock);
        client_count++; // increment number of clients available
        pid = fork();
        if (pid > 0)
        {
            pthread_mutex_unlock(&bank_lock);
        }

        if (pid == 0)
        { // child process runs forever sending messages
            close(sockfd);//close old socket since this forked process doesnt need it
            uint8_t buffer[msg_size];
            uint8_t rx_buff[msg_size], tx_buff[msg_size];
            struct client * client_ptr = add_client();
            memcpy(tx_buff, client_ptr, sizeof(struct client));//send the connected client his PID and initial balance
            n = write(newsockfd, tx_buff, msg_size);
            struct args read_args, write_args;
            
            read_args.buffer = tx_buff;
            write_args.buffer = rx_buff;

            read_args.id = client_ptr->id;
            write_args.id = client_ptr->id;

            read_args.socket = newsockfd;
            write_args.socket = newsockfd;

            pthread_create(&read_thread, 0, &server_read, &read_args);
            pthread_create(&write_thread, 0, &server_send, &write_args);
            signal(SIGINT, cleanup);

            pthread_join(read_thread, 0);
            pthread_join(write_thread, 0);
            remove_client(client_ptr->id);//remove client from the list
            close(newsockfd);
            return 0;//return this forked child
            // while (1)
            // {
            //     bzero(buffer, 256);
            //     n = read(newsockfd, buffer, 255);
            //     if (n < 0)
            //         error("ERROR reading from socket");
            //     else if (n == 0)
            //     {
            //         printf("Client %i has left\n", newsockfd);
            //         close(newsockfd);
            //         return 0;
            //     }
            //     printf("Here is the message: %s\n", buffer);
            //     char messageback[100];
            //     sprintf(messageback, "I got your message, client %i\n", newsockfd);
            //     n = write(newsockfd, messageback, strlen(messageback));
            //     if (n < 0)
            //     {
            //         close(newsockfd);
            //         error("ERROR writing to socket");
            //         return 0;
            //     }
            // }
        }
    }
    close(sockfd);
    return 0;
}