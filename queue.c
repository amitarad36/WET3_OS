#include "queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

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

void* worker_thread(void* arg) {
    threads_stats t_stats = (threads_stats)arg;

    while (1) {
        pthread_mutex_lock(&request_queue.lock);

        while (isQueueEmpty(&request_queue)) {
            pthread_cond_wait(&request_queue.not_empty, &request_queue.lock);
        }

        Request req = dequeue(&request_queue, 0);

        pthread_mutex_unlock(&request_queue.lock);

        // **Ensure we have a valid request**
        if (req.connfd == -1) {
            continue;
        }

        struct timeval dispatch;
        gettimeofday(&dispatch, NULL);

        // **Process the request**
        requestHandle(req.connfd, req.arrival, dispatch, t_stats);
        Close(req.connfd);
    }
}

Request dequeue(Queue* q, int is_vip) {
    pthread_mutex_lock(&q->lock);

    if (isQueueEmpty(q)) {
        printf("ERROR: Dequeue called but queue is empty!\n");
        fflush(stdout);
        pthread_mutex_unlock(&q->lock);
        return (Request) { -1, { 0, 0 } }; // Return an invalid request
    }

    Request req;
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

    printf("Dequeued request (fd=%d). New queue size: %d\n", req.connfd, q->size);
    fflush(stdout);

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


