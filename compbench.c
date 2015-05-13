#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include "compbench.h"
#include "benches.h"
#include <fcntl.h>
#include <sched.h>

#define MAXLINE 4096
#define MAXLEN ULONG_MAX
#define MAXQUANTA ULONG_MAX
#define MAXWORK ULONG_MAX
#define MAXCPU 4096

pthread_mutex_t proclock;
int rank;
int flushcache;
int shouldobserve;
int qsize;

int producer(void *v) {
	unsigned long long i, j;
	Work *w;
	double workresult;
	struct timeval before, after;

	w = (Work*) v;
	workresult = 0.0;
	gettimeofday(&before, NULL);
	for (i = 0; i < w->datalen; i++) {
		for (j = 0; j < w->nprodwork; j++)
			workresult += workresult;

// FIXME: size boundary conditions


		w->buf[i % w->quanta] = workresult;
		if (i != 0 && (i % w->quanta) == 0) {
			if (w->b->sharework(w) < 0) {
				perror("couldn't share work");
				exit(1);
			}
		}
	}

	gettimeofday(&after, NULL);
	timersub(&after, &before, &w->b->prodresult);
	return 0;
}

int observer(void *v) {
	Work *w;
	unsigned long long i, j;
	int ret;
	struct timeval before, after;
	w = (Work*) v;

	ret = 0;
	gettimeofday(&before, NULL);
	while (w->b->getwork(w) != NULL) {
		for(i = 0; i < w->quanta; i++) {
			for(j = 0; j < w->nobswork; j++)
				ret++;
			ret += w->rbuf[i];
		}
//		for(i = w->quanta; i; i--) {
//			for(j = 0; j < w->nobswork; j++)
//				ret++;
//			ret += w->rbuf[i];
//		}
	}
	gettimeofday(&after, NULL);
	timersub(&after, &before, &w->b->obsresult);

	return ret;

}

void runbenchmark(char *line) {
	char *p;
	int i, nrun, fd;
	Work w;
	Bench **bb, *b, *torun[MAXBENCH];
	struct timeval before, after;


	nrun = 0;
	w.datalen = strtoull(line, &p, 10);
	if (w.datalen <= 0 || MAXLEN < w.datalen) {
		fprintf(stderr, "invalid data length: %llud\n", w.datalen);
		exit(1);
	}

	w.quanta = strtoull(p, &p, 10);
	if (w.quanta <= 0 || MAXQUANTA < w.quanta) {
		fprintf(stderr, "invalid quanta size: %llud\n", w.quanta);
		exit(1);
	}

	w.nprodwork = strtoull(p, &p, 10);
	if (w.nprodwork < 0 || MAXWORK < w.nprodwork) {
		fprintf(stderr, "invalid work amount: %llud\n", w.nprodwork);
		exit(1);
	}

	w.nobswork = strtoull(p, &p, 10);
	if (w.nobswork < 0 || MAXWORK < w.nobswork) {
		fprintf(stderr, "invalid work amount: %llud\n", w.nobswork);
		exit(1);
	}

	w.prodcpu = strtoull(p, &p, 10);
	if (w.prodcpu < 0 || MAXCPU < w.prodcpu) {
		fprintf(stderr, "invalid work amount: %llud\n", w.nobswork);
		exit(1);
	}

	w.obscpu = strtoull(p, &p, 10);
	if (w.obscpu < 0 || MAXCPU < w.obscpu) {
		fprintf(stderr, "invalid work amount: %llud\n", w.nobswork);
		exit(1);
	}

	/*	figure out how many benchmarks we want to run */
	p = strtok(p, " \n");
	do {
		for (bb = benches; bb != NULL; bb++)
			if (strcmp((*bb)->name, p) == 0) {
				torun[nrun++] = *bb;
				break;
			}
		if (bb == NULL) {
			fprintf(stderr, "invalid benchmark name: valid names are:");
			for (bb = benches; bb != NULL; bb++)
				fprintf(stderr, " %s", (*bb)->name);
			fprintf(stderr, "\n");
			exit(1);
		}
	} while ((p = strtok(NULL, " \n")) != NULL);
	for (i = 0; i < nrun; i++) {
		b = torun[i];
		w.b = b;
		/* different compositions approaches (eg. shared memory, message passing) have different approaches to allocating memory */
		if(flushcache) {
			sync();
			fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
			write(fd, "3", sizeof(char));
			close(fd);
		}
		gettimeofday(&before, NULL);
		b->spawn(&w);
		gettimeofday(&after, NULL);
		timersub(&after, &before, &b->totalresult);
	}
	if(rank == 0)
		printf("%llu %llu %llu %llu %d %d", w.datalen, w.quanta, w.nprodwork,
				w.nobswork, w.prodcpu, w.obscpu);
	for (i = 0; i < nrun; i++) {
		b = torun[i];
		if(rank == 0) {
			printf(" %ld.%06ld", b->prodresult.tv_sec, b->prodresult.tv_usec);
			printf(" %ld.%06ld", b->obsresult.tv_sec, b->obsresult.tv_usec);
			printf(" %ld.%06ld", b->totalresult.tv_sec, b->totalresult.tv_usec);
		}
	}
	printf("\n");
}

void usage() {
	fprintf(stderr, "compbench <file>\n");
	exit(1);
}

void main(int argc, char *argv[]) {
	char c;
	Bench **bb;
	char line[MAXLINE];
	FILE *f;

	rank = 0;
	flushcache = 0;
	shouldobserve = 1;
	qsize = 2;
	while ((c = getopt(argc, argv, "q:ne:f")) != -1) {
		switch (c) {
		case 'q':
			qsize = strtoull(optarg, NULL, 10);
			if (qsize <= 0 || MAX_QUEUE < qsize) {
				fprintf(stderr, "invalid queue size: %d\n", qsize);
				exit(1);
			}
			break;
		case 'n':
			shouldobserve = 0;
			break;
		case 'e':
			runbenchmark(optarg);
			exit(0);
			break;
		case 'f':
			flushcache = 1;
			break;
		default:
			printf("option %c\n", c);
			usage();
			break; /* shut up static analyzer */
		}
	}
	int i;
	switch (argc - optind) {
	case 0:
		f = stdin;
		break;
	case 1:
		f = fopen(argv[1], "r");
		break;
	default:
		printf("argc %d optind %d argc - optind %d\n", argc, optind, argc - optind);
		for(i = 0; i < argc; i++)
			printf("argv[%d] %s\n", i, argv[i]);
		usage();
		break; /* shut up static analyzers */
	}
	if (f == NULL) {
		perror("couldn't open input file");
		exit(1);
	}
	for (bb = benches; *bb != NULL; bb++)
		(*bb)->init();
	/* get the names of the benchmarks to run */
	while (fgets(line, MAXLINE, f) != NULL)
		runbenchmark(line);

	for (bb = benches; *bb != NULL; bb++)
		(*bb)->finalize();

	fclose(f);
	exit(0);
}
