/*
 * Copyright (C) 2013 Daniel Mack
 * Copyright (C) 2013 Kay Sievers
 *
 * kdbus is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "kdbus-util.h"
#include "kdbus-enum.h"

#define POOL_SIZE (16 * 1024LU * 1024LU)
static struct conn *
__kdbus_hello(const char *path, uint64_t flags,
	      const struct kdbus_item *item, size_t item_size)
{
	int fd, ret;
	struct {
		struct kdbus_cmd_hello hello;
		uint64_t size;
		uint64_t type;
		char comm[16];
		uint8_t extra_items[item_size];
	} h;
	struct conn *conn;

	memset(&h, 0, sizeof(h));

	if (item_size > 0)
		memcpy(h.extra_items, item, item_size);

	fd = open(path, O_RDWR|O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "--- error %d (%m)\n", fd);
		return NULL;
	}

	h.hello.conn_flags = flags | KDBUS_HELLO_ACCEPT_FD;

	h.hello.attach_flags = KDBUS_ATTACH_TIMESTAMP |
			       KDBUS_ATTACH_CREDS |
			       KDBUS_ATTACH_NAMES |
			       KDBUS_ATTACH_COMM |
			       KDBUS_ATTACH_EXE |
			       KDBUS_ATTACH_CMDLINE |
			       KDBUS_ATTACH_CAPS |
			       KDBUS_ATTACH_CGROUP |
			       KDBUS_ATTACH_SECLABEL |
			       KDBUS_ATTACH_AUDIT |
			       KDBUS_ATTACH_CONN_NAME;

	h.type = KDBUS_ITEM_CONN_NAME;
	h.size = KDBUS_ITEM_HEADER_SIZE + sizeof(h.comm);
	strcpy(h.comm, "this-is-my-name");

	h.hello.size = sizeof(h);
	h.hello.pool_size = POOL_SIZE;

	ret = ioctl(fd, KDBUS_CMD_HELLO, &h.hello);
	if (ret < 0) {
		fprintf(stderr, "--- error when saying hello: %d (%m)\n", ret);
		return NULL;
	}

	conn = malloc(sizeof(*conn));
	if (!conn) {
		fprintf(stderr, "unable to malloc()!?\n");
		return NULL;
	}

	conn->buf = mmap(NULL, POOL_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	if (conn->buf == MAP_FAILED) {
		free(conn);
		fprintf(stderr, "--- error mmap (%m)\n");
		return NULL;
	}

	conn->fd = fd;
	conn->id = h.hello.id;
	return conn;
}

struct conn *kdbus_hello(const char *path, uint64_t flags)
{
	return __kdbus_hello(path, flags, NULL, 0);
}

static struct conn *
kdbus_hello_registrar(const char *path, const char *name,
		      const struct kdbus_policy_access *access,
		      size_t num_access, uint64_t flags)
{
	struct kdbus_item *item, *items;
	size_t i, size;

	size = KDBUS_ITEM_SIZE(offsetof(struct kdbus_item, str) + strlen(name) + 1)
		+ num_access * KDBUS_ITEM_SIZE(offsetof(struct kdbus_item, policy_access) +
					       sizeof(struct kdbus_policy_access));

	items = alloca(size);

	item = items;
	item->size = offsetof(struct kdbus_item, str) + strlen(name) + 1;
	item->type = KDBUS_ITEM_NAME;
	strcpy(item->str, name);
	item = KDBUS_ITEM_NEXT(item);

	for (i = 0; i < num_access; i++) {
		item->size = offsetof(struct kdbus_item, policy_access) +
			     sizeof(struct kdbus_policy_access);
		item->type = KDBUS_ITEM_POLICY_ACCESS;

		item->policy_access.type = access[i].type;
		item->policy_access.access = access[i].access;
		item->policy_access.id = access[i].id;

		item = KDBUS_ITEM_NEXT(item);
	}

	return __kdbus_hello(path, flags, items, size);
}

struct conn *kdbus_hello_activator(const char *path, const char *name,
				   const struct kdbus_policy_access *access,
				   size_t num_access)
{
	return kdbus_hello_registrar(path, name, access, num_access,
				     KDBUS_HELLO_ACTIVATOR);
}

int msg_send(const struct conn *conn,
	     const char *name,
	     uint64_t cookie,
	     uint64_t flags,
	     uint64_t timeout,
	     int64_t priority,
	     uint64_t dst_id)
{
	struct kdbus_msg *msg;
	const char ref1[1024 * 1024 + 3] = "0123456789_0";
	const char ref2[] = "0123456789_1";
	struct kdbus_item *item;
	uint64_t size;
	int memfd = -1;
	int ret;

	size = sizeof(struct kdbus_msg);
	size += KDBUS_ITEM_SIZE(sizeof(struct kdbus_vec));
	size += KDBUS_ITEM_SIZE(sizeof(struct kdbus_vec));
	size += KDBUS_ITEM_SIZE(sizeof(struct kdbus_vec));

	if (dst_id == KDBUS_DST_ID_BROADCAST)
		size += KDBUS_ITEM_SIZE(sizeof(struct kdbus_bloom_filter)) + 64;
	else {
		struct {
			struct kdbus_cmd_memfd_make cmd;
			uint64_t size;
			uint64_t type;
			char name[16];
		} m = {};

		m.cmd.size = sizeof(m);
		m.cmd.file_size = 1024 * 1024;
		m.cmd.items[0].type = KDBUS_ITEM_MEMFD_NAME;
		m.cmd.items[0].size = KDBUS_ITEM_HEADER_SIZE + sizeof(m.name);
		strcpy(m.name, "my-name-is-nice");
		ret = ioctl(conn->fd, KDBUS_CMD_MEMFD_NEW, &m);
		if (ret < 0) {
			fprintf(stderr, "KDBUS_CMD_MEMFD_NEW failed: %m\n");
			return EXIT_FAILURE;
		}
		memfd = m.cmd.fd;

		if (write(memfd, "kdbus memfd 1234567", 19) != 19) {
			fprintf(stderr, "writing to memfd failed: %m\n");
			return EXIT_FAILURE;
		}

		ret = ioctl(memfd, KDBUS_CMD_MEMFD_SEAL_SET, true);
		if (ret < 0) {
			fprintf(stderr, "memfd sealing failed: %m\n");
			return EXIT_FAILURE;
		}

		size += KDBUS_ITEM_SIZE(sizeof(struct kdbus_memfd));
	}

	if (name)
		size += KDBUS_ITEM_SIZE(strlen(name) + 1);

	msg = malloc(size);
	if (!msg) {
		fprintf(stderr, "unable to malloc()!?\n");
		return EXIT_FAILURE;
	}

	memset(msg, 0, size);
	msg->flags = flags;
	msg->timeout_ns = timeout;
	msg->priority = priority;
	msg->size = size;
	msg->src_id = conn->id;
	msg->dst_id = name ? 0 : dst_id;
	msg->cookie = cookie;
	msg->payload_type = KDBUS_PAYLOAD_DBUS;

	item = msg->items;

	if (name) {
		item->type = KDBUS_ITEM_DST_NAME;
		item->size = KDBUS_ITEM_HEADER_SIZE + strlen(name) + 1;
		strcpy(item->str, name);
		item = KDBUS_ITEM_NEXT(item);
	}

	item->type = KDBUS_ITEM_PAYLOAD_VEC;
	item->size = KDBUS_ITEM_HEADER_SIZE + sizeof(struct kdbus_vec);
	item->vec.address = (uintptr_t)&ref1;
	item->vec.size = sizeof(ref1);
	item = KDBUS_ITEM_NEXT(item);

	/* data padding for ref1 */
	item->type = KDBUS_ITEM_PAYLOAD_VEC;
	item->size = KDBUS_ITEM_HEADER_SIZE + sizeof(struct kdbus_vec);
	item->vec.address = (uintptr_t)NULL;
	item->vec.size =  KDBUS_ALIGN8(sizeof(ref1)) - sizeof(ref1);
	item = KDBUS_ITEM_NEXT(item);

	item->type = KDBUS_ITEM_PAYLOAD_VEC;
	item->size = KDBUS_ITEM_HEADER_SIZE + sizeof(struct kdbus_vec);
	item->vec.address = (uintptr_t)&ref2;
	item->vec.size = sizeof(ref2);
	item = KDBUS_ITEM_NEXT(item);

	if (dst_id == KDBUS_DST_ID_BROADCAST) {
		item->type = KDBUS_ITEM_BLOOM_FILTER;
		item->size = KDBUS_ITEM_SIZE(sizeof(struct kdbus_bloom_filter)) + 64;
		item->bloom_filter.generation = 0;
	} else {
		item->type = KDBUS_ITEM_PAYLOAD_MEMFD;
		item->size = KDBUS_ITEM_HEADER_SIZE + sizeof(struct kdbus_memfd);
		item->memfd.size = 16;
		item->memfd.fd = memfd;
	}
	item = KDBUS_ITEM_NEXT(item);

	ret = ioctl(conn->fd, KDBUS_CMD_MSG_SEND, msg);
	if (ret < 0) {
		fprintf(stderr, "error sending message: %d err %d (%m)\n", ret, errno);
		return EXIT_FAILURE;
	}

	if (memfd >= 0)
		close(memfd);

	if (flags & KDBUS_MSG_FLAGS_SYNC_REPLY) {
		struct kdbus_msg *reply;

		printf("SYNC REPLY @offset %llu:\n", msg->offset_reply);
		reply = (struct kdbus_msg *)(conn->buf + msg->offset_reply);
		msg_dump(conn, reply);

		ret = ioctl(conn->fd, KDBUS_CMD_FREE, &msg->offset_reply);
		if (ret < 0) {
			ret = -errno;
			fprintf(stderr, "error free message: %d (%m)\n", ret);
			return ret;
		}
	}

	free(msg);

	return 0;
}

