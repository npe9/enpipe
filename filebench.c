/*
 * file.c - file transfer benchmark
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include "compbench.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sched.h>

typedef struct File File;
struct File {
	int fd;
	struct timeval *obstime;
} filepriv;

static void
fileinit()
{
}

void
filespawn(Work *w)
{
	File *f;
	int pid, status;
	char end;

	end = -1;

	w->private = (void*)&filepriv;
	f = (File*)w->private;
	f->fd = open("/tmp/filebench", O_RDWR | O_CREAT);
	unlink("/tmp/filebench");
	if(f->fd < 0) {
		perror("filebench: couldn't open /tmp/filebench");
		exit(1);
	}
	w->buf = malloc(w->quanta);
	if(w->buf == NULL) {
		perror("filebench: couldn't allocate w->buf");
		exit(1);
	}
	w->rbuf = malloc(w->quanta);
	if(w->rbuf == NULL) {
		perror("filebench: couldn't allocate f->rbuf");
		exit(1);
	}

	f->obstime = mmap(NULL, sizeof(struct timeval), PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,
			-1, 0);

	// so what do our file reader and writer do?
	// I need to call
	cpu_set_t  mask;
	int result;
	switch(pid = fork()) {
	case 0:
	    CPU_ZERO(&mask);
		CPU_SET(4, &mask);
		result = sched_setaffinity(0, CPU_ALLOC_SIZE(16), &mask);
		producer(w);
		// FIXME: wait for all observers
		write(f->fd, &end, 1);
		close(f->fd);
		wait(&status);
		memcpy(&w->b->obsresult, f->obstime, sizeof(struct timeval));
		free(w->rbuf);
		free(w->buf);
		break;
	default:
	    CPU_ZERO(&mask);
		CPU_SET(5, &mask);
		result = sched_setaffinity(0, CPU_ALLOC_SIZE(16), &mask);
		observer(w);
		memcpy(f->obstime, &w->b->obsresult, sizeof(struct timeval));
		close(f->fd);
		free(w->rbuf);
		exit(0);
		break;
	}
	// wait
}

static int
filesharework(Work *w)
{
	File *f;
	int n;

	f = (File*)w->private;
	n = write(f->fd, w->buf, w->quanta);
	if(n < 0) {
		perror("filebench: couldn't write to file descriptor");
		exit(1);
	}
	return 0;
}



static Work*
filegetwork(Work *w)
{
	File *f;
	int n;

	f = (File*)w->private;
again:
//	printf("%d %p %llu\n", f->fd, w->rbuf, w->quanta);
	n = read(f->fd, w->rbuf, w->quanta);
	/* FIXME: use poll? */
	if(n == 0)
		goto again;
	if(n < 0) {
		perror("filebench: couldn't read from file descriptor");
		exit(1);
	}
	if(w->rbuf[0] == -1) {
		return NULL;
	}
	return w;
}

static void
filefinalize()
{
	// who wakes the sleeper?
}

Bench filebench = {
	.name = "file",
	.init = fileinit,
	.spawn = filespawn,
	.sharework = filesharework,
	.getwork = filegetwork,
	.finalize = filefinalize,

};
