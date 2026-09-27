/*
 * server.c - control path cua daemon: Unix domain socket + TCP socket, epoll.
 *
 * Bao cao muc 9.6: Unix domain socket dung cho IPC noi bo (web UI, CLI),
 * TCP socket dung khi can quan ly tu xa.
 * Bao cao muc 13.3/13.4: socket -> bind -> listen -> accept, non-blocking I/O,
 * kiem tra gia tri tra ve, dong fd dung luc de khong can kiet file descriptor.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include "apd.h"
#include "../common/log.h"
#include "../common/util.h"

#define MAX_EVENTS	16
#define EPOLL_TIMEOUT	1000	/* ms */

static const char *HELP_TEXT =
	"Cac lenh ho tro:\n"
	"  HELP                 - danh sach lenh\n"
	"  SHOW                 - in toan bo profile\n"
	"  GET <Key>            - doc mot tham so\n"
	"  SET <Key> <Value>    - dat tham so (co validate)\n"
	"  SAVE                 - ghi profile xuong flash (atomic)\n"
	"  RELOAD               - nap lai profile tu file\n"
	"  APPLY                - ioctl xuong driver + fork/exec script\n"
	"  RADIO                - trang thai radio doc tu driver\n"
	"  STATS                - telemetry tu shared memory\n"
	"  STATUS               - trang thai daemon\n"
	"  QUIT                 - dong ket noi\n";

/* -------------------------------------------------------------- */
/* Tao listening socket						  */
/* -------------------------------------------------------------- */

static int mkdir_for(const char *path)
{
	char tmp[256];
	char *dir;

	snprintf(tmp, sizeof(tmp), "%s", path);
	dir = dirname(tmp);
	if (mkdir(dir, 0755) < 0 && errno != EEXIST)
		return -1;
	return 0;
}

static int make_unix_socket(const char *path)
{
	struct sockaddr_un addr;
	int fd;

	if (strlen(path) >= sizeof(addr.sun_path)) {
		log_err("duong dan socket qua dai: %s", path);
		return -1;
	}
	if (mkdir_for(path) < 0)
		log_warn("khong tao duoc thu muc cho %s: %s", path,
			 strerror(errno));

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		log_err("socket(AF_UNIX) that bai: %s", strerror(errno));
		return -1;
	}

	/* Xoa socket cu neu daemon truoc do khong thoat sach. */
	unlink(path);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		log_err("bind(%s) that bai: %s", path, strerror(errno));
		close(fd);
		return -1;
	}
	if (chmod(path, 0660) < 0)
		log_warn("chmod(%s) that bai: %s", path, strerror(errno));

	if (listen(fd, 8) < 0) {
		log_err("listen(%s) that bai: %s", path, strerror(errno));
		close(fd);
		unlink(path);
		return -1;
	}
	set_nonblock(fd);
	return fd;
}

static int make_tcp_socket(int port)
{
	struct sockaddr_in addr;
	int fd, on = 1;

	fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		log_err("socket(AF_INET) that bai: %s", strerror(errno));
		return -1;
	}
	/* SO_REUSEADDR: cho phep bind lai ngay sau khi restart service. */
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((uint16_t)port);	/* host -> network byte order */

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		log_err("bind(tcp:%d) that bai: %s", port, strerror(errno));
		close(fd);
		return -1;
	}
	if (listen(fd, 8) < 0) {
		log_err("listen(tcp:%d) that bai: %s", port, strerror(errno));
		close(fd);
		return -1;
	}
	set_nonblock(fd);
	return fd;
}

