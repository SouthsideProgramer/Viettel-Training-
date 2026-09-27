/* apd.h - trang thai dung chung cua daemon quan ly AP
 *
 * apd la phan tich hop cua bao cao: mot daemon Embedded Linux dien hinh
 *   - control path: Unix domain socket + TCP socket, epoll (muc 9.6, 13.3)
 *   - worker thread lay telemetry, dong bo bang mutex/condvar (muc 8.3, 8.4)
 *   - shared memory + mutex process-shared cho process quan sat (muc 9.4)
 *   - signal: SIGTERM/SIGINT thoat sach, SIGHUP reload, SIGCHLD reap (muc 10)
 *   - fork + exec + waitpid khi ap dung cau hinh (muc 7.3, 7.4)
 *   - ioctl xuong driver (muc 11.4, 16.2)
 */
#ifndef AP6_APD_H
#define AP6_APD_H

#include <pthread.h>
#include <signal.h>
#include <sys/types.h>

#include "../common/profile.h"
#include "../common/stats.h"
#include "hwctl.h"

#define APD_MAX_CLIENTS		16
#define APD_BUF_SIZE		4096

struct apd_client {
	int fd;
	char buf[APD_BUF_SIZE];
	size_t len;
	char peer[64];
};

struct apd_ctx {
	/* Cau hinh chay */
	char profile_path[256];
	char sock_path[256];
	char pid_path[256];
	char apply_script[256];
	int tcp_port;			/* 0 = khong mo TCP */
	int foreground;

	/* Du lieu dung chung giua main loop va telemetry thread */
	pthread_mutex_t lock;
	pthread_cond_t cond;		/* danh thuc thread khi can thoat */
	struct profile profile;
	int telemetry_stop;

	/* Tai nguyen he thong */
	struct hwctl *hw;
	struct ap6_stats *stats;
	pthread_t telemetry_tid;
	int telemetry_started;

	/* Socket / epoll */
	int epfd;
	int unix_fd;
	int tcp_fd;
	int sigpipe[2];			/* self-pipe cho signal handler */
	struct apd_client clients[APD_MAX_CLIENTS];
	int nclients;

	/* Thong ke vong doi */
	double start_time;
	unsigned long cmd_count;
	unsigned long conn_count;
	pid_t last_child;
	int last_child_status;
	int last_child_valid;
};

/* server.c */
int server_init(struct apd_ctx *ctx);
int server_run(struct apd_ctx *ctx);
void server_cleanup(struct apd_ctx *ctx);

/* telemetry.c */
int telemetry_start(struct apd_ctx *ctx);
void telemetry_stop(struct apd_ctx *ctx);

/* apd.c */
void apd_request_stop(void);
int apd_should_stop(void);
int apd_apply_profile(struct apd_ctx *ctx, char *reply, size_t replylen);
int apd_reload_profile(struct apd_ctx *ctx);

#endif /* AP6_APD_H */
