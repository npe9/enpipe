/*
 * file.c - file transfer benchmark
 */
#include <stdio.h>
#include <stdlib.h>
#include "compbench.h"

typedef struct File File;
struct File {
	int fd;
	char *rbuf;
} file;

static void
fileinit()
{


	// the producer allocates the memory
	// the consumer adds it.
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
	n = read(f->fd, f->rbuf, w->quanta);
	if(n < 0) {
		perror("filebench: couldn't read from file descriptor");
		exit(1);
	}
	return NULL;
}

void
filespawn(Work *w)
{
	File *f;
	int pid, status;

	f = (File*)w->private;
	w->buf = malloc(w->quanta);
	if(w->buf == NULL) {
		perror("filebench: couldn't allocate w->buf");
		exit(1);
	}
	f->rbuf = malloc(w->quanta);
	if(f->rbuf == NULL) {
		perror("filebench: couldn't allocate f->rbuf");
		exit(1);
	}

	// so what do our file reader and writer do?
	// I need to call
	switch(pid = fork()) {
	case 0:
		producer(w);
		// FIXME: wait for all observers
		wait(&status);
		close(f->fd);
		free(f->rbuf);
		free(w->buf);
		break;
	default:
		observer(w);
		close(f->fd);
		free(f->rbuf);
		exit(0);
		break;
	}
	// wait
}

static void
filefinalize()
{
	// who wakes the sleeper?
}

Bench filebench = {
	.name = "file",
	.init = fileinit,
	.sharework = filesharework,
	.getwork = filegetwork,
	.finalize = filefinalize,

};
