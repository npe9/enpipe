/*
 * default.h - template file for doing application specific producer observer app composition benchmarks
 */
#include <unistd.h>
#include "compbench.h"
/*
 * Producerinit does all of the necessary work to set up a producer. This includes setting up any buffers oir shared memory regions.
 */
void
defproducerinit()
{
	// the producer allocates the memory
	// the consumer adds it.
}

/*
 * Observerinit sets up mappings to the shared data structures provided by the producer. This can be dialing a connection, opening a file
 */
void
defobserverinit()
{

}
/*
 * Sharework produces data in nwork size chunks (see the definition of Buf in compbench.h) It is used to simulate messaging
 * and descrete sharing for application composition.
 */
int
defsharework(Work *w)
{
	// msync here
	return 0;
}

/*
 * Shareall shares the entire data set made by the producer. It is used for simulating checkpointing.
 */
int
defshareall(Work *w)
{
	return 0;
}

Work*
defgetwork(Work *w)
{
	return NULL;
}

char*
defbenchalloc(Work *w)
{
	return NULL;
}

// you also need a function to make it possible to couple these more tightly.
// for example a MPI or an OpenMP version of this manage themselves.
// in many cases you need to be able to manage this.
// so for mpi

void
defspawn()
{

}

Bench defbench = {
	.name = "default", /* default is a reserved word in C */
	.producerinit = defproducerinit,
	.observerinit = defobserverinit,
	.sharework = defsharework,
	.shareall = defshareall,
	.getwork = defgetwork,
	.spawn = defspawn,
};
