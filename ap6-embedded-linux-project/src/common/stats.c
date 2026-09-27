#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "stats.h"

struct ap6_stats *stats_create(const char *name)
{
	pthread_mutexattr_t attr;
	struct ap6_stats *st;
	int fd;

	shm_unlink(name);		/* don du lieu cu neu daemon crash truoc do */

	fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0660);
	if (fd < 0)
		return NULL;

	if (ftruncate(fd, sizeof(struct ap6_stats)) < 0) {
		close(fd);
		shm_unlink(name);
		return NULL;
	}

	st = mmap(NULL, sizeof(*st), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);			/* mapping van ton tai sau khi dong fd */
	if (st == MAP_FAILED) {
		shm_unlink(name);
		return NULL;
	}

	memset(st, 0, sizeof(*st));
	pthread_mutexattr_init(&attr);
	/* Bat buoc: mutex nam trong shm phai la PROCESS_SHARED, neu khong
	 * cac process khac se khoa nham tren ban sao cua rieng minh. */
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	/* ROBUST: neu process giu lock bi kill, process sau nhan EOWNERDEAD
	 * thay vi cho vo han (Bao cao muc 9.5). */
	pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
	pthread_mutex_init(&st->lock, &attr);
	pthread_mutexattr_destroy(&attr);

	st->magic = AP6_STATS_MAGIC;
	st->version = 1;
	return st;
}

struct ap6_stats *stats_open(const char *name)
{
	struct ap6_stats *st;
	int fd;

	fd = shm_open(name, O_RDWR, 0);
	if (fd < 0)
		return NULL;

	st = mmap(NULL, sizeof(*st), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (st == MAP_FAILED)
		return NULL;

	if (st->magic != AP6_STATS_MAGIC) {
		munmap(st, sizeof(*st));
		errno = EINVAL;
		return NULL;
	}
	return st;
}

void stats_close(struct ap6_stats *st)
{
	if (st)
		munmap(st, sizeof(*st));
}

void stats_unlink(const char *name)
{
	shm_unlink(name);
}
