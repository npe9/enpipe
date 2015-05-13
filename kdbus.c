#include "compbench.h"

/*
 * kdbus.c - kdbus transfer benchmark
 * much of this code is based upon the test-kdbus-benchmark from the kdbus test suite.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include "kdbus-util.h"
#include "compbench.h"
#include <fcntl.h>
#include <string.h>
#include <poll.h>

typedef struct Kdbus Kdbus;
struct Kdbus {
	struct conn *c1;
	struct conn *c2;
	struct kdbus_cmd_memfd_make mfd;
} kdbus;


static void
kdbusinit()
{


	// the producer allocates the memory
	// the consumer adds it.
}

void
kdbusspawn(Work *w)
{
	Kdbus *k;
	int fd, memfd, pid, status, ret;
	struct conn *c;
	char *bus;

	struct {
		struct kdbus_cmd_make head;

		/* bloom size item */
		struct {
			uint64_t size;
			uint64_t type;
			struct kdbus_bloom_parameter bloom;
		} bs;

		/* name item */
		uint64_t n_size;
		uint64_t n_type;
		char name[64];
	} bus_make;

	w->private = &kdbus;
	k = w->private;

	fd = open("/dev/kdbus/control", O_RDWR|O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "--- error %d (%m)\n", fd);
		exit(1);
	}

	memset(&bus_make, 0, sizeof(bus_make));
	bus_make.bs.size = sizeof(bus_make.bs);
	bus_make.bs.type = KDBUS_ITEM_BLOOM_PARAMETER;
	bus_make.bs.bloom.size = 64;
	bus_make.bs.bloom.n_hash = 1;

	snprintf(bus_make.name, sizeof(bus_make.name), "%u-testbus", getuid());
	bus_make.n_type = KDBUS_ITEM_MAKE_NAME;
	bus_make.n_size = KDBUS_ITEM_HEADER_SIZE + strlen(bus_make.name) + 1;

	bus_make.head.size = sizeof(struct kdbus_cmd_make) +
			     sizeof(bus_make.bs) +
			     bus_make.n_size;

	ret = ioctl(fd, KDBUS_CMD_BUS_MAKE, &bus_make);
	if (ret) {
		fprintf(stderr, "--- error %d (%m)\n", ret);
		exit(1);
	}

	if (asprintf(&bus, "/dev/kdbus/%s/bus", bus_make.name) < 0) {
		perror("kdbus: couldn't asprintf");
	}

	k->c1 = kdbus_hello(bus, 0);
	if (!k->c1) {
		perror("kdbus: couldn't connect 1");
		exit(1);
	}

	k->c2 = kdbus_hello(bus, 0);
	if (!k->c2) {
		perror("kdbus: couldn't connect 2");
		exit(1);
	}

	k->mfd.size = sizeof(struct kdbus_cmd_memfd_make);
	ret = ioctl(k->c1->fd, KDBUS_CMD_MEMFD_NEW, &k->mfd);
	if (ret < 0) {
		fprintf(stderr, "kdbus: spawn: couldn't make new memfd: %m\n");
		exit(1);
	}
	printf("mfd fd %d\n", k->mfd.fd);

	memfd = k->mfd.fd;
//	w->buf = mmap(NULL, sizeof(struct kdbus_cmd_memfd_make), PROT_READ, MAP_SHARED, memfd, 0);
//	if(w->buf == MAP_FAILED) {
//		perror("kdbus: couldn't allocate w->buf");
//		exit(1);
//	}
	switch(pid = fork()) {
	case 0:
		printf("producing\n");
		producer(w);
		// FIXME: wait for all observers
		printf("produced\n");
		wait(&status);
		free(w->buf);
		free(w->rbuf);
		close(k->c1->fd);
		close(k->c2->fd);
		close(fd);
		break;
	default:
		observer(w);
		exit(0);
		break;
	}
	// wait
}

