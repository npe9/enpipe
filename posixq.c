/*
 * posixq.c - template file for doing application specific producer observer app composition benchmarks
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

typedef struct Posixq Posixq;
struct Posixq {
	mqd_t mq;
} posixq;

#define STACK_SIZE 8192

void
posixqinit()
{

}
void
posixqspawn(Work *w)
{
	char *stack;
	Posixq *t;
	int status;

	w->private = (void*)&posixq;
	t = (Posixq*)w->private;
	t->mq = mq_open("/bench-q.q", O_NONBLOCK);

	if (t->mq < 0) {
		perror("posixq: couldn't open posix message queue");
		exit(1);
	}

	w->buf = malloc(w->quanta);
	if(w->buf == NULL) {
		perror("posixq: couldn't allocate");
		exit(1);
	}

	w->rbuf = malloc(w->quanta);
	if(w->rbuf == NULL) {
		perror("posixq: couldn't allocate");
		exit(1);
	}

	stack = mmap(NULL, STACK_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_GROWSDOWN, -1, 0);
	if(stack == MAP_FAILED) {
		perror("posixq: couldn't make stack for observer");
		exit(1);
	}
	// pthreads might be better here.
	clone(observer, stack + STACK_SIZE, CLONE_FS, w);
	producer(w);

	wait(&status);
	// FIXME: handle dying children
	free(w->buf);
	free(w->rbuf);
	mq_close(t->mq);
}

int
posixqsharework(Work *w)
{
	ssize_t n;
	Posixq *q;

	q = (Posixq*)w->private;

	// TODO: decide what to do with priorities.
	n = mq_send(q->mq, w->buf, w->quanta, 0);
	if(n < 0) {
		// FIXME: what kind of error handling should I do here? what about eof?
		perror("posixq: couldn't share work to queue");
		exit(1);
	}
	return 0;
}


Work*
posixqgetwork(Work *w)
{
	ssize_t n;
	Posixq *q;

	q = (Posixq*)w->private;

	n = mq_receive(q->mq, w->rbuf, w->quanta, NULL);
	if(n < 0) {
		// FIXME: what kind of error handling should I do here? what about eof?
		perror("posixq: couldn't get work from queue");
		exit(1);
	}

	return NULL;
}



void
posixqfinalize()
{
	/* noop, the producer is the main thread so you automatically have synchronization */
	/* do we care if the observers haven't finished? Yes, if they're still running and causing noise */
	/* so we need a way for everyone to finish */
}

Bench posixqbench = {
	.name = "posixq",
	.init = posixqinit,
	.sharework = posixqsharework,
	.getwork = posixqgetwork,
	.spawn = posixqspawn,
	.finalize = posixqfinalize,
};
