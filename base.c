/*
 * base.c - template file for doing application specific producer observer app composition benchmarks
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sched.h>
#include "compbench.h"


typedef struct Base Base;
struct Base {
} base;


void
baseinit()
{

}
void
basespawn(Work *w)
{
	char *stack;
	Base *t;
	int status;
	cpu_set_t  mask;
	int result;

	/* FIXME: let the user set this */
	CPU_ZERO(&mask);
	CPU_SET(w->prodcpu, &mask);
	result = sched_setaffinity(0, sizeof(cpu_set_t), &mask);
	if(result < 0) {
		perror("base: couldn't set affinity");
		exit(1);
	}


	w->private = (void*)&base;
	t = (Base*)w->private;

	w->buf = malloc(w->quanta);
	if(w->buf == NULL) {
		perror("base: couldn't allocate");
		exit(1);
	}

	w->rbuf = w->buf;

	producer(w);
	free(w->buf);
}

int
basesharework(Work *w)
{
	return 0;
}

/*
 * Simulate a one element queue. We don't actually do anything,
 * but the compbench API expects concurrent producer/consumer
 * queues of work, so follow that model.
 */
Work*
basegetwork(Work *w)
{
	return NULL;
}



void
basefinalize()
{
}

Bench basebench = {
	.name = "base",
	.init = baseinit,
	.spawn = basespawn,
	.sharework = basesharework,
	.getwork = basegetwork,
	.finalize = basefinalize,
};
