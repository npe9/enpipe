/*
 * xpmemault.h - template file for doing application specific producer observer app composition benchmarks
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#include <semaphore.h>
#include <xpmem.h>
#include <sched.h>
#include "compbench.h"

typedef struct Xpmem Xpmem;
struct Xpmem {
	sem_t full;
	sem_t mutex;
	sem_t empty;
	int i;
	int n;
	char** q;
	xpmem_segid_t *seg;
} xpmem;

void xpmeminit(Work *w) {
}

void xpmemspawn(Work *w) {
	struct xpmem_addr a;
	Xpmem *x;
	int i;
	__pid_t pid;
	void *status;
	void* stack;
	cpu_set_t mask;
	int result;

	CPU_ZERO(&mask);
	CPU_SET(w->prodcpu, &mask);
	result = sched_setaffinity(getpid(), sizeof(cpu_set_t), &mask);
	if (result < 0) {
		perror("mmap: couldn't set producer affinity");
		exit(1);
	}

	w->private = mmap(NULL, sizeof(struct Xpmem), PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	x = (Xpmem*) w->private;

	x->q = mmap(NULL, sizeof(char**) * qsize, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	x->seg = mmap(NULL, sizeof(xpmem_segid_t) * qsize, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	x->i = 0;
	x->n = 0;
	sem_init(&x->full, 1, 0);
	sem_init(&x->empty, 1, 0);
	sem_init(&x->mutex, 1, 0);

	sem_post(&x->mutex);
//	sem_post(&x->full);
	for (i = 0; i < qsize; i++) {
		sem_post(&x->empty);

		x->q[i] = mmap(NULL, w->quanta, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, 0, 0);
		if (x->q[i] == MAP_FAILED) {
			perror("mmap: couldn't allocate queue buffer");
			exit(1);
		}
		// is this a share or also an mmap?
		x->seg[i] = xpmem_make(x->q[i], w->quanta, XPMEM_PERMIT_MODE,
				(void*) (uintptr_t) 0600);
	}
	/* prime the queue, no need to mutex, observer doesn't exist yet */
	sem_wait(&x->empty);
	w->buf = x->q[x->n++];

	if (shouldobserve) {
		switch (pid = fork()) {
		default:
			printf("producer\n");
			producer(w);
			sem_wait(&x->empty);
			sem_wait(&x->mutex);
			munmap(x->q[x->n % qsize], w->quanta);
			x->q[x->n++ % qsize] = NULL;
			printf("shared work last w->buf %d %d %p\n", x->n - 1,
					(x->n - 1) % qsize, w->buf);
			sem_post(&x->mutex);
			sem_post(&x->full);
			sem_post(&x->full);

			// FIXME: wait for all observers
			if (shouldobserve) {
				wait(&status);
			}
			for (i = 0; i < qsize; i++) {
				if (x->q[i] == NULL)
					continue;
				munmap(x->q[i], w->quanta);
				x->q[i] = NULL;
			}
			munmap(x->q, sizeof(char**) * qsize);
			sem_destroy(&x->mutex);
			sem_destroy(&x->full);
			sem_destroy(&x->empty);
			break;
		case 0:
			CPU_ZERO(&mask);
			CPU_SET(w->obscpu, &mask);
			result = sched_setaffinity(0, sizeof(cpu_set_t), &mask);
			if (result < 0) {
				perror("mmap: couldn't set observer affinity");
				exit(1);
			}
			// clone here.
			for (i = 0; i < qsize; i++) {
				a.apid = xpmem_get(x->seg[i], XPMEM_RDWR, XPMEM_PERMIT_MODE,
						(void*) (uintptr_t) 0600);
				a.offset = 0;
				x->q[i] = xpmem_attach(a, w->quanta, NULL);
			}

			observer(w);
			exit(0);
			break;
		}
	} else {
		/* pretend the observer consumed */
		for (i = 0; i < w->datalen / w->quanta; i++) {
			sem_post(&x->full);
			sem_post(&x->empty);
		}

		producer(w);
		sem_wait(&x->empty);
		sem_wait(&x->mutex);
		munmap(x->q[x->n % qsize], w->quanta);
		x->q[x->n++ % qsize] = NULL;
		printf("shared work last w->buf %d %d %p\n", x->n - 1,
				(x->n - 1) % qsize, w->buf);
		sem_post(&x->mutex);
		sem_post(&x->full);
		sem_post(&x->full);

		for (i = 0; i < qsize; i++) {
			if (x->q[i] == NULL)
				continue;
			munmap(x->q[i], w->quanta);
			x->q[i] = NULL;
		}
		munmap(x->q, sizeof(char**) * qsize);
		sem_destroy(&x->mutex);
		sem_destroy(&x->full);
		sem_destroy(&x->empty);

	}
}

int xpmemsharework(Work *w) {
	Xpmem *x;

	x = (Xpmem*) w->private;

	sem_wait(&x->empty);
	sem_wait(&x->mutex);
	w->buf = x->q[x->n++ % qsize];
//	printf("shared work w->buf %d %d %p\n", x->n-1, (x->n-1) % qsize, w->buf);
	sem_post(&x->mutex);
	sem_post(&x->full);

	return 0;
}

Work*
xpmemgetwork(Work *w) {
	Xpmem *x;

	x = (Xpmem*) w->private;
	//	printf("getting work\n");
	sem_wait(&x->full);
	sem_wait(&x->mutex);
	w->rbuf = x->q[x->i++ % qsize];
	//	printf("got work w->rbuf %d %d %p\n", x->i-1, (x->i-1) % qsize, w->rbuf);
	sem_post(&x->mutex);
	sem_post(&x->empty);
	if (w->rbuf == NULL)
		return NULL;
	return w;

	return NULL;
}

void xpmemfinalize() {

}

Bench xpmembench = { .name = "xpmem", .init = xpmeminit, .spawn = xpmemspawn,
		.sharework = xpmemsharework, .getwork = xpmemgetwork, .finalize =
				xpmemfinalize, };
