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

void* vip_thread(void* arg) {
    while (1) {
        pthread_mutex_lock(&request_queue.lock);
        while (isQueueEmpty(&request_queue) || request_queue.vip_size == 0) {
            pthread_cond_wait(&request_queue.vip_not_empty, &request_queue.lock);
        }
        Request req = dequeue(&request_queue, 1); 
        pthread_mutex_unlock(&request_queue.lock);

        struct timeval dispatch;
        gettimeofday(&dispatch, NULL);

        requestHandle(req.connfd, req.arrival, dispatch, NULL);
        Close(req.connfd);
    }
}

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
    int thread_id = *((int*)arg); // Get thread index
    free(arg); // Free dynamically allocated thread ID

    // Allocate memory for thread stats
    threads_stats t_stats = malloc(sizeof(threads_stats));
    if (t_stats == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for thread stats\n");
        pthread_exit(NULL);
    }

    // Initialize thread statistics
    t_stats->id = thread_id;
    t_stats->stat_req = 0;
    t_stats->dynm_req = 0;
    t_stats->total_req = 0;

    while (1) {
        pthread_mutex_lock(&request_queue.lock);
        while (isQueueEmpty(&request_queue) || request_queue.vip_size > 0) {
            pthread_cond_wait(&request_queue.not_empty, &request_queue.lock);
        }
        Request req = dequeue(&request_queue, 0); 
        pthread_mutex_unlock(&request_queue.lock);

        struct timeval dispatch;
        gettimeofday(&dispatch, NULL);

        requestHandle(req.connfd, req.arrival, dispatch, t_stats);
        Close(req.connfd);
    }

    // Free memory before exiting (not usually reached)
    free(t_stats);
    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    int listenfd, connfd, port, clientlen;
    struct sockaddr_in clientaddr;

    // Command-line arguments for port, number of threads, queue size, and scheduling algorithm
    int threads, queue_size;
    char* schedalg;

    getargs(&port, &threads, &queue_size, &schedalg, argc, argv);

    // Initialize request queue
    initQueue(&request_queue, queue_size);

    // Create worker threads
    pthread_t* worker_threads = malloc(sizeof(pthread_t) * threads);
    if (worker_threads == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for worker threads\n");
        exit(1);
    }

    for (int i = 0; i < threads; i++) {
        int* thread_id = malloc(sizeof(int));
        if (thread_id == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory for thread ID\n");
            exit(1);
        }
        *thread_id = i;

        if (pthread_create(&worker_threads[i], NULL, worker_thread, (void*)thread_id) != 0) {
            fprintf(stderr, "Error: Failed to create worker thread %d\n", i);
            free(thread_id);
            exit(1);
        }
    }

    // Create VIP thread
    pthread_t vip_thread_id;
    if (pthread_create(&vip_thread_id, NULL, vip_thread, NULL) != 0) {
        fprintf(stderr, "Error: Failed to create VIP thread\n");
        exit(1);
    }

    listenfd = Open_listenfd(port);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA*)&clientaddr, (socklen_t*)&clientlen);

        // Capture the arrival time of the request
        struct timeval arrival;
        gettimeofday(&arrival, NULL);

        int is_vip = getRequestType(connfd); // Check if request is VIP

        pthread_mutex_lock(&request_queue.lock);

        // If the queue is full, apply the appropriate overload policy
        if (isQueueFull(&request_queue)) {
            if (is_vip) {
                // VIP requests always block until a slot is available
                while (isQueueFull(&request_queue)) {
                    pthread_cond_wait(&request_queue.not_full, &request_queue.lock);
                }
            }
            else {
                // Apply the specified overload policy for regular requests
                if (strcmp(schedalg, "block") == 0) {
                    while (isQueueFull(&request_queue)) {
                        pthread_cond_wait(&request_queue.not_full, &request_queue.lock);
                    }
                }
                else if (strcmp(schedalg, "dt") == 0) { // Drop Tail
                    Close(connfd);
                    pthread_mutex_unlock(&request_queue.lock);
                    continue;
                }
                else if (strcmp(schedalg, "dh") == 0) { // Drop Head
                    dequeue(&request_queue, 0); // Remove the oldest request
                }
                else if (strcmp(schedalg, "block_flush") == 0) { // Bonus Policy
                    while (request_queue.size > 0) {
                        pthread_cond_wait(&request_queue.not_empty, &request_queue.lock);
                    }
                    Close(connfd);
                    pthread_mutex_unlock(&request_queue.lock);
                    continue;
                }
                else if (strcmp(schedalg, "drop_random") == 0) { // Bonus Policy
                    dropRandomRequests(&request_queue, 50); // Drop 50% of regular requests
                }
            }
        }

        // Add the request to the appropriate queue
        enqueue(&request_queue, (Request) { connfd, arrival }, is_vip);

        pthread_mutex_unlock(&request_queue.lock);
    }

    // Cleanup (Not usually reached)
    for (int i = 0; i < threads; i++) {
        pthread_join(worker_threads[i], NULL);
    }
    pthread_join(vip_thread_id, NULL);
    free(worker_threads);
    destroyQueue(&request_queue);

    return 0;
}
