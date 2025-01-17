#ifndef __REQUEST_H__
#define __REQUEST_H__

#include <pthread.h>


typedef struct Threads_stats{
	int id;
	int stat_req;
	int dynm_req;
	int total_req;
} * threads_stats;

// Global mutex for statistics
extern pthread_mutex_t stat_lock;

void requestHandle(int fd, struct timeval arrival, struct timeval dispatch, threads_stats t_stats);
int getRequestType(int fd);
int requestParseURI(char* uri, char* filename, char* cgiargs);
void requestServeStatic(int fd, char* filename, int filesize, struct timeval arrival, struct timeval dispatch, threads_stats t_stats);
void requestServeDynamic(int fd, char* filename, char* cgiargs);
void requestError(int fd, char* cause, char* errnum, char* shortmsg, char* longmsg, struct timeval arrival, struct timeval dispatch, threads_stats t_stats);

#endif
