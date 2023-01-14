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

uint8_t rx_buffer[1024];
uint32_t my_balance;
struct client_queue *queue;
struct blockchain *copy_chain;
struct blockchain *req_block;
int clock = 0, sockfd = 0, reply_count = 0;
pthread_t listening_thread, sending_thread;

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

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
void add_client_to_queue(struct client *req_client)//priority queue for adding clients to local queue
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
        if (req_client->time > cmpr_client->curr_client->time)//request occurs after cmpr
        {
            if (cmpr_client->next_client == NULL)//add if we have reached end of queue
            {
                cmpr_client->next_client = nextQueue;
                nextQueue->prev_client = cmpr_client;
                return;
            }
        }
        else if (req_client->time == cmpr_client->curr_client->time)//req and cmpr share time. check id
        {
            if (req_client->id < cmpr_client->curr_client->id)//only add if id is less/higher priority
            {
                nextQueue->prev_client = cmpr_client->prev_client; // update next client ahead of nextQueue
                if(nextQueue->prev_client != NULL)//only change if prev actually exists
                    nextQueue->prev_client->next_client = nextQueue;   // update client ahead to know nextQueue is next
                nextQueue->next_client = cmpr_client;              // next client after nextQueue is the comparison
                cmpr_client->prev_client = nextQueue;              // client ahead of comparison is nextQueue
                if(cmpr_client == queue)//add to head if replaced
                    queue = nextQueue;
                return;
            }
        }
        else if(req_client->time < cmpr_client->curr_client->time)//req occurs before cmpr. add into list
        {
                nextQueue->prev_client = cmpr_client->prev_client; // update next client ahead of nextQueue
                if(nextQueue->prev_client != NULL)//only change if prev actually exists
                    nextQueue->prev_client->next_client = nextQueue;   // update client ahead to know nextQueue is next
                nextQueue->next_client = cmpr_client;              // next client after nextQueue is the comparison
                cmpr_client->prev_client = nextQueue;              // client ahead of comparison is nextQueue
                if(cmpr_client == queue)//add to head if replaced
                    queue = nextQueue;                
                return;            
        }
        else
        {
            cmpr_client = cmpr_client->next_client;//go to next client in queue
        }
    }
}


/*
@brief pops head of the client queue
*/
void pop_queue(){
    if(queue == NULL) return;
    queue->next_client->prev_client = NULL;
    struct client_queue* old_head = queue;
    queue = queue->next_client;
    free(old_head);
    return;
}


/*
@brief thread function to listen for messages and update the client on what to do
@param socket the socket the client is reading from
*/
void listen_message(int socket)
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
        add_client_to_queue((struct client *)rx_buffer + 1)

            case REPLY:
            reply_count++;
        }
    }
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