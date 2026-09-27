/*
 * udp_telemetry.c - UDP: gui/nhan datagram (Bao cao muc 13.2).
 *
 * Mo phong AP gui telemetry dinh ky ve server quan ly: mat mot goi khong
 * nghiem trong, do tre thap quan trong hon -> UDP hop hon TCP.
 *
 * Chay ca hai vai tro trong mot chuong trinh de tien thu:
 *   ./udp_telemetry server 9200        # nhan
 *   ./udp_telemetry client 127.0.0.1 9200 5   # gui 5 ban tin
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>

static volatile sig_atomic_t g_stop;

static void on_term(int signo)
{
	(void)signo;
	g_stop = 1;
}

static int run_server(int port)
{
	struct sockaddr_in addr;
	struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
	int fd, count = 0;

	/* SOCK_DGRAM: khong co listen/accept, khong co ket noi. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)port);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(fd);
		return 1;
	}
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	printf("UDP collector dang nghe udp/%d (Ctrl+C de dung)\n", port);
	while (!g_stop) {
		struct sockaddr_in from;
		socklen_t flen = sizeof(from);
		char buf[512], ip[INET_ADDRSTRLEN];
		ssize_t n;

		/* recvfrom cho biet luon dia chi nguoi gui - khong can ket noi
		 * truoc nhu TCP. */
		n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
			     (struct sockaddr *)&from, &flen);
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK ||
			    errno == EINTR)
				continue;
			perror("recvfrom");
			break;
		}
		buf[n] = '\0';
		inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
		printf("  [%s:%d] %s", ip, ntohs(from.sin_port), buf);
		if (n > 0 && buf[n - 1] != '\n')
			printf("\n");
		count++;
	}
	printf("Da nhan %d datagram\n", count);
	close(fd);
	return 0;
}

static int run_client(const char *host, int port, int n)
{
	struct sockaddr_in addr;
	int fd, i;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		fprintf(stderr, "dia chi khong hop le: %s\n", host);
		close(fd);
		return 1;
	}

	for (i = 1; i <= n && !g_stop; i++) {
		char msg[256];
		int len;

		len = snprintf(msg, sizeof(msg),
			       "ap6 telemetry seq=%d clients=%d channel=%d "
			       "noise=-%d\n", i, i % 5, 6, 90 + (i % 6));

		/* sendto: khong bao dam den noi, khong bao dam thu tu.
		 * Doi lai khong co handshake, do tre thap. */
		if (sendto(fd, msg, (size_t)len, 0, (struct sockaddr *)&addr,
			   sizeof(addr)) < 0) {
			perror("sendto");
			break;
		}
		printf("  gui: %s", msg);
		usleep(300000);
	}
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_term;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	if (argc >= 2 && strcmp(argv[1], "server") == 0)
		return run_server(argc > 2 ? atoi(argv[2]) : 9200);

	if (argc >= 2 && strcmp(argv[1], "client") == 0)
		return run_client(argc > 2 ? argv[2] : "127.0.0.1",
				  argc > 3 ? atoi(argv[3]) : 9200,
				  argc > 4 ? atoi(argv[4]) : 5);

	printf("Cach dung:\n"
	       "  %s server [port]\n"
	       "  %s client [host] [port] [so_ban_tin]\n", argv[0], argv[0]);
	return 2;
}