char *msg_id(uint64_t id, char *buf)
{
	if (id == 0)
		return "KERNEL";
	if (id == ~0ULL)
		return "BROADCAST";
	sprintf(buf, "%llu", (unsigned long long)id);
	return buf;
}

void msg_dump(const struct conn *conn, const struct kdbus_msg *msg)
{
	const struct kdbus_item *item = msg->items;
	char buf_src[32];
	char buf_dst[32];
	uint64_t timeout = 0;
	uint64_t cookie_reply = 0;

	if (msg->flags & KDBUS_MSG_FLAGS_EXPECT_REPLY)
		timeout = msg->timeout_ns;
	else
		cookie_reply = msg->cookie_reply;

	printf("MESSAGE: %s (%llu bytes) flags=0x%08llx, %s → %s, cookie=%llu, timeout=%llu cookie_reply=%llu priority=%lli\n",
		enum_PAYLOAD(msg->payload_type), (unsigned long long)msg->size,
		(unsigned long long)msg->flags,
		msg_id(msg->src_id, buf_src), msg_id(msg->dst_id, buf_dst),
		(unsigned long long)msg->cookie, (unsigned long long)timeout, (unsigned long long)cookie_reply,
		(long long)msg->priority);

	KDBUS_ITEM_FOREACH(item, msg, items) {
		if (item->size < KDBUS_ITEM_HEADER_SIZE) {
			printf("  +%s (%llu bytes) invalid data record\n", enum_MSG(item->type), item->size);
			break;
		}

		switch (item->type) {
		case KDBUS_ITEM_PAYLOAD_OFF: {
			char *s;

			if (item->vec.offset == ~0ULL)
				s = "[\\0-bytes]";
			else
				s = (char *)msg + item->vec.offset;

			printf("  +%s (%llu bytes) off=%llu size=%llu '%s'\n",
			       enum_MSG(item->type), item->size,
			       (unsigned long long)item->vec.offset,
			       (unsigned long long)item->vec.size, s);
			break;
		}

		case KDBUS_ITEM_PAYLOAD_MEMFD: {
			char *buf;
			uint64_t size;

			buf = mmap(NULL, item->memfd.size, PROT_READ, MAP_SHARED, item->memfd.fd, 0);
			if (buf == MAP_FAILED) {
				printf("mmap() fd=%i failed:%m", item->memfd.fd);
				break;
			}

			if (ioctl(item->memfd.fd, KDBUS_CMD_MEMFD_SIZE_GET, &size) < 0) {
				fprintf(stderr, "KDBUS_CMD_MEMFD_SIZE_GET failed: %m\n");
				break;
			}

			printf("  +%s (%llu bytes) fd=%i size=%llu filesize=%llu '%s'\n",
			       enum_MSG(item->type), item->size, item->memfd.fd,
			       (unsigned long long)item->memfd.size, (unsigned long long)size, buf);
			break;
		}

		case KDBUS_ITEM_CREDS:
			printf("  +%s (%llu bytes) uid=%lld, gid=%lld, pid=%lld, tid=%lld, starttime=%lld\n",
				enum_MSG(item->type), item->size,
				item->creds.uid, item->creds.gid,
				item->creds.pid, item->creds.tid,
				item->creds.starttime);
			break;

		case KDBUS_ITEM_PID_COMM:
		case KDBUS_ITEM_TID_COMM:
		case KDBUS_ITEM_EXE:
		case KDBUS_ITEM_CGROUP:
		case KDBUS_ITEM_SECLABEL:
		case KDBUS_ITEM_DST_NAME:
		case KDBUS_ITEM_CONN_NAME:
			printf("  +%s (%llu bytes) '%s' (%zu)\n",
			       enum_MSG(item->type), item->size, item->str, strlen(item->str));
			break;

		case KDBUS_ITEM_NAME: {
			printf("  +%s (%llu bytes) '%s' (%zu) flags=0x%08llx\n",
			       enum_MSG(item->type), item->size, item->name.name, strlen(item->name.name),
			       item->name.flags);
			break;
		}

		case KDBUS_ITEM_CMDLINE: {
			size_t size = item->size - KDBUS_ITEM_HEADER_SIZE;
			const char *str = item->str;
			int count = 0;

			printf("  +%s (%llu bytes) ", enum_MSG(item->type), item->size);
			while (size) {
				printf("'%s' ", str);
				size -= strlen(str) + 1;
				str += strlen(str) + 1;
				count++;
			}

			printf("(%d string%s)\n", count, (count == 1) ? "" : "s");
			break;
		}

		case KDBUS_ITEM_AUDIT:
			printf("  +%s (%llu bytes) loginuid=%llu sessionid=%llu\n",
			       enum_MSG(item->type), item->size,
			       (unsigned long long)item->data64[0],
			       (unsigned long long)item->data64[1]);
			break;

		case KDBUS_ITEM_CAPS: {
			int n;
			const uint32_t *cap;
			int i;

			printf("  +%s (%llu bytes) len=%llu bytes\n",
			       enum_MSG(item->type), item->size,
			       (unsigned long long)item->size - KDBUS_ITEM_HEADER_SIZE);

			cap = item->data32;
			n = (item->size - KDBUS_ITEM_HEADER_SIZE) / 4 / sizeof(uint32_t);

			printf("    CapInh=");
			for (i = 0; i < n; i++)
				printf("%08x", cap[(0 * n) + (n - i - 1)]);

			printf(" CapPrm=");
			for (i = 0; i < n; i++)
				printf("%08x", cap[(1 * n) + (n - i - 1)]);

			printf(" CapEff=");
			for (i = 0; i < n; i++)
				printf("%08x", cap[(2 * n) + (n - i - 1)]);

			printf(" CapInh=");
			for (i = 0; i < n; i++)
				printf("%08x", cap[(3 * n) + (n - i - 1)]);
			printf("\n");
			break;
		}

		case KDBUS_ITEM_TIMESTAMP:
			printf("  +%s (%llu bytes) seq=%llu realtime=%lluns monotonic=%lluns\n",
			       enum_MSG(item->type), item->size,
			       (unsigned long long)item->timestamp.seqnum,
			       (unsigned long long)item->timestamp.realtime_ns,
			       (unsigned long long)item->timestamp.monotonic_ns);
			break;

		case KDBUS_ITEM_REPLY_TIMEOUT:
			printf("  +%s (%llu bytes) cookie=%llu\n",
			       enum_MSG(item->type), item->size, msg->cookie_reply);
			break;

		case KDBUS_ITEM_NAME_ADD:
		case KDBUS_ITEM_NAME_REMOVE:
		case KDBUS_ITEM_NAME_CHANGE:
			printf("  +%s (%llu bytes) '%s', old id=%lld, new id=%lld, old_flags=0x%llx new_flags=0x%llx\n",
				enum_MSG(item->type), (unsigned long long) item->size,
				item->name_change.name, item->name_change.old.id,
				item->name_change.new.id, item->name_change.old.flags,
				item->name_change.new.flags);
			break;

		case KDBUS_ITEM_ID_ADD:
		case KDBUS_ITEM_ID_REMOVE:
			printf("  +%s (%llu bytes) id=%llu flags=%llu\n",
			       enum_MSG(item->type), (unsigned long long) item->size,
			       (unsigned long long) item->id_change.id,
			       (unsigned long long) item->id_change.flags);
			break;

		default:
			printf("  +%s (%llu bytes)\n", enum_MSG(item->type), item->size);
			break;
		}
	}

	if ((char *)item - ((char *)msg + msg->size) >= 8)
		printf("invalid padding at end of message\n");

	printf("\n");
}

