/*
 * func.c - template file for doing application specific producer observer app composition benchmarks
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sched.h>
#include "compbench.h"

#define MS_UPDATE 0x8

typedef struct Func Func;
struct Func {
	mqd_t mq;
} func;

#define STACK_SIZE 8192

void
funcinit()
{

}
void
funcspawn(Work *w)
{
	char *stack;
	Func *t;
	int status;


	w->private = (void*)&func;
	t = (Func*)w->private;

	w->buf = malloc(w->quanta);
	if(w->buf == NULL) {
		perror("func: couldn't allocate");
		exit(1);
	}

	w->rbuf = w->buf;

	producer(w);

	free(w->buf);
}

int
funcsharework(Work *w)
{
	observer(w);

	return 0;
}

/*
 * Simulate a one element queue. We don't actually do anything,
 * but the compbench API expects concurrent producer/consumer
 * queues of work, so follow that model.
 */
Work*
funcgetwork(Work *w)
{
	static int called;

	if(called){
		called = 0;
		return NULL;
	}
	called = 1;
	return w;
}



void
funcfinalize()
{
}

Bench funcbench = {
	.name = "func",
	.init = funcinit,
	.spawn = funcspawn,
	.sharework = funcsharework,
	.getwork = funcgetwork,
	.finalize = funcfinalize,
};
