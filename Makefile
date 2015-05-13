#CARGS=-g -lrt
MPICC=mpicc
CC=cc

SRCS=$(wildcard test*.c)
EXES=$(SRCS:.c=.exe) compbench compbench-mpi

all: compbench compbench-mpi

%.exe: %.c
	$(MPICC) $(CARGS) $< -o $@ -lrt

clean:
	rm -f $(EXES)

compbench-mpi: compbench.c tcasm.c mpi.c kdbus.c kdbus-util.c kdbus-enum.c xpmem.c pipebench.c mmap.c filebench.c posixq.c func.c base.c compbench.h benches.h
	$(MPICC) -ggdb  -o $@ compbench.c kdbus.c kdbus-util.c kdbus-enum.c tcasm.c mpi.c xpmem.c pipebench.c mmap.c filebench.c posixq.c func.c base.c -lrt -lxpmem
#	cc -g  -o $@ compbench.c kdbus.c kdbus-util.c kdbus-enum.c tcasm.c mpi.c xpmem.c pipebench.c mmap.c filebench.c posixq.c func.c base.c -lrt -lxpmem

compbench: compbench.c tcasm.c  kdbus.c kdbus-util.c kdbus-enum.c xpmem.c pipebench.c mmap.c filebench.c posixq.c func.c base.c compbench.h benches.h
	gcc -ggdb -pthread -o $@ compbench.c kdbus.c kdbus-util.c kdbus-enum.c tcasm.c xpmem.c pipebench.c mmap.c filebench.c posixq.c func.c base.c -lrt -lxpmem

