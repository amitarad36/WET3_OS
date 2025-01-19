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
        while (isQueueEmpty(&request_queue)) {
            pthread_cond_wait(&request_queue.not_empty, &request_queue.lock);
        }
        pthread_mutex_unlock(&request_queue.lock);

        Request req = dequeue(&request_queue, 0);

        if (req.connfd <= 0) {
            continue;
        }

        struct timeval dispatch;
        gettimeofday(&dispatch, NULL);

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
        exit(1);
    }

    for (int i = 0; i < threads; i++) {
        threads_stats t_stats = malloc(sizeof(struct Threads_stats));
        if (t_stats == NULL) {
            exit(1);
        }
        t_stats->id = i;
        t_stats->stat_req = 0;
        t_stats->dynm_req = 0;
        t_stats->total_req = 0;

        if (pthread_create(&worker_threads[i], NULL, worker_thread, (void*)t_stats) != 0) {
            free(t_stats);
            exit(1);
        }
    }

    pthread_t vip_thread_id;
    if (pthread_create(&vip_thread_id, NULL, vip_thread, NULL) != 0) {
        exit(1);
    }

    listenfd = Open_listenfd(port);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA*)&clientaddr, (socklen_t*)&clientlen);

        struct timeval arrival;
        gettimeofday(&arrival, NULL);

        int is_vip = getRequestType(connfd);
        enqueue(&request_queue, (Request) { connfd, arrival }, is_vip);
    }

    for (int i = 0; i < threads; i++) {
        pthread_join(worker_threads[i], NULL);
    }
    pthread_join(vip_thread_id, NULL);
    free(worker_threads);
    destroyQueue(&request_queue);

    return 0;
}
