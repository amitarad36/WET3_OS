#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

void initQueue(Queue* q, int capacity) {
    q->capacity = capacity;
    q->size = 0;
    q->front = 0;
    q->rear = -1;
    q->buffer = malloc(sizeof(Request) * capacity); 
    if (q->buffer == NULL) {
        fprintf(stderr, "Error: Queue memory allocation failed\n");
        exit(1);
    }
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void enqueue(Queue* q, Request req) {
    q->rear = (q->rear + 1) % q->capacity;
    q->buffer[q->rear] = req;
    q->size++;
}

Request dequeue(Queue* q) {
    if (q->size == 0) {
        fprintf(stderr, "Error: Trying to dequeue from an empty queue\n");
        exit(1);
    }
    Request req = q->buffer[q->front];
    q->front = (q->front + 1) % q->capacity;
    q->size--;
    return req;
}


int isQueueFull(Queue* q) {
    return q->size == q->capacity;
}

int isQueueEmpty(Queue* q) {
    return q->size == 0;
}

void destroyQueue(Queue* q) {
    free(q->buffer);
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}


#include <time.h>

void dropRandomRequest(Queue* q) {
    if (q->size == 0) return;

    int random_index = rand() % q->size;
    for (int i = random_index; i < q->size - 1; i++) {
        q->buffer[i] = q->buffer[i + 1];
    }
    q->size--;
}

