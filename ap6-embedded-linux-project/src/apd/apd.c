/*
 * apd.c - daemon quan ly Access Point (phan tich hop cua bao cao thuc tap).
 *
 * Vong doi: parse tham so -> nap profile -> mo backend driver -> tao shm
 * telemetry -> dang ky signal handler -> mo socket -> chay epoll loop.
 * Khi nhan SIGTERM: dong socket, dung thread, ghi cau hinh, giai phong shm.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "apd.h"
#include "../common/log.h"
#include "../common/util.h"

#define DEFAULT_PROFILE	"/tmp/ap6/ap6.dat"
#define DEFAULT_SOCK	"/tmp/ap6/apd.sock"
#define DEFAULT_PID	"/tmp/ap6/apd.pid"
#define DEFAULT_SCRIPT	"scripts/apply_profile.sh"

/* Bien duy nhat duoc cham vao tu signal handler phai la
 * volatile sig_atomic_t (Bao cao muc 10.3). */
static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_reload;
static int g_sigwrite = -1;	/* dau ghi cua self-pipe */

void apd_request_stop(void)
{
	g_stop = 1;
}

int apd_should_stop(void)
{
	return g_stop;
}

/*
 * Signal handler chi lam hai viec an toan: dat co va danh thuc epoll loop
 * bang self-pipe. Moi xu ly that su nam trong main loop, noi co the goi
 * bat ky ham nao (Bao cao muc 10.3).
 */
static void sig_handler(int signo)
{
	int saved_errno = errno;
	unsigned char byte = (unsigned char)signo;

	switch (signo) {
	case SIGTERM:
	case SIGINT:
		g_stop = 1;
		break;
	case SIGHUP:
		g_reload = 1;
		break;
	default:
		break;
	}

	if (g_sigwrite >= 0) {
		/* write() la async-signal-safe; bo qua ket qua co y. */
		ssize_t n = write(g_sigwrite, &byte, 1);
		(void)n;
	}
	errno = saved_errno;
}

static int install_signal_handlers(struct apd_ctx *ctx)
{
	struct sigaction sa;

	g_sigwrite = ctx->sigpipe[1];

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigemptyset(&sa.sa_mask);
	/* SA_RESTART: cac system call cham duoc tu dong goi lai thay vi
	 * tra ve EINTR (Bao cao muc 10.3 - sigaction ro rang hon signal). */
	sa.sa_flags = SA_RESTART;

	if (sigaction(SIGTERM, &sa, NULL) < 0 ||
	    sigaction(SIGINT, &sa, NULL) < 0 ||
	    sigaction(SIGHUP, &sa, NULL) < 0 ||
	    sigaction(SIGCHLD, &sa, NULL) < 0)
		return -1;

	/* SIGPIPE: neu client dong ket noi giua chung, write() se lam
	 * daemon chet neu khong bo qua signal nay (Bao cao muc 13.4). */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &sa, NULL) < 0)
		return -1;

	return 0;
}

/* Thu hoi tat ca process con da ket thuc - tranh zombie (Bao cao muc 10.4). */
static void reap_children(struct apd_ctx *ctx)
{
	pid_t pid;
	int status;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		ctx->last_child = pid;
		ctx->last_child_status = status;
		ctx->last_child_valid = 1;
		if (WIFEXITED(status))
			log_info("process con %ld ket thuc, exit=%d",
				 (long)pid, WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			log_warn("process con %ld bi signal %d",
				 (long)pid, WTERMSIG(status));
	}
}

/*
 * apd_apply_profile - fork + exec script ap dung cau hinh.
 *
 * Day chinh la mo hinh o Bao cao muc 7.3/7.4: process cha fork, process con
 * exec sang chuong trinh khac, cha khong block ma thu hoi con qua SIGCHLD.
 * Tren thiet bi that, script nay se goi iwpriv / uci commit / wifi reload.
 */