int msg_recv(struct conn *conn)
{
	struct kdbus_cmd_recv recv = {};
	struct kdbus_msg *msg;
	int ret;

	ret = ioctl(conn->fd, KDBUS_CMD_MSG_RECV, &recv);
	if (ret < 0) {
		ret = -errno;
		fprintf(stderr, "error receiving message: %d (%m)\n", ret);
		return ret;
	}

	msg = (struct kdbus_msg *)(conn->buf + recv.offset);
	msg_dump(conn, msg);

	ret = ioctl(conn->fd, KDBUS_CMD_FREE, &recv.offset);
	if (ret < 0) {
		ret = -errno;
		fprintf(stderr, "error free message: %d (%m)\n", ret);
		return ret;
	}

	return 0;
}

int name_acquire(struct conn *conn, const char *name, uint64_t flags)
{
	struct kdbus_cmd_name *cmd_name;
	int ret;
	uint64_t size = sizeof(*cmd_name) + strlen(name) + 1;

	cmd_name = alloca(size);

	memset(cmd_name, 0, size);
	strcpy(cmd_name->name, name);
	cmd_name->size = size;
	cmd_name->flags = flags;

	ret = ioctl(conn->fd, KDBUS_CMD_NAME_ACQUIRE, cmd_name);
	if (ret < 0) {
		ret = -errno;
		fprintf(stderr, "error aquiring name: %s\n", strerror(-ret));
		return ret;
	}

	printf("%s(): flags after call: 0x%llx\n", __func__, cmd_name->conn_flags);

	return 0;
}

