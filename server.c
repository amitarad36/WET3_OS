#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "segel.h"
#include "request.h"
#include "queue.h"

// Global request queue
Queue request_queue;

void getargs(int* port, int* threads, int* queue_size, char** schedalg, int argc, char* argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <port> <threads> <queue_size> <schedalg>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    *queue_size = atoi(argv[3]);
    *schedalg = argv[4];
}

void* worker_thread(void* arg) {
    if (arg == NULL) {
        fprintf(stderr, "Error: worker_thread received NULL argument\n");
        return NULL;
    }
    threads_stats* t_stats = (threads_stats*)arg;

    while (1) {
        pthread_mutex_lock(&request_queue.lock);
        while (isQueueEmpty(&request_queue)) {
            pthread_cond_wait(&request_queue.not_empty, &request_queue.lock);
        }
        Request req = dequeue(&request_queue);
        pthread_cond_signal(&request_queue.not_full);
        pthread_mutex_unlock(&request_queue.lock);

        struct timeval dispatch;
        gettimeofday(&dispatch, NULL);

        requestHandle(req.connfd, req.arrival, dispatch, t_stats);
        Close(req.connfd);
    }

    free(t_stats);
    return NULL;
}

int main(int argc, char* argv[]) {
    int listenfd, connfd, port, clientlen;
    struct sockaddr_in clientaddr;

    // Command-line arguments for port, number of threads, queue size, and scheduling algorithm
    int threads, queue_size;
    char* schedalg;

    getargs(&port, &threads, &queue_size, &schedalg, argc, argv);

    // Initialize thread pool and request queue
    pthread_t* worker_threads = malloc(sizeof(pthread_t) * threads);
    if (worker_threads == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for worker threads\n");
        exit(1);
    }

    initQueue(&request_queue, queue_size);

    // Create worker threads
    for (int i = 0; i < threads; i++) {
        threads_stats* t_stats = malloc(sizeof(threads_stats));
        if (t_stats == NULL) {
            fprintf(stderr, "Error: Thread stats memory allocation failed\n");
            exit(1);
        }
        t_stats->id = i;
        t_stats->stat_req = 0;
        t_stats->dynm_req = 0;
        t_stats->total_req = 0;

        if (pthread_create(&worker_threads[i], NULL, worker_thread, (void*)t_stats) != 0) {
            fprintf(stderr, "Error: Failed to create thread %d\n", i);
            free(t_stats);
            exit(1);
        }
    }

    listenfd = Open_listenfd(port);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA*)&clientaddr, (socklen_t*)&clientlen);

        // Capture the arrival time of the request
        struct timeval arrival;
        gettimeofday(&arrival, NULL);

        pthread_mutex_lock(&request_queue.lock);

        // Check if the queue is full and handle according to the scheduling algorithm
        if (isQueueFull(&request_queue)) {
            if (strcmp(schedalg, "block") == 0) {
                // Wait until space is available in the queue
                while (isQueueFull(&request_queue)) {
                    pthread_cond_wait(&request_queue.not_full, &request_queue.lock);
                }
            }
            else if (strcmp(schedalg, "dt") == 0) {
                // Drop the new connection
                Close(connfd);
                pthread_mutex_unlock(&request_queue.lock);
                continue;
            }
            else if (strcmp(schedalg, "dh") == 0) {
                // Drop the oldest request
                dequeue(&request_queue);
            }
            else if (strcmp(schedalg, "random") == 0) {
                // Randomly drop a request from the queue
                dropRandomRequest(&request_queue);
            }
        }

        // Add the request to the queue
        enqueue(&request_queue, (Request) { connfd, arrival });
        pthread_cond_signal(&request_queue.not_empty);
        pthread_mutex_unlock(&request_queue.lock);
    }

    // Clean up resources (not typically reached in a server)
    for (int i = 0; i < threads; i++) {
        pthread_join(worker_threads[i], NULL);
    }
    free(worker_threads);
    destroyQueue(&request_queue);

    return 0;
}
