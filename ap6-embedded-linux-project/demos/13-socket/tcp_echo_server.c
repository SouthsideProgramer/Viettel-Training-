/*
 * tcp_echo_server.c - TCP server dung poll() phuc vu nhieu client
 *                     (Bao cao muc 13.1 - 13.4).
 *
 * Luong: socket -> bind -> listen -> accept -> recv/send -> close
 *
 * Server nay khong fork mot process cho moi client (ton RAM tren thiet bi
 * nhung) ma dung mot vong lap poll() duy nhat - dung mo hinh ma daemon apd
 * su dung voi epoll.
 *
 * Chay: ./tcp_echo_server 9100
 * Test: nc 127.0.0.1 9100     hoac    ./tcp_client 127.0.0.1 9100 "xin chao"
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MAX_CLIENTS	8
#define BUFSZ		1024

static volatile sig_atomic_t g_stop;

static void on_term(int signo)
{
	(void)signo;
	g_stop = 1;
}

int main(int argc, char **argv)
{
	struct pollfd fds[MAX_CLIENTS + 1];
	struct sockaddr_in addr;
	struct sigaction sa;
	int port = (argc > 1) ? atoi(argv[1]) : 9100;
	int listen_fd, nfds = 1, on = 1;
	long total_conn = 0;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_term;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);	/* client dong dot ngot -> khong chet */

	/* 1. socket(): tao endpoint, tra ve file descriptor. */
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		perror("socket");
		return 1;
	}
	/* SO_REUSEADDR: bind lai duoc ngay sau khi restart (tranh
	 * "Address already in use" do TIME_WAIT). */
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	/* 2. bind(): gan dia chi + port. htons/htonl doi sang network byte
	 *    order - quen buoc nay la loi kinh dien (muc 13.4). */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)port);

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(listen_fd);
		return 1;
	}

	/* 3. listen(): chuyen sang trang thai cho ket noi. */
	if (listen(listen_fd, 8) < 0) {
		perror("listen");
		close(listen_fd);
		return 1;
	}

	printf("TCP echo server dang nghe tren 0.0.0.0:%d (Ctrl+C de dung)\n",
	       port);

	memset(fds, 0, sizeof(fds));
	fds[0].fd = listen_fd;
	fds[0].events = POLLIN;

	while (!g_stop) {
		int i, rc;

		rc = poll(fds, (nfds_t)nfds, 1000);
		if (rc < 0) {
			if (errno == EINTR)
				continue;	/* bi signal ngat */
			perror("poll");
			break;
		}
		if (rc == 0)
			continue;

		/* 4. accept(): moi ket noi duoc chap nhan sinh ra MOT fd moi,
		 *    dai dien cho phien giao tiep voi client do. */
		if (fds[0].revents & POLLIN) {
			struct sockaddr_in cli;
			socklen_t clilen = sizeof(cli);
			char ip[INET_ADDRSTRLEN];
			int cfd = accept(listen_fd, (struct sockaddr *)&cli,
					 &clilen);

			if (cfd < 0) {
				if (errno != EINTR && errno != EAGAIN)
					perror("accept");
			} else if (nfds > MAX_CLIENTS) {
				const char *msg = "server ban, thu lai sau\n";

				send(cfd, msg, strlen(msg), 0);
				close(cfd);	/* dong ngay - tranh can kiet fd */
			} else {
				inet_ntop(AF_INET, &cli.sin_addr, ip,
					  sizeof(ip));
				printf("  + client %s:%d (fd=%d)\n", ip,
				       ntohs(cli.sin_port), cfd);
				fds[nfds].fd = cfd;
				fds[nfds].events = POLLIN;
				nfds++;
				total_conn++;
			}
		}

		/* 5. recv/send tren tung ket noi. */
		for (i = 1; i < nfds; i++) {
			char buf[BUFSZ];
			ssize_t n;

			if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
				continue;

			n = recv(fds[i].fd, buf, sizeof(buf) - 1, 0);
			if (n > 0) {
				buf[n] = '\0';
				printf("  fd=%d nhan %zd byte: %.*s", fds[i].fd,
				       n, (int)n, buf);
				if (buf[n - 1] != '\n')
					printf("\n");
				/* Echo lai; send co the ghi thieu -> vong lap. */
				{
					ssize_t sent = 0;

					while (sent < n) {
						ssize_t w = send(fds[i].fd,
								 buf + sent,
								 (size_t)(n - sent),
								 0);
						if (w <= 0)
							break;
						sent += w;
					}
				}
				continue;
			}

			if (n < 0 && (errno == EAGAIN || errno == EINTR))
				continue;

			/* n == 0: peer da dong ket noi (orderly shutdown). */
			printf("  - dong fd=%d\n", fds[i].fd);
			close(fds[i].fd);
			fds[i] = fds[nfds - 1];
			nfds--;
			i--;
		}
	}

	printf("\nDung server. Tong so ket noi da phuc vu: %ld\n", total_conn);
	{
		int i;

		for (i = 1; i < nfds; i++)
			close(fds[i].fd);
	}
	close(listen_fd);
	return 0;
}
