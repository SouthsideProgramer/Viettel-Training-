/*
 * signal_demo.c - signal trong Linux (Bao cao muc 10.1 - 10.5).
 *
 *   1. sigaction thay cho signal: hanh vi ro rang, co mask va flags
 *   2. Handler chi dat co volatile sig_atomic_t, xu ly o vong lap chinh
 *   3. SIGCHLD + waitpid(WNOHANG): thu hoi process con, khong de zombie
 *   4. Self-pipe: danh thuc select/poll/epoll tu signal handler
 *   5. SIGTERM vs SIGKILL: chi SIGTERM cho phep don dep truoc khi thoat
 *
 * Chay thu:  ./signal_demo &
 *            kill -HUP  <pid>   # reload cau hinh
 *            kill -TERM <pid>   # thoat mem
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/wait.h>

/* Bien duoc cham vao trong handler PHAI la volatile sig_atomic_t: kieu nay
 * dam bao doc/ghi la mot thao tac nguyen tu doi voi handler (muc 10.3). */
static volatile sig_atomic_t g_term;
static volatile sig_atomic_t g_hup;
static volatile sig_atomic_t g_chld;
static volatile sig_atomic_t g_alrm;

static int g_sigpipe[2] = { -1, -1 };

static void handler(int signo)
{
	int saved_errno = errno;	/* handler khong duoc lam hong errno */
	char b = (char)signo;

	switch (signo) {
	case SIGTERM:
	case SIGINT:  g_term = 1; break;
	case SIGHUP:  g_hup = 1;  break;
	case SIGCHLD: g_chld = 1; break;
	case SIGALRM: g_alrm = 1; break;
	default: break;
	}

	/* write() la async-signal-safe; printf/malloc thi KHONG.
	 * Ghi 1 byte vao self-pipe de danh thuc select() o vong lap chinh. */
	if (g_sigpipe[1] >= 0) {
		ssize_t n = write(g_sigpipe[1], &b, 1);
		(void)n;
	}
	errno = saved_errno;
}

static int install(int signo, void (*fn)(int), int flags)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = fn;
	sigemptyset(&sa.sa_mask);
	/* Chan cac signal khac trong luc handler chay -> tranh handler bi
	 * chen giua chung. */
	sigaddset(&sa.sa_mask, SIGTERM);
	sigaddset(&sa.sa_mask, SIGHUP);
	sa.sa_flags = flags;
	return sigaction(signo, &sa, NULL);
}

static void reap_children(void)
{
	pid_t pid;
	int status;

	/* Vong lap WNOHANG: nhieu con co the ket thuc cung luc nhung chi sinh
	 * mot SIGCHLD (muc 10.4). */
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		if (WIFEXITED(status))
			printf("   [SIGCHLD] con %ld thoat, exit=%d\n",
			       (long)pid, WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			printf("   [SIGCHLD] con %ld bi signal %d\n",
			       (long)pid, WTERMSIG(status));
	}
}

int main(void)
{
	int i, loops = 0;

	printf("=== Signal trong Linux (pid=%ld) ===\n\n", (long)getpid());

	if (pipe(g_sigpipe) < 0) {
		perror("pipe");
		return 1;
	}
	fcntl(g_sigpipe[0], F_SETFL, O_NONBLOCK);
	fcntl(g_sigpipe[1], F_SETFL, O_NONBLOCK);

	/* SA_RESTART: system call cham tu dong duoc goi lai thay vi EINTR.
	 * SA_NOCLDSTOP: chi bao SIGCHLD khi con KET THUC, khong bao khi con
	 * bi stop/continue. */
	install(SIGTERM, handler, SA_RESTART);
	install(SIGINT,  handler, SA_RESTART);
	install(SIGHUP,  handler, SA_RESTART);
	install(SIGALRM, handler, SA_RESTART);
	install(SIGCHLD, handler, SA_RESTART | SA_NOCLDSTOP);
	/* Bo qua SIGPIPE: khong de daemon chet khi client dong socket. */
	signal(SIGPIPE, SIG_IGN);

	printf("Da dang ky handler cho TERM/INT/HUP/ALRM/CHLD, bo qua PIPE\n\n");

	/* Tao vai process con ket thuc som de kich SIGCHLD. */
	for (i = 0; i < 3; i++) {
		pid_t pid = fork();

		if (pid == 0) {
			usleep((useconds_t)(200000 * (i + 1)));
			_exit(i + 1);
		}
	}
	printf("Da tao 3 process con (se ket thuc sau 0.2 - 0.6 giay)\n");

	alarm(2);	/* SIGALRM sau 2 giay - mo phong watchdog/timer */
	printf("Da dat alarm(2) - mo phong timer cua daemon\n");
	printf("Thu: kill -HUP %ld  hoac  kill -TERM %ld\n\n", (long)getpid(),
	       (long)getpid());

	/* Vong lap chinh: ngu trong select() cho den khi co su kien hoac
	 * signal danh thuc qua self-pipe. */
	while (!g_term && loops < 10) {
		fd_set rfds;
		struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
		char drain[64];
		int rc;

		FD_ZERO(&rfds);
		FD_SET(g_sigpipe[0], &rfds);

		rc = select(g_sigpipe[0] + 1, &rfds, NULL, NULL, &tv);
		if (rc < 0 && errno != EINTR) {
			perror("select");
			break;
		}
		if (rc > 0 && FD_ISSET(g_sigpipe[0], &rfds)) {
			while (read(g_sigpipe[0], drain, sizeof(drain)) > 0)
				;
		}

		/* Toan bo xu ly "nang" nam ngoai handler - o day duoc phep
		 * goi printf, malloc, waitpid... (muc 10.3). */
		if (g_chld) {
			g_chld = 0;
			reap_children();
		}
		if (g_hup) {
			g_hup = 0;
			printf("   [SIGHUP] nap lai cau hinh (khong restart)\n");
		}
		if (g_alrm) {
			g_alrm = 0;
			printf("   [SIGALRM] timer het han -> kiem tra trang thai\n");
			alarm(2);
		}
		loops++;
	}

	if (g_term)
		printf("\n[SIGTERM/SIGINT] Bat dau don dep truoc khi thoat:\n");
	else
		printf("\n[Het vong lap demo] Don dep:\n");

	/* Day chinh la phan ma SIGKILL se cuop mat: dong socket, luu trang
	 * thai, giai phong shared memory, xoa pid file (muc 10.2, 10.5). */
	printf("   - dong socket dieu khien\n");
	printf("   - ghi cau hinh xuong flash\n");
	printf("   - giai phong shared memory, xoa pid file\n");
	close(g_sigpipe[0]);
	close(g_sigpipe[1]);
	printf("   -> SIGKILL (kill -9) khong the bat, cac buoc tren se bi bo qua.\n");
	return 0;
}