int name_release(struct conn *conn, const char *name)
{
	struct kdbus_cmd_name *cmd_name;
	int ret;
	uint64_t size = sizeof(*cmd_name) + strlen(name) + 1;

	cmd_name = alloca(size);

	memset(cmd_name, 0, size);
	strcpy(cmd_name->name, name);
	cmd_name->size = size;

	printf("conn %lld giving up name '%s'\n", (unsigned long long)conn->id, name);

	ret = ioctl(conn->fd, KDBUS_CMD_NAME_RELEASE, cmd_name);
	if (ret < 0) {
		ret = -errno;
		fprintf(stderr, "error releasing name: %s\n", strerror(-ret));
		return ret;
	}

	return 0;
}

int name_list(struct conn *conn, uint64_t flags)
{
	struct kdbus_cmd_name_list cmd_list;
	struct kdbus_name_list *list;
	struct kdbus_cmd_name *name;
	int ret;

	cmd_list.flags = flags;

	ret = ioctl(conn->fd, KDBUS_CMD_NAME_LIST, &cmd_list);
	if (ret < 0) {
		fprintf(stderr, "error listing names: %d (%m)\n", ret);
		return EXIT_FAILURE;
	}

	printf("REGISTRY:\n");
	list = (struct kdbus_name_list *)(conn->buf + cmd_list.offset);
	KDBUS_ITEM_FOREACH(name, list, names)
		printf("%8llu flags=0x%08llx conn=0x%08llx '%s'\n", name->owner_id,
		       name->flags, name->conn_flags,
		       name->size > sizeof(struct kdbus_cmd_name) ? name->name : "");
	printf("\n");

	ret = ioctl(conn->fd, KDBUS_CMD_FREE, &cmd_list.offset);
	if (ret < 0) {
		fprintf(stderr, "error free name list: %d (%m)\n", ret);
		return EXIT_FAILURE;
	}

	return 0;
}

void add_match_empty(int fd)
{
	struct {
		struct kdbus_cmd_match cmd;
		struct kdbus_item item;
	} buf;
	int ret;

	memset(&buf, 0, sizeof(buf));

	buf.item.size = sizeof(uint64_t) * 3;
	buf.item.type = KDBUS_ITEM_ID;
	buf.item.id = KDBUS_MATCH_ID_ANY;

	buf.cmd.size = sizeof(buf.cmd) + buf.item.size;

	ret = ioctl(fd, KDBUS_CMD_MATCH_ADD, &buf);
	if (ret < 0)
		fprintf(stderr, "--- error adding conn match: %d (%m)\n", ret);
}
