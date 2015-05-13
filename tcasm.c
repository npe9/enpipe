/*
 * tcasm.c - template file for doing application specific producer observer app composition benchmarks
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sched.h>
#include <unistd.h>
#include "compbench.h"
#include <string.h>

#define MS_UPDATE 0x8

typedef struct Shared Shared;
struct Shared {
	int spin;
	struct timeval obstime;
};
typedef struct Tcasm Tcasm;
struct Tcasm {
	int shmfd;
	int spinfd;
	Shared *shared;
} tcasm;

#define STACK_SIZE 8192

void tcasminit() {
}

static int observerwrapper(void* v) {
	Work *w;
	Tcasm *t;

	observer(w);
	close(t->shmfd);
	close(t->spinfd);
	munmap(w->buf, w->quanta);
	exit(0);
}

void tcasmspawn(Work *w) {
	char *stack;
	Tcasm *t;
	__pid_t pid;
	int status;
	cpu_set_t mask;
	int result;

	w->private = (void*) &tcasm;
	t = (Tcasm*) w->private;
	t->shmfd = shm_open("/bench-shm.shm", O_CREAT | O_TRUNC | O_RDWR, 0666);
	if (t->shmfd < 0) {
		perror("couldn't open shared memory file");
		exit(1);
	}
	if (ftruncate(t->shmfd, w->datalen) < 0) {
		perror("tcasm: couldn't ftruncate initial buffer");
		exit(1);
	}
	w->buf = mmap(NULL, w->quanta, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, t->shmfd, 0);
	if (w->buf == MAP_FAILED) {
		perror("couldn't allocate");
		exit(1);
	}

	t->shared = mmap(NULL, sizeof(Shared), PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1 /* t->spinfd */, 0);



	if (shouldobserve)
		switch (pid = fork()) {
		default:
			CPU_ZERO(&mask);
			CPU_SET(w->prodcpu, &mask);
			result = sched_setaffinity(0, CPU_ALLOC_SIZE(16), &mask);
			if (result < 0) {
				perror("base: couldn't set affinity");
				exit(1);
			}
			producer(w);
			t->shared->spin = -1;
			close(t->shmfd);
			close(t->spinfd);
			munmap(w->buf, w->quanta);
			wait(&status);
			memcpy(&w->b->obsresult, &t->shared->obstime,
					sizeof(struct timeval));

			break;
		case 0:
			CPU_ZERO(&mask);
			CPU_SET(w->obscpu, &mask);
			result = sched_setaffinity(0, CPU_ALLOC_SIZE(16), &mask);
			if (result < 0) {
				perror("base: couldn't set affinity");
				exit(1);
			}
			observer(w);
			/* fork doesn't share but we need the time back from the observer
			 * clone() or a pthread might be better.
			 */
			memcpy(&t->shared->obstime, &w->b->obsresult,
					sizeof(struct timeval));

			close(t->shmfd);
			close(t->spinfd);
			munmap(w->buf, w->quanta);
			exit(0);
			break;
		}
	else {
		CPU_ZERO(&mask);
		CPU_SET(w->prodcpu, &mask);
		result = sched_setaffinity(0, CPU_ALLOC_SIZE(16), &mask);
		if (result < 0) {
			perror("base: couldn't set affinity");
			exit(1);
		}
		producer(w);
		close(t->shmfd);
		close(t->spinfd);
		munmap(w->buf, w->quanta);
		memcpy(&w->b->obsresult, &t->shared->obstime,
				sizeof(struct timeval));
	}

}

int tcasmsharework(Work *w) {
	Tcasm *t;

	t = (Tcasm*) w->private;
	t->shared->spin++;
	if (msync(w->buf, w->quanta, MS_SYNC | MS_UPDATE) < 0) {
		perror("tcasmsharework: msync failed: couldn't share work");
		exit(1);
	}
//	w->buf[0] = t->shared->spin;
//	printf("prod w->buf[0] %d\n", w->buf[0]);
	return 0;
}

Work*
tcasmgetwork(Work *w) {
	Tcasm *t;
	static int lastspin;
	static int first = 1;

	t = (Tcasm*) w->private;

	while (lastspin == t->shared->spin)
		;
	if (t->shared->spin == -1)
		return NULL;
//	w->buf[0] = -1;
	if (!first && munmap(w->rbuf, w->quanta) < 0) {
		perror("couldn't munmap observer");
		exit(1);
	}
	w->rbuf = mmap(NULL, w->quanta, PROT_READ, MAP_PRIVATE, t->shmfd, 0);
	if (w->rbuf == MAP_FAILED) {
		perror("couldn't allocate");
		exit(1);
	}
//	printf("obs w->buf[0] %d\n", w->buf[0]);
	lastspin = t->shared->spin;
	first = 0;
	return w;
}

void tcasmfinalize() {
}

Bench tcasmbench = { .name = "tcasm", .init = tcasminit, .sharework =
		tcasmsharework, .getwork = tcasmgetwork, .spawn = tcasmspawn,
		.finalize = tcasmfinalize, };
