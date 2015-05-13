#pragma once

#include <sys/time.h>
#include <pthread.h>
#define MAXBENCH 256
#define MAX_QUEUE 1024

typedef struct Work Work;
typedef struct Bench Bench;

extern pthread_mutex_t proclock;

extern int rank;
extern int shouldobserve;
extern int qsize;

struct Work {
	unsigned long long datalen;
	unsigned long long quanta;
	char* buf;
	char* rbuf;
	unsigned long long nprodwork;
	unsigned long long nobswork;
	int prodcpu;
	int obscpu;
	void *private;
	Bench *b;
};

struct Bench {
	char name[256];
	struct timeval totalresult; /* so we can cache benchmark results by benchmark type */
	struct timeval prodresult; /* so we can cache benchmark results by benchmark type */
	struct timeval obsresult; /* so we can cache benchmark results by benchmark type */
	void (*init)();
	int (*sharework)(Work *w);
	int (*shareall)(Work *w);
	Work *(*getwork)(Work *w);
	char *(*benchalloc)(Work *w);
	void (*spawn)(Work *w);
	void (*finalize)();
};

int producer(void *v);
int observer(void *v);