static int
kdbussharework(Work *w)
{
	Kdbus *k;
	int n;
	struct kdbus_msg *msg;
	struct kdbus_item *item;
	uint64_t size;
	int ret;

	k = (Kdbus*)w->private;
	printf("sharing\n");

	size = sizeof(struct kdbus_msg);

	printf("sealing\n");
//	munmap(w->buf, sizeof(struct kdbus_cmd_memfd_make));
	ret = ioctl(k->mfd.fd, KDBUS_CMD_MEMFD_SIZE_SET, &k->mfd.size);
	if (ret) {
		fprintf(stderr, "error setting size: %d err %d (%m)\n", ret, errno);
		exit(1);
	}

	ret = ioctl(k->mfd.fd, KDBUS_CMD_MEMFD_SEAL_SET, 1);
	if (ret < 0) {
		fprintf(stderr, "kdbus: memfd sealing failed: %m\n");
		exit(1);
	}

	size += KDBUS_ITEM_SIZE(sizeof(struct kdbus_memfd));

	msg = malloc(size);
	if (!msg) {
		fprintf(stderr, "unable to malloc()!?\n");
		exit(1);
	}

	memset(msg, 0, size);
	msg->size = size;
	msg->src_id = k->c1->id;
	msg->dst_id = k->c2->id;
	msg->payload_type = KDBUS_PAYLOAD_DBUS;

	item = msg->items;

	item->type = KDBUS_ITEM_PAYLOAD_MEMFD;
	item->size = KDBUS_ITEM_HEADER_SIZE + sizeof(struct kdbus_memfd);
	item->memfd.size = sizeof(struct kdbus_cmd_memfd_make);
	item->memfd.fd = k->mfd.fd;

	printf("k->c1->fd %d mfd fd %d\n", k->c1->fd, k->mfd.fd);


	ret = ioctl(k->c1->fd, KDBUS_CMD_MSG_SEND, msg);
	if (ret) {
		fprintf(stderr, "error sending message: %d err %d (%m)\n", ret, errno);
		exit(1);
	}
	printf("sent\n");
	/* we've sent this message, munmap and free it */

	if (k->mfd.fd >= 0)
		close(k->mfd.fd);
	memset(&k->mfd, 0, sizeof(k->mfd));
	free(msg);

	ret = ioctl(k->c1->fd, KDBUS_CMD_MEMFD_NEW, &k->mfd);
	if (ret < 0) {
		fprintf(stderr, "KDBUS_CMD_MEMFD_NEW failed: %m\n");
		exit(1);
	}
//	w->buf = mmap(NULL, w->quanta, PROT_READ, MAP_SHARED, k->mfd.fd, 0);
//	if(w->buf == MAP_FAILED) {
//		perror("kdbus: couldn't allocate w->buf");
//		exit(1);
//	}



	return 0;
}



static Work*
kdbusgetwork(Work *w)
{
	Kdbus *k;
	int n;

	k = (Kdbus*)w->private;
	int ret;
	struct kdbus_cmd_recv recv = {};
	struct kdbus_msg *msg;
	const struct kdbus_item *item;

again:
	memset(&recv, 0, sizeof(recv));
	ret = ioctl(k->c2->fd, KDBUS_CMD_MSG_RECV, &recv);
//	printf("%d %d %d\n", recv.flags, recv.offset, recv.priority);
	if(ret < 0 && errno == EAGAIN) {
		goto again;
	}
	if (ret < 0) {
		fprintf(stderr, "error receiving message: %d %d (%m)\n", ret, errno);
		exit(1);
	}

	printf("received\n");

	msg = (struct kdbus_msg *)(w->buf + recv.offset);
	item = msg->items;

	KDBUS_ITEM_FOREACH(item, msg, items) {
		switch (item->type) {
		case KDBUS_ITEM_PAYLOAD_MEMFD: {
			char *buf;

			buf = mmap(NULL, item->memfd.size, PROT_READ, MAP_SHARED, item->memfd.fd, 0);
			if (buf == MAP_FAILED) {
				printf("mmap() fd=%i failed: %m", item->memfd.fd);
				break;
			}

			munmap(buf, item->memfd.size);
			close(item->memfd.fd);
			break;
		}

		case KDBUS_ITEM_PAYLOAD_OFF: {
			/* ignore */
			break;
		}
		}
	}

	ret = ioctl(k->c2->fd, KDBUS_CMD_FREE, &recv.offset);
	if (ret < 0) {
		fprintf(stderr, "error free message: %d (%m)\n", ret);
		exit(1);
	}
	return 0;
}

static void
kdbusfinalize()
{
	// who wakes the sleeper?
}

Bench kdbusbench = {
	.name = "kdbus",
	.init = kdbusinit,
	.sharework = kdbussharework,
	.getwork = kdbusgetwork,
	.spawn = kdbusspawn,
	.finalize = kdbusfinalize,

};