int apd_apply_profile(struct apd_ctx *ctx, char *reply, size_t replylen)
{
	struct hw_radio radio;
	const char *ssid, *ch, *txp;
	pid_t pid;

	pthread_mutex_lock(&ctx->lock);
	ssid = profile_get(&ctx->profile, "SSID");
	ch = profile_get(&ctx->profile, "Channel");
	txp = profile_get(&ctx->profile, "TxPower");

	memset(&radio, 0, sizeof(radio));
	radio.channel = ch ? (uint32_t)atoi(ch) : 6;
	radio.txpower = txp ? (uint32_t)atoi(txp) : 100;
	radio.enabled = 1;
	snprintf(radio.ssid, sizeof(radio.ssid), "%s", ssid ? ssid : "");
	pthread_mutex_unlock(&ctx->lock);

	/* Buoc 1: day cau hinh xuong driver bang ioctl. */
	if (hwctl_set_radio(ctx->hw, &radio) < 0) {
		snprintf(reply, replylen,
			 "ERR khong ap dung duoc cau hinh xuong driver");
		return -1;
	}

	/* Buoc 2: goi script he thong (iwpriv/uci) trong process con. */
	pid = fork();
	if (pid < 0) {
		snprintf(reply, replylen, "ERR fork that bai: %s",
			 strerror(errno));
		return -1;
	}

	if (pid == 0) {
		/* Process con: chi duoc dung ham async-signal-safe truoc exec.
		 * Dat lai signal disposition ve mac dinh de script khong thua
		 * huong handler cua daemon. */
		char chbuf[16], txbuf[16];

		signal(SIGTERM, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGHUP, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);
		signal(SIGPIPE, SIG_DFL);

		snprintf(chbuf, sizeof(chbuf), "%u", radio.channel);
		snprintf(txbuf, sizeof(txbuf), "%u", radio.txpower);

		execl("/bin/sh", "sh", ctx->apply_script, radio.ssid, chbuf,
		      txbuf, (char *)NULL);
		/* Chi den day neu exec that bai. */
		_exit(127);
	}

	log_info("APPLY: da fork process con pid=%ld chay %s", (long)pid,
		 ctx->apply_script);
	snprintf(reply, replylen,
		 "OK apply ssid=%s channel=%u txpower=%u child_pid=%ld",
		 radio.ssid, radio.channel, radio.txpower, (long)pid);
	return 0;
}

int apd_reload_profile(struct apd_ctx *ctx)
{
	struct profile tmp;
	int ret;

	if (profile_load(&tmp, ctx->profile_path) < 0) {
		log_warn("reload: khong doc duoc %s (%s), giu cau hinh cu",
			 ctx->profile_path, strerror(errno));
		return -1;
	}

	pthread_mutex_lock(&ctx->lock);
	ctx->profile = tmp;
	pthread_mutex_unlock(&ctx->lock);

	ret = 0;
	log_info("reload: da nap lai profile tu %s", ctx->profile_path);
	return ret;
}

/* Xu ly byte doc duoc tu self-pipe: goi tu main loop, khong phai handler. */
static void handle_signal_events(struct apd_ctx *ctx)
{
	unsigned char buf[64];
	ssize_t n;

	/* Doc het pipe (pipe o che do non-blocking). */
	while ((n = read(ctx->sigpipe[0], buf, sizeof(buf))) > 0)
		;
	(void)n;

	if (g_reload) {
		g_reload = 0;
		apd_reload_profile(ctx);
	}
	/* SIGCHLD co the den bat ky luc nao -> luon thu reap. */
	reap_children(ctx);
}