static int epoll_add(int epfd, int fd, uint32_t events)
{
	struct epoll_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.events = events;
	ev.data.fd = fd;
	return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

int server_init(struct apd_ctx *ctx)
{
	int i;

	for (i = 0; i < APD_MAX_CLIENTS; i++)
		ctx->clients[i].fd = -1;

	if (pipe2(ctx->sigpipe, O_NONBLOCK | O_CLOEXEC) < 0) {
		log_err("pipe2 that bai: %s", strerror(errno));
		return -1;
	}

	ctx->epfd = epoll_create1(EPOLL_CLOEXEC);
	if (ctx->epfd < 0) {
		log_err("epoll_create1 that bai: %s", strerror(errno));
		return -1;
	}

	ctx->unix_fd = make_unix_socket(ctx->sock_path);
	if (ctx->unix_fd < 0)
		return -1;

	if (ctx->tcp_port > 0) {
		ctx->tcp_fd = make_tcp_socket(ctx->tcp_port);
		if (ctx->tcp_fd < 0)
			return -1;
	}

	if (epoll_add(ctx->epfd, ctx->unix_fd, EPOLLIN) < 0 ||
	    epoll_add(ctx->epfd, ctx->sigpipe[0], EPOLLIN) < 0) {
		log_err("epoll_ctl that bai: %s", strerror(errno));
		return -1;
	}
	if (ctx->tcp_fd >= 0 && epoll_add(ctx->epfd, ctx->tcp_fd, EPOLLIN) < 0) {
		log_err("epoll_ctl(tcp) that bai: %s", strerror(errno));
		return -1;
	}
	return 0;
}

/* -------------------------------------------------------------- */
/* Quan ly client						  */
/* -------------------------------------------------------------- */

static struct apd_client *client_alloc(struct apd_ctx *ctx, int fd,
				       const char *peer)
{
	int i;

	for (i = 0; i < APD_MAX_CLIENTS; i++) {
		if (ctx->clients[i].fd < 0) {
			ctx->clients[i].fd = fd;
			ctx->clients[i].len = 0;
			snprintf(ctx->clients[i].peer,
				 sizeof(ctx->clients[i].peer), "%s", peer);
			ctx->nclients++;
			ctx->conn_count++;
			return &ctx->clients[i];
		}
	}
	return NULL;
}

static struct apd_client *client_find(struct apd_ctx *ctx, int fd)
{
	int i;

	for (i = 0; i < APD_MAX_CLIENTS; i++) {
		if (ctx->clients[i].fd == fd)
			return &ctx->clients[i];
	}
	return NULL;
}

static void client_close(struct apd_ctx *ctx, struct apd_client *cl)
{
	if (!cl || cl->fd < 0)
		return;
	epoll_ctl(ctx->epfd, EPOLL_CTL_DEL, cl->fd, NULL);
	close(cl->fd);
	log_dbg("dong ket noi %s (fd=%d)", cl->peer, cl->fd);
	cl->fd = -1;
	cl->len = 0;
	ctx->nclients--;
}

static void do_accept(struct apd_ctx *ctx, int listen_fd, int is_tcp)
{
	struct sockaddr_storage ss;
	socklen_t slen = sizeof(ss);
	char peer[64];
	int fd;

	fd = accept4(listen_fd, (struct sockaddr *)&ss, &slen,
		     SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (fd < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
			log_warn("accept that bai: %s", strerror(errno));
		return;
	}

	if (is_tcp) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
		char ip[INET_ADDRSTRLEN];

		inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
		snprintf(peer, sizeof(peer), "tcp:%s:%d", ip,
			 ntohs(sin->sin_port));
	} else {
		snprintf(peer, sizeof(peer), "unix:fd%d", fd);
	}

	if (ctx->nclients >= APD_MAX_CLIENTS) {
		const char *msg = "ERR qua nhieu ket noi\n";

		log_warn("tu choi %s: da dat gioi han %d client", peer,
			 APD_MAX_CLIENTS);
		write_all(fd, msg, strlen(msg));
		close(fd);
		return;
	}

	if (!client_alloc(ctx, fd, peer) ||
	    epoll_add(ctx->epfd, fd, EPOLLIN) < 0) {
		log_warn("khong nhan duoc client %s", peer);
		close(fd);
		return;
	}
	log_info("client moi: %s (tong %d)", peer, ctx->nclients);
}

/* -------------------------------------------------------------- */
/* Xu ly lenh							  */
/* -------------------------------------------------------------- */

static void cmd_show(struct apd_ctx *ctx, char *out, size_t outlen)
{
	pthread_mutex_lock(&ctx->lock);
	profile_dump(&ctx->profile, out, outlen);
	pthread_mutex_unlock(&ctx->lock);
}

static void cmd_status(struct apd_ctx *ctx, char *out, size_t outlen)
{
	double up = monotonic_now() - ctx->start_time;
	int dirty;

	pthread_mutex_lock(&ctx->lock);
	dirty = ctx->profile.dirty;
	pthread_mutex_unlock(&ctx->lock);

	snprintf(out, outlen,
		 "pid=%ld\n"
		 "uptime=%.0fs\n"
		 "backend=%s\n"
		 "profile=%s\n"
		 "profile_dirty=%d\n"
		 "clients=%d\n"
		 "connections_total=%lu\n"
		 "commands_total=%lu\n"
		 "last_child_pid=%ld\n",
		 (long)getpid(), up, hwctl_backend(ctx->hw), ctx->profile_path,
		 dirty, ctx->nclients, ctx->conn_count, ctx->cmd_count,
		 ctx->last_child_valid ? (long)ctx->last_child : -1L);
}

static void cmd_stats(struct apd_ctx *ctx, char *out, size_t outlen)
{
	struct ap6_stats snap;

	if (!ctx->stats) {
		snprintf(out, outlen, "ERR telemetry chua san sang\n");
		return;
	}

	/* Chi giu lock du lau de copy snapshot (Bao cao muc 8.3). */
	pthread_mutex_lock(&ctx->stats->lock);
	snap = *ctx->stats;
	pthread_mutex_unlock(&ctx->stats->lock);

	snprintf(out, outlen,
		 "seq=%llu\nuptime=%llus\nssid=%s\nchannel=%u\ntxpower=%u\n"
		 "clients=%u\nnoise_dbm=%d\ntx_bytes=%llu\nrx_bytes=%llu\n"
		 "tx_errors=%llu\nrx_errors=%llu\n",
		 (unsigned long long)snap.seq,
		 (unsigned long long)snap.uptime_s, snap.ssid, snap.channel,
		 snap.txpower, snap.clients, snap.noise_dbm,
		 (unsigned long long)snap.tx_bytes,
		 (unsigned long long)snap.rx_bytes,
		 (unsigned long long)snap.tx_errors,
		 (unsigned long long)snap.rx_errors);
}

static void cmd_radio(struct apd_ctx *ctx, char *out, size_t outlen)
{
	struct hw_radio r;
	struct hw_counters c;

	if (hwctl_get_radio(ctx->hw, &r) < 0) {
		snprintf(out, outlen, "ERR doc trang thai radio that bai\n");
		return;
	}
	if (hwctl_get_counters(ctx->hw, &c) < 0)
		memset(&c, 0, sizeof(c));

	snprintf(out, outlen,
		 "backend=%s\nssid=%s\nchannel=%u\ntxpower=%u\nenabled=%u\n"
		 "clients=%u\nirq_count=%llu\ntx_packets=%llu\n"
		 "rx_packets=%llu\n",
		 hwctl_backend(ctx->hw), r.ssid, r.channel, r.txpower,
		 r.enabled, r.clients, (unsigned long long)c.irq_count,
		 (unsigned long long)c.tx_packets,
		 (unsigned long long)c.rx_packets);
}

/* Tra ve 1 neu client yeu cau dong ket noi (QUIT). */
static int handle_command(struct apd_ctx *ctx, struct apd_client *cl,
			  char *line)
{
	char out[APD_BUF_SIZE];
	char *cmd, *arg1, *arg2, *save;

	out[0] = '\0';
	ctx->cmd_count++;

	cmd = strtok_r(line, " \t", &save);
	if (!cmd)
		return 0;
	arg1 = strtok_r(NULL, " \t", &save);
	arg2 = strtok_r(NULL, "", &save);	/* phan con lai (SSID co space) */
	if (arg2) {
		while (*arg2 == ' ' || *arg2 == '\t')
			arg2++;
	}

	log_dbg("[%s] lenh: %s", cl->peer, cmd);

	if (strcasecmp(cmd, "HELP") == 0) {
		snprintf(out, sizeof(out), "%s", HELP_TEXT);
	} else if (strcasecmp(cmd, "SHOW") == 0) {
		cmd_show(ctx, out, sizeof(out));
	} else if (strcasecmp(cmd, "GET") == 0) {
		const char *v;

		if (!arg1) {
			snprintf(out, sizeof(out), "ERR thieu ten tham so\n");
		} else {
			pthread_mutex_lock(&ctx->lock);
			v = profile_get(&ctx->profile, arg1);
			snprintf(out, sizeof(out), "%s\n",
				 v ? v : "ERR khong co tham so nay");
			pthread_mutex_unlock(&ctx->lock);
		}
	} else if (strcasecmp(cmd, "SET") == 0) {
		char err[128];
		int ret;

		if (!arg1 || !arg2) {
			snprintf(out, sizeof(out),
				 "ERR cu phap: SET <Key> <Value>\n");
		} else {
			pthread_mutex_lock(&ctx->lock);
			ret = profile_set(&ctx->profile, arg1, arg2, err,
					  sizeof(err));
			pthread_mutex_unlock(&ctx->lock);
			if (ret < 0)
				snprintf(out, sizeof(out), "ERR %s\n", err);
			else
				snprintf(out, sizeof(out), "OK %s=%s\n", arg1,
					 arg2);
		}
	} else if (strcasecmp(cmd, "SAVE") == 0) {
		int ret;

		pthread_mutex_lock(&ctx->lock);
		ret = profile_save(&ctx->profile, ctx->profile_path);
		pthread_mutex_unlock(&ctx->lock);
		snprintf(out, sizeof(out), ret == 0 ? "OK da ghi %s\n" :
			 "ERR khong ghi duoc %s\n", ctx->profile_path);
	} else if (strcasecmp(cmd, "RELOAD") == 0) {
		snprintf(out, sizeof(out),
			 apd_reload_profile(ctx) == 0 ?
			 "OK da nap lai profile\n" :
			 "ERR khong doc duoc profile\n");
	} else if (strcasecmp(cmd, "APPLY") == 0) {
		char reply[256];

		apd_apply_profile(ctx, reply, sizeof(reply));
		snprintf(out, sizeof(out), "%s\n", reply);
	} else if (strcasecmp(cmd, "RADIO") == 0) {
		cmd_radio(ctx, out, sizeof(out));
	} else if (strcasecmp(cmd, "STATS") == 0) {
		cmd_stats(ctx, out, sizeof(out));
	} else if (strcasecmp(cmd, "STATUS") == 0) {
		cmd_status(ctx, out, sizeof(out));
	} else if (strcasecmp(cmd, "QUIT") == 0) {
		write_all(cl->fd, "OK bye\n", 7);
		return 1;
	} else {
		snprintf(out, sizeof(out),
			 "ERR lenh khong hop le '%s' (dung HELP)\n", cmd);
	}

	if (out[0] != '\0' && write_all(cl->fd, out, strlen(out)) < 0) {
		log_warn("ghi toi %s that bai: %s", cl->peer, strerror(errno));
		return 1;
	}
	return 0;
}

/* Doc du lieu tu client, tach theo dong '\n' va xu ly tung lenh. */
static void client_read(struct apd_ctx *ctx, struct apd_client *cl)
{
	ssize_t n;

	n = read(cl->fd, cl->buf + cl->len, sizeof(cl->buf) - cl->len - 1);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return;
		log_warn("read tu %s loi: %s", cl->peer, strerror(errno));
		client_close(ctx, cl);
		return;
	}
	if (n == 0) {			/* peer dong ket noi */
		client_close(ctx, cl);
		return;
	}

	cl->len += (size_t)n;
	cl->buf[cl->len] = '\0';

	for (;;) {
		char *nl = memchr(cl->buf, '\n', cl->len);
		size_t linelen;
		char line[APD_BUF_SIZE];

		if (!nl) {
			/* Dong qua dai ma khong co '\n' -> bo buffer. */
			if (cl->len >= sizeof(cl->buf) - 1) {
				const char *msg = "ERR dong lenh qua dai\n";

				write_all(cl->fd, msg, strlen(msg));
				cl->len = 0;
			}
			break;
		}

		linelen = (size_t)(nl - cl->buf);
		memcpy(line, cl->buf, linelen);
		line[linelen] = '\0';
		if (linelen > 0 && line[linelen - 1] == '\r')
			line[linelen - 1] = '\0';

		/* Don phan con lai ve dau buffer. */
		memmove(cl->buf, nl + 1, cl->len - linelen - 1);
		cl->len -= linelen + 1;

		if (handle_command(ctx, cl, line)) {
			client_close(ctx, cl);
			return;
		}
	}
}

