/*
 * apctl.c - client dieu khien apd.
 *
 * Bao cao muc 13.3: luong client la socket -> connect -> send/recv -> close.
 * Chuong trinh ho tro ca Unix domain socket (mac dinh, IPC noi bo) va TCP
 * (quan ly tu xa), cho thay cung mot doan code xu ly du lieu chi khac o buoc
 * tao dia chi.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include "../common/util.h"

#define DEFAULT_SOCK	"/tmp/ap6/apd.sock"
#define RECV_TIMEOUT_S	3

static int connect_unix(const char *path)
{
	struct sockaddr_un addr;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "connect(%s): %s\n", path, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

static int connect_tcp(const char *host, int port)
{
	struct sockaddr_in addr;
	int fd;

	fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);	/* network byte order */
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		fprintf(stderr, "dia chi IP khong hop le: %s\n", host);
		close(fd);
		return -1;
	}

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "connect(%s:%d): %s\n", host, port,
			strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

/* Gui mot lenh, in phan hoi cho den khi server im lang (timeout ngan). */
static int send_command(int fd, const char *cmd)
{
	char line[1024];
	char buf[4096];
	int len;

	len = snprintf(line, sizeof(line), "%s\n", cmd);
	if (write_all(fd, line, (size_t)len) < 0) {
		perror("write");
		return -1;
	}

	for (;;) {
		ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;	/* het du lieu -> ket thuc phan hoi */
			perror("recv");
			return -1;
		}
		if (n == 0)
			break;		/* server dong ket noi */
		buf[n] = '\0';
		fputs(buf, stdout);
		if ((size_t)n < sizeof(buf) - 1)
			break;
	}
	fflush(stdout);
	return 0;
}

static void usage(const char *prog)
{
	printf("Cach dung:\n"
	       "  %s [-s SOCKET] <lenh> [tham so...]\n"
	       "  %s -H 127.0.0.1 -P 9000 <lenh>\n"
	       "\nVi du:\n"
	       "  %s STATUS\n"
	       "  %s SET SSID Viettel_Lab\n"
	       "  %s SET Channel 11\n"
	       "  %s APPLY\n"
	       "  %s SHOW\n",
	       prog, prog, prog, prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
	const char *sock = DEFAULT_SOCK;
	const char *host = NULL;
	int port = 0;
	char cmd[1024] = "";
	struct timeval tv;
	int fd, i, c;

	while ((c = getopt(argc, argv, "+s:H:P:h")) != -1) {
		switch (c) {
		case 's': sock = optarg; break;
		case 'H': host = optarg; break;
		case 'P': port = atoi(optarg); break;
		case 'h': usage(argv[0]); return 0;
		default:  usage(argv[0]); return 2;
		}
	}

	if (optind >= argc) {
		usage(argv[0]);
		return 2;
	}

	/* Ghep cac tham so con lai thanh mot dong lenh. */
	for (i = optind; i < argc; i++) {
		if (i > optind)
			strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
		strncat(cmd, argv[i], sizeof(cmd) - strlen(cmd) - 1);
	}

	fd = host ? connect_tcp(host, port ? port : 9000) : connect_unix(sock);
	if (fd < 0)
		return 1;

	/* SO_RCVTIMEO: khong treo vo han neu daemon khong tra loi
	 * (Bao cao muc 13.4 - blocking I/O can co timeout). */
	tv.tv_sec = RECV_TIMEOUT_S;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	if (send_command(fd, cmd) < 0) {
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}
