#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include "segel.h"

typedef struct Request {
    int connfd;
    struct timeval arrival;
} Request;

typedef struct Queue {
    Request* buffer;     // Regular queue
    Request* vip_buffer; // VIP queue
    int capacity;
    int size;
    int vip_size;        // Number of VIP requests
    int front, rear;
    int vip_front, vip_rear;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    pthread_cond_t vip_not_empty; 
} Queue;

void initQueue(Queue* q, int capacity);
void enqueue(Queue* q, Request req, int is_vip);
Request dequeue(Queue* q, int vip);
int isQueueEmpty(Queue* q);
int isQueueFull(Queue* q);
void dropRandomRequests(Queue* q, int percentage);
void destroyQueue(Queue* q);

#endif
