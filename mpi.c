/*
 * mpi.c - mpi transfer benchmark
 */
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "compbench.h"

typedef struct Mpi Mpi;
struct Mpi {
} mpi;

void
mpiinit()
{
	MPI_Init(NULL, NULL);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
}

void
mpispawn(Work *w)
{
	Mpi *t;

	t = (Mpi*)w->private;
	w->buf = malloc(w->quanta);
	if(w->buf == NULL) {
		perror("couldn't allocate");
		exit(1);
	}
	// FIXME: multiple producers
	switch(rank) {
	case 0:
		producer(w);
		MPI_Send(w->buf, 0, MPI_CHAR, 1, 0, MPI_COMM_WORLD);
		free(w->buf);
		break;
	case 1:
		observer(w);
		break;
	default:
		// FIXME: allow multiple observers
		fprintf(stderr, "multiple observers not implemented yet");
		exit(1);
	}
}


int
mpisharework(Work *w)
{
	static int sent;
	int ret;
	ret = MPI_Send(w->buf, w->quanta, MPI_CHAR, 1, 0, MPI_COMM_WORLD);
	if(ret != MPI_SUCCESS) {
		fprintf(stderr, "mpi: couldn't send: reason %d\n", ret);
		exit(1);
	}
	return 0;
}


Work*
mpigetwork(Work *w)
{
	int count;
	static int recv;
	int ret;
	MPI_Status stat;
	// this is where the messaging is important.
	ret = MPI_Recv(w->buf, w->quanta, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &stat);
	if(ret != MPI_SUCCESS) {
		fprintf(stderr, "MPI_Recv failed: reason %d", ret);
		exit(1);
	}
	MPI_Get_count(&stat, MPI_CHAR, &count);
	if(count == MPI_UNDEFINED) {
		fprintf(stderr, "bad count!\n");
		exit(1);
	}
	if(count == 0) {
		return NULL;
	}
	return w;
}

void
mpifinalize()
{
	MPI_Finalize();
}

Bench mpibench = {
	.name = "mpi",
	.init = mpiinit,
	.sharework = mpisharework,
	.getwork = mpigetwork,
	.spawn = mpispawn,
	.finalize = mpifinalize,
};
