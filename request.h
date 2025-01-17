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
int isStaticRequest(int fd);  // Add this line
void sendHttpResponseWithStats(int fd, struct timeval arrival, struct timeval dispatch, threads_stats t_stats); // Add this line


#endif
