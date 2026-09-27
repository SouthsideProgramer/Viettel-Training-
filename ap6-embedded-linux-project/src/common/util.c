#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "util.h"

ssize_t write_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	size_t done = 0;

	while (done < len) {
		ssize_t n = write(fd, p + done, len - done);

		if (n < 0) {
			if (errno == EINTR)
				continue;	/* bi signal ngat -> thu lai */
			return -1;
		}
		if (n == 0)
			break;
		done += (size_t)n;
	}
	return (ssize_t)done;
}

ssize_t read_all(int fd, void *buf, size_t len)
{
	char *p = buf;
	size_t done = 0;

	while (done < len) {
		ssize_t n = read(fd, p + done, len - done);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			break;		/* peer dong ket noi / EOF */
		done += (size_t)n;
	}
	return (ssize_t)done;
}

int set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);

	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int set_cloexec(int fd)
{
	int flags = fcntl(fd, F_GETFD, 0);

	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

int daemonize(void)
{
	pid_t pid;
	int fd;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid > 0)
		_exit(0);		/* process cha thoat -> con thanh orphan */

	if (setsid() < 0)		/* tach khoi controlling terminal */
		return -1;

	/* fork lan hai: process moi khong phai session leader nen khong the
	 * vo tinh mo lai terminal. */
	pid = fork();
	if (pid < 0)
		return -1;
	if (pid > 0)
		_exit(0);

	if (chdir("/") < 0)
		return -1;
	umask(0027);

	fd = open("/dev/null", O_RDWR);
	if (fd >= 0) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			close(fd);
	}
	return 0;
}

int pidfile_write(const char *path)
{
	char buf[32];
	int len, fd;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0)
		return -1;
	len = snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());
	if (write_all(fd, buf, (size_t)len) < 0) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

void pidfile_remove(const char *path)
{
	if (path)
		unlink(path);
}

int write_file_atomic(const char *path, const char *data, size_t len)
{
	char tmp[512];
	int fd;

	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0)
		return -1;

	if (write_all(fd, data, len) < 0)
		goto err;
	/* fsync truoc khi rename: du lieu phai nam tren flash, khong chi
	 * trong page cache (Bao cao muc 5.4). */
	if (fsync(fd) < 0)
		goto err;
	if (close(fd) < 0) {
		unlink(tmp);
		return -1;
	}
	if (rename(tmp, path) < 0) {
		unlink(tmp);
		return -1;
	}
	return 0;

err:
	close(fd);
	unlink(tmp);
	return -1;
}

double monotonic_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
