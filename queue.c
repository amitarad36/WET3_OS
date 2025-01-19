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
        fprintf(stderr, "Error: Queue memory allocation failed\n");
        exit(1);
    }

    q->vip_buffer = malloc(sizeof(Request) * capacity);
    if (q->vip_buffer == NULL) {
        fprintf(stderr, "Error: VIP Queue memory allocation failed\n");
        exit(1);
    }

    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void enqueue(Queue* q, Request req, int is_vip) {
    pthread_mutex_lock(&q->lock);

    if (isQueueFull(q)) {
        printf("Queue is full! Dropping request (fd=%d)\n", req.connfd);
        fflush(stdout);
        pthread_mutex_unlock(&q->lock);
        return;
    }

    printf("Enqueueing request: fd=%d | Queue Size Before: %d\n", req.connfd, q->size);
    fflush(stdout);

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

    printf("Request enqueued: fd=%d | Queue Size After: %d\n", req.connfd, q->size);
    fflush(stdout);

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

Request dequeue(Queue* q, int is_vip) {
    Request req;

    printf("Dequeue function called. Queue Size: %d\n", q->size);
    fflush(stdout);

    pthread_mutex_lock(&q->lock);
    printf("Mutex locked inside dequeue.\n");
    fflush(stdout);

    if (isQueueEmpty(q)) {
        printf("Queue is EMPTY inside dequeue(), returning invalid request!\n");
        fflush(stdout);
        pthread_mutex_unlock(&q->lock);
        printf("Mutex unlocked (empty queue).\n");
        fflush(stdout);
        return (Request) { -1, { 0, 0 } };
    }

    printf("Stored request at front (fd=%d)\n", q->buffer[q->front].connfd);
    fflush(stdout);

    if (is_vip && q->vip_size > 0) {
        req = q->vip_buffer[q->vip_front];  // Copy struct
        q->vip_front = (q->vip_front + 1) % q->capacity;
        q->vip_size--;
    }
    else {
        req = q->buffer[q->front];  // Copy struct
        q->front = (q->front + 1) % q->capacity;
        q->size--;
    }

    printf("Dequeued request: fd=%d | Queue Size After: %d\n", req.connfd, q->size);
    fflush(stdout);

    pthread_mutex_unlock(&q->lock);
    printf("Mutex unlocked inside dequeue.\n");
    fflush(stdout);

    return req;
}

int isQueueFull(Queue* q) {
    return q->size == q->capacity;
}

int isQueueEmpty(Queue* q) {
    int result = (q->size == 0);
    printf("isQueueEmpty() called: returning %d (size=%d)\n", result, q->size);
    fflush(stdout);
    return result;
}

void destroyQueue(Queue* q) {
    free(q->buffer);
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

void dropRandomRequests(Queue* q, int percentage) {
    int to_remove = (q->size * percentage) / 100; // Calculate how many to drop
    for (int i = 0; i < to_remove; i++) {
        int rand_index = (q->front + rand() % q->size) % q->capacity;
        for (int j = rand_index; j != q->rear; j = (j + 1) % q->capacity) {
            q->buffer[j] = q->buffer[(j + 1) % q->capacity]; // Shift elements left
        }
        q->rear = (q->rear - 1 + q->capacity) % q->capacity;
        q->size--;
    }
}


