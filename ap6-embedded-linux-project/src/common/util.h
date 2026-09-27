/* util.h - cac helper dung chung: I/O an toan, daemonize, pid file */
#ifndef AP6_UTIL_H
#define AP6_UTIL_H

#include <stddef.h>
#include <sys/types.h>

/* Ghi du lieu cho den khi het buffer, xu ly ghi thieu (short write) va EINTR.
 * Bao cao muc 13.4: send/write co the tra ve it hon so byte yeu cau. */
ssize_t write_all(int fd, const void *buf, size_t len);

/* Doc du lieu day du len byte; tra ve so byte doc duoc (< len neu EOF). */
ssize_t read_all(int fd, void *buf, size_t len);

/* Dat fd sang non-blocking + close-on-exec (Bao cao muc 13.4, 7.4). */
int set_nonblock(int fd);
int set_cloexec(int fd);

/* Bien process hien tai thanh daemon (Bao cao muc 7.5):
 * fork -> setsid -> fork -> chdir("/") -> dong stdin/stdout/stderr. */
int daemonize(void);

/* PID file de service manager (systemd/procd) theo doi process. */
int pidfile_write(const char *path);
void pidfile_remove(const char *path);

/* Ghi file theo kieu nguyen tu: ghi tmp -> fsync -> rename.
 * Bao cao muc 5.4: tranh mat cau hinh khi mat dien tren thiet bi flash. */
int write_file_atomic(const char *path, const char *data, size_t len);

/* Thoi gian monotonic tinh bang giay (khong bi anh huong khi doi gio he thong). */
double monotonic_now(void);

#endif /* AP6_UTIL_H */
