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
    // Allocate a threads_stats struct for the VIP thread
    threads_stats t_stats = malloc(sizeof(struct Threads_stats));
    if (t_stats == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for VIP thread stats\n");
        pthread_exit(NULL);
    }
    t_stats->id = -1;  // Use -1 to indicate VIP thread
    t_stats->stat_req = 0;
    t_stats->dynm_req = 0;
    t_stats->total_req = 0;

    while (1) {
        pthread_mutex_lock(&request_queue.lock);
        while (isQueueEmpty(&request_queue) || request_queue.vip_size == 0) {
            pthread_cond_wait(&request_queue.vip_not_empty, &request_queue.lock);
        }
        Request req = dequeue(&request_queue, 1); // VIP queue
        pthread_mutex_unlock(&request_queue.lock);

        struct timeval dispatch;
        gettimeofday(&dispatch, NULL);

        requestHandle(req.connfd, req.arrival, dispatch, t_stats);
        Close(req.connfd);
    }

    free(t_stats); // Should never reach here
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
    threads_stats t_stats = (threads_stats)arg;

    while (1) {
        pthread_mutex_lock(&request_queue.lock);
        while (isQueueEmpty(&request_queue) || request_queue.vip_size > 0) {
            pthread_cond_wait(&request_queue.not_empty, &request_queue.lock);
        }
        Request req = dequeue(&request_queue, 0); // Regular queue
        pthread_mutex_unlock(&request_queue.lock);

        struct timeval dispatch;
        gettimeofday(&dispatch, NULL);

        requestHandle(req.connfd, req.arrival, dispatch, t_stats);
        Close(req.connfd);
    }

    free(t_stats); // Should never reach here
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
        threads_stats t_stats = malloc(sizeof(struct Threads_stats)); // Correct: Allocates space for struct
        if (t_stats == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory for thread stats\n");
            exit(1);
        }
        t_stats->id = i;
        t_stats->stat_req = 0;
        t_stats->dynm_req = 0;
        t_stats->total_req = 0;

        if (pthread_create(&worker_threads[i], NULL, worker_thread, (void*)t_stats) != 0) {
            fprintf(stderr, "Error: Failed to create worker thread %d\n", i);
            free(t_stats);
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
        printf("Waiting for new connection...\n");
        fflush(stdout);

        connfd = Accept(listenfd, (SA*)&clientaddr, (socklen_t*)&clientlen);
        printf("Accepted connection from client (fd=%d)\n", connfd);
        fflush(stdout);

        // Capture the arrival time of the request
        struct timeval arrival;
        gettimeofday(&arrival, NULL);

        printf("Checking request type (VIP or Regular)...\n");
        fflush(stdout);

        int is_vip = getRequestType(connfd);
        printf("Determined request type: %s\n", is_vip ? "VIP" : "Regular");
        fflush(stdout);

        pthread_mutex_lock(&request_queue.lock);

        printf("About to enqueue request (fd=%d)...\n", connfd);
        fflush(stdout);

        printf("DEBUG: Preparing to call enqueue()...\n");
        fflush(stdout);

        Request debug_req = { connfd, arrival };
        printf("DEBUG: Request Struct - fd: %d, time: %lu\n", debug_req.connfd, debug_req.arrival.tv_sec);
        fflush(stdout);

        enqueue(&request_queue, debug_req, is_vip);

        printf("DEBUG: enqueue() completed successfully!\n");
        fflush(stdout);
        printf("Request successfully enqueued (fd=%d)\n", connfd);
        fflush(stdout);

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
