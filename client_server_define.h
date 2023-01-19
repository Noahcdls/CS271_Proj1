#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#define MAX_CLIENTS 64
#define msg_size 4096 //4K bytes is adequate for data, including sending blockchain blocks
#define REP_RCVD 1U
#define REQ_RCVD (1U << 1)
#define BAL_RCVD (1U << 2)
#define LCK_RELEASE (1U << 3)
volatile pthread_mutex_t bank_lock, msg_lock;
volatile uint32_t client_count;
volatile uint32_t next_id;
static struct client* client_ids[MAX_CLIENTS];//up to 64 users
struct message_queue * msg_queue;
struct message_queue * msg_tail;


struct client{
    uint32_t id;
    uint32_t balance;
    uint32_t loc;//location of client in managed list
};

struct client_queue{
    struct client* curr_client; 
    struct client_queue* next_client; //next in queue
    struct client_queue* prev_client; //ahead in queue
};

struct message_queue{
    uint32_t id;
    uint8_t msg[msg_size];
    struct message_queue* next_msg;
};

enum commands{
    ABORT,
    BALANCE,
    REQ,
    REPLY,
    COMMIT,
    RELEASE,
    CLIENT_ADDED,
    CLIENT_REMOVED
};

