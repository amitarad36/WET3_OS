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
    threads_stats t_stats = malloc(sizeof(struct Threads_stats));
    if (t_stats == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for VIP thread stats\n");
        pthread_exit(NULL);
    }
    t_stats->id = -1;
    t_stats->stat_req = 0;
    t_stats->dynm_req = 0;
    t_stats->total_req = 0;

    while (1) {
        pthread_mutex_lock(&request_queue.lock);
        while (isQueueEmpty(&request_queue) || request_queue.vip_size == 0) {
            pthread_cond_wait(&request_queue.vip_not_empty, &request_queue.lock);
        }
        Request req = dequeue(&request_queue, 1);
        pthread_mutex_unlock(&request_queue.lock);

        struct timeval dispatch;
        gettimeofday(&dispatch, NULL);

        requestHandle(req.connfd, req.arrival, dispatch, t_stats);
        Close(req.connfd);
    }

    free(t_stats);
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

        // **Wait until a request is available**
        while (isQueueEmpty(&request_queue)) {
            pthread_cond_wait(&request_queue.not_empty, &request_queue.lock);
        }

        // **Dequeue request**
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

int main(int argc, char* argv[]) {
    int listenfd, connfd, port, clientlen;
    struct sockaddr_in clientaddr;

    int threads, queue_size;
    char* schedalg;

    getargs(&port, &threads, &queue_size, &schedalg, argc, argv);

    initQueue(&request_queue, queue_size);

    pthread_t* worker_threads = malloc(sizeof(pthread_t) * threads);
    if (worker_threads == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for worker threads\n");
        exit(1);
    }

    for (int i = 0; i < threads; i++) {
        threads_stats t_stats = malloc(sizeof(struct Threads_stats));
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

    pthread_t vip_thread_id;
    if (pthread_create(&vip_thread_id, NULL, vip_thread, NULL) != 0) {
        fprintf(stderr, "Error: Failed to create VIP thread\n");
        exit(1);
    }

    listenfd = Open_listenfd(port);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA*)&clientaddr, (socklen_t*)&clientlen);
        printf("Accepted connection from client (fd=%d)\n", connfd);
        fflush(stdout);

        // Capture the arrival time of the request
        struct timeval arrival;
        gettimeofday(&arrival, NULL);

        int is_vip = getRequestType(connfd);

        printf("About to enqueue request (fd=%d)...\n", connfd);
        fflush(stdout);

        enqueue(&request_queue, (Request) { connfd, arrival }, is_vip);

        printf("Request successfully enqueued (fd=%d)\n", connfd);
        fflush(stdout);
    }
    for (int i = 0; i < threads; i++) {
        pthread_join(worker_threads[i], NULL);
    }
    pthread_join(vip_thread_id, NULL);
    free(worker_threads);
    destroyQueue(&request_queue);

    return 0;
}
