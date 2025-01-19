#include "queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

void initQueue(Queue* q, int capacity) {
    q->capacity = capacity;
    q->size = 0;
    q->front = 0;
    q->rear = 0;
    q->vip_size = 0;
    q->vip_front = 0;
    q->vip_rear = 0;

    q->buffer = malloc(sizeof(Request) * capacity);
    if (q->buffer == NULL) {
        exit(1);
    }

    q->vip_buffer = malloc(sizeof(Request) * capacity);
    if (q->vip_buffer == NULL) {
        exit(1);
    }

    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void enqueue(Queue* q, Request req, int is_vip) {
    pthread_mutex_lock(&q->lock);

    if (isQueueFull(q)) {
        pthread_mutex_unlock(&q->lock);
        return;
    }

    if (is_vip) {
        q->vip_buffer[q->vip_rear] = req;
        q->vip_rear = (q->vip_rear + 1) % q->capacity;
        q->vip_size++;
    }
    else {
        q->buffer[q->rear] = req;
        q->rear = (q->rear + 1) % q->capacity;
        q->size++;
    }

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

Request dequeue(Queue* q, int is_vip) {
    Request req;

    pthread_mutex_lock(&q->lock);

    if (isQueueEmpty(q)) {
        pthread_mutex_unlock(&q->lock);
        return (Request) { -1, { 0, 0 } };
    }

    if (is_vip && q->vip_size > 0) {
        req = q->vip_buffer[q->vip_front];
        q->vip_front = (q->vip_front + 1) % q->capacity;
        q->vip_size--;
    }
    else {
        req = q->buffer[q->front];
        q->front = (q->front + 1) % q->capacity;
        q->size--;
    }

    pthread_mutex_unlock(&q->lock);
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

void dropRandomRequests(Queue* q, int percentage) {
    int to_remove = (q->size * percentage) / 100;
    for (int i = 0; i < to_remove; i++) {
        int rand_index = (q->front + rand() % q->size) % q->capacity;
        for (int j = rand_index; j != q->rear; j = (j + 1) % q->capacity) {
            q->buffer[j] = q->buffer[(j + 1) % q->capacity];
        }
        q->rear = (q->rear - 1 + q->capacity) % q->capacity;
        q->size--;
    }
}


