#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#define MAX_CLIENTS 64
#define msg_size 128
volatile pthread_mutex_t bank_lock, msg_lock;
volatile int client_count;
volatile int next_id;
static struct client client_ids[MAX_CLIENTS];//up to 64 users
struct message_queue * msg_queue;
struct message_queue * msg_tail;


struct client{
    uint32_t id;
    uint32_t time;
    uint32_t balance;
};

struct client_queue{
    struct client* curr_client; 
    struct client_queue* next_client; //next in queue
    struct client_queue* prev_client; //ahead in queue
};

struct message_queue{
    uint32_t id;
    uint8_t msg[1024];
    struct message_queue* next_msg;
};

enum commands{
    ABORT = -1,
    BALANCE,
    REQ,
    REPLY,
    COMMIT,
    RELEASE
};

