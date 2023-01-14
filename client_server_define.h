#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

volatile pthread_mutex_t bank_lock;
volatile int client_count;

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

enum commands{
    ABORT = -1,
    BALANCE,
    REQ,
    REPLY,
    SEND
};

enum status{
    FAILED = -1,
    IN_PROG,
    SUCCESS
};

