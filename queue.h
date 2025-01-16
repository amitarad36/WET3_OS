#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include "segel.h"

typedef struct Request {
    int connfd;
    struct timeval arrival;
} Request;

typedef struct Queue {
    Request* buffer;
    int capacity;
    int size;
    int front;
    int rear;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} Queue;

void initQueue(Queue* q, int capacity);
void enqueue(Queue* q, Request req);
Request dequeue(Queue* q);
int isQueueEmpty(Queue* q);
int isQueueFull(Queue* q);
void dropRandomRequest(Queue* q);
void destroyQueue(Queue* q);

#endif