/* -------------------------------------------------------------- */
/* Vong lap chinh						  */
/* -------------------------------------------------------------- */

int server_run(struct apd_ctx *ctx)
{
	struct epoll_event evs[MAX_EVENTS];
	int signal_event = 0;
	int n, i;

	n = epoll_wait(ctx->epfd, evs, MAX_EVENTS, EPOLL_TIMEOUT);
	if (n < 0) {
		if (errno == EINTR)
			return 1;	/* signal -> kiem tra co */
		log_err("epoll_wait loi: %s", strerror(errno));
		return -1;
	}

	for (i = 0; i < n; i++) {
		int fd = evs[i].data.fd;

		if (fd == ctx->sigpipe[0]) {
			signal_event = 1;
		} else if (fd == ctx->unix_fd) {
			do_accept(ctx, fd, 0);
		} else if (ctx->tcp_fd >= 0 && fd == ctx->tcp_fd) {
			do_accept(ctx, fd, 1);
		} else {
			struct apd_client *cl = client_find(ctx, fd);

			if (!cl) {
				epoll_ctl(ctx->epfd, EPOLL_CTL_DEL, fd, NULL);
				close(fd);
				continue;
			}
			if (evs[i].events & (EPOLLHUP | EPOLLERR))
				client_close(ctx, cl);
			else if (evs[i].events & EPOLLIN)
				client_read(ctx, cl);
		}
	}
	return signal_event;
}

void server_cleanup(struct apd_ctx *ctx)
{
	int i;

	for (i = 0; i < APD_MAX_CLIENTS; i++) {
		if (ctx->clients[i].fd > 0)
			client_close(ctx, &ctx->clients[i]);
	}
	if (ctx->unix_fd >= 0) {
		close(ctx->unix_fd);
		unlink(ctx->sock_path);
		ctx->unix_fd = -1;
	}
	if (ctx->tcp_fd >= 0) {
		close(ctx->tcp_fd);
		ctx->tcp_fd = -1;
	}
	if (ctx->epfd >= 0) {
		close(ctx->epfd);
		ctx->epfd = -1;
	}
	if (ctx->sigpipe[0] >= 0) {
		close(ctx->sigpipe[0]);
		close(ctx->sigpipe[1]);
		ctx->sigpipe[0] = ctx->sigpipe[1] = -1;
	}
}