static void usage(const char *prog)
{
	printf("Cach dung: %s [tuy chon]\n"
	       "  -p, --profile FILE   duong dan profile (mac dinh %s)\n"
	       "  -s, --socket PATH    Unix domain socket (mac dinh %s)\n"
	       "  -t, --tcp PORT       mo them TCP control port (0 = tat)\n"
	       "  -S, --script FILE    script ap dung cau hinh (mac dinh %s)\n"
	       "  -i, --pidfile FILE   pid file (mac dinh %s)\n"
	       "  -d, --daemon         chay nen (daemonize + syslog)\n"
	       "  -v, --verbose        bat log debug\n"
	       "  -h, --help           hien thi tro giup\n",
	       prog, DEFAULT_PROFILE, DEFAULT_SOCK, DEFAULT_SCRIPT,
	       DEFAULT_PID);
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "profile", required_argument, NULL, 'p' },
		{ "socket",  required_argument, NULL, 's' },
		{ "tcp",     required_argument, NULL, 't' },
		{ "script",  required_argument, NULL, 'S' },
		{ "pidfile", required_argument, NULL, 'i' },
		{ "daemon",  no_argument,       NULL, 'd' },
		{ "verbose", no_argument,       NULL, 'v' },
		{ "help",    no_argument,       NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	struct apd_ctx ctx;
	enum log_level level = LOG_L_INFO;
	int daemon_mode = 0;
	int rc = 0;
	int c;

	memset(&ctx, 0, sizeof(ctx));
	snprintf(ctx.profile_path, sizeof(ctx.profile_path), "%s",
		 DEFAULT_PROFILE);
	snprintf(ctx.sock_path, sizeof(ctx.sock_path), "%s", DEFAULT_SOCK);
	snprintf(ctx.pid_path, sizeof(ctx.pid_path), "%s", DEFAULT_PID);
	snprintf(ctx.apply_script, sizeof(ctx.apply_script), "%s",
		 DEFAULT_SCRIPT);
	ctx.unix_fd = ctx.tcp_fd = ctx.epfd = -1;
	ctx.sigpipe[0] = ctx.sigpipe[1] = -1;

	while ((c = getopt_long(argc, argv, "p:s:t:S:i:dvh", opts, NULL)) != -1) {
		switch (c) {
		case 'p':
			snprintf(ctx.profile_path, sizeof(ctx.profile_path),
				 "%s", optarg);
			break;
		case 's':
			snprintf(ctx.sock_path, sizeof(ctx.sock_path), "%s",
				 optarg);
			break;
		case 't':
			ctx.tcp_port = atoi(optarg);
			break;
		case 'S':
			snprintf(ctx.apply_script, sizeof(ctx.apply_script),
				 "%s", optarg);
			break;
		case 'i':
			snprintf(ctx.pid_path, sizeof(ctx.pid_path), "%s",
				 optarg);
			break;
		case 'd':
			daemon_mode = 1;
			break;
		case 'v':
			level = LOG_L_DEBUG;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 2;
		}
	}

	if (daemon_mode && daemonize() < 0) {
		fprintf(stderr, "daemonize that bai: %s\n", strerror(errno));
		return 1;
	}

	log_init("apd", level, daemon_mode);
	log_info("khoi dong apd (pid=%ld)", (long)getpid());

	ctx.start_time = monotonic_now();
	pthread_mutex_init(&ctx.lock, NULL);
	pthread_cond_init(&ctx.cond, NULL);

	/* 1. Profile: doc tu flash, thieu thi dung mac dinh. */
	if (profile_load(&ctx.profile, ctx.profile_path) < 0) {
		log_warn("khong doc duoc %s (%s) -> dung gia tri mac dinh",
			 ctx.profile_path, strerror(errno));
		profile_defaults(&ctx.profile);
	} else {
		log_info("da nap profile tu %s (%zu tham so)", ctx.profile_path,
			 ctx.profile.n);
	}

	/* 2. Backend phan cung (driver hoac mo phong). */
	ctx.hw = hwctl_open("/dev/ap6sim");
	if (!ctx.hw) {
		log_err("khong khoi tao duoc hwctl");
		rc = 1;
		goto out;
	}
	log_info("backend phan cung: %s", hwctl_backend(ctx.hw));

	/* 3. Shared memory telemetry cho process quan sat (apmon). */
	ctx.stats = stats_create(AP6_SHM_NAME);
	if (!ctx.stats)
		log_warn("khong tao duoc shm %s (%s) - bo qua telemetry",
			 AP6_SHM_NAME, strerror(errno));

	/* 4. Socket + epoll + self-pipe cho signal. */
	if (server_init(&ctx) < 0) {
		rc = 1;
		goto out;
	}
	if (install_signal_handlers(&ctx) < 0) {
		log_err("dang ky signal handler that bai: %s", strerror(errno));
		rc = 1;
		goto out;
	}

	/* 5. Thread telemetry. */
	if (ctx.stats && telemetry_start(&ctx) < 0)
		log_warn("khong tao duoc thread telemetry");

	if (pidfile_write(ctx.pid_path) < 0)
		log_warn("khong ghi duoc pid file %s: %s", ctx.pid_path,
			 strerror(errno));

	log_info("san sang: socket=%s tcp_port=%d profile=%s", ctx.sock_path,
		 ctx.tcp_port, ctx.profile_path);

	/* 6. Main loop. */
	while (!apd_should_stop()) {
		int n = server_run(&ctx);

		if (n < 0)
			break;
		if (n == 1)		/* co su kien tu self-pipe */
			handle_signal_events(&ctx);
	}

	log_info("nhan yeu cau dung, bat dau don dep");

out:
	/* Thu tu don dep nguoc voi thu tu khoi tao (Bao cao muc 11.2). */
	telemetry_stop(&ctx);
	server_cleanup(&ctx);
	pidfile_remove(ctx.pid_path);
	if (ctx.stats) {
		stats_close(ctx.stats);
		stats_unlink(AP6_SHM_NAME);
	}
	hwctl_close(ctx.hw);
	pthread_cond_destroy(&ctx.cond);
	pthread_mutex_destroy(&ctx.lock);
	log_info("apd da thoat (rc=%d)", rc);
	log_close();
	return rc;
}
