/*
 * pipe.c - pipe transfer benchmark
 */
#include <stdio.h>
#include <stdlib.h>
#include "compbench.h"

typedef struct Pipe Pipe;
struct Pipe {
	int fd;
} pipedata;

void
pipeinit(Work *w)
{
}

void
pipespawn(Work *w)
{
	pid_t pid;
	int fd[2];
	Pipe *p;

	w->private = &pipedata;
	p = (Pipe*)w->private;

	if(pipe(fd) < 0) {
		perror("couldn't make pipe");
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
	// TODO: multiple observers
	pid = fork();
	switch(pid){
	case 0:
		p->fd = fd[0];
		producer(w);
		break;
	default:
		p->fd = fd[1];
		observer(w);
		break;
	}
}

int
pipesharework(Work *w)
{
	int n;
	Pipe *p;

	p = (Pipe*)w->private;
	printf("writing %d\n", p->fd);
	n = write(p->fd, w->buf, w->quanta);
	// FIXME: handle zero?
	if(n < 0) {
		perror("pipe write error");
		exit(1);
	}
	return 0;
}


Work*
pipegetwork(Work *w)
{
	int n;
	Pipe *p;

	p = (Pipe*)w->private;
	printf("reading %d\n", p->fd);
	n = read(p->fd, w->buf, w->quanta);
	if(n < 0) {
		perror("pipe read error");
		exit(1);
	}
	return NULL;
}

void
pipefinalize()
{

}

Bench pipebench = {
	.name = "pipe",
	.init = pipeinit,
	.sharework = pipesharework,
	.getwork = pipegetwork,
	.spawn = pipespawn,
	.finalize = pipefinalize,
};
