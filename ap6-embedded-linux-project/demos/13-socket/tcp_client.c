/*
 * tcp_client.c - TCP client (Bao cao muc 13.3).
 *
 * Luong: socket -> connect -> send -> recv -> close
 *
 * Chay: ./tcp_client 127.0.0.1 9100 "xin chao tu client"
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>

int main(int argc, char **argv)
{
	struct sockaddr_in addr;
	struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
	const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
	int port = (argc > 2) ? atoi(argv[2]) : 9100;
	const char *msg = (argc > 3) ? argv[3] : "ping tu AP\n";
	char line[512], buf[1024];
	ssize_t n, sent = 0;
	int fd, len;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	/* inet_pton doi chuoi "192.168.1.1" sang dang nhi phan network order.
	 * Tra ve 1 = OK, 0 = chuoi sai dinh dang, -1 = ho dia chi sai. */
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		fprintf(stderr, "dia chi khong hop le: %s\n", host);
		close(fd);
		return 1;
	}

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "connect(%s:%d): %s\n", host, port,
			strerror(errno));
		close(fd);
		return 1;
	}
	printf("Da ket noi %s:%d\n", host, port);

	/* Timeout de khong treo vinh vien neu server khong tra loi. */
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	len = snprintf(line, sizeof(line), "%s", msg);
	if (len > 0 && line[len - 1] != '\n' && len < (int)sizeof(line) - 1) {
		line[len++] = '\n';
		line[len] = '\0';
	}

	/* send() co the gui thieu -> phai lap cho den khi het du lieu. */
	while (sent < len) {
		ssize_t w = send(fd, line + sent, (size_t)(len - sent), 0);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			perror("send");
			close(fd);
			return 1;
		}
		sent += w;
	}
	printf("Da gui %zd byte: %s", sent, line);

	n = recv(fd, buf, sizeof(buf) - 1, 0);
	if (n > 0) {
		buf[n] = '\0';
		printf("Nhan lai %zd byte: %s", n, buf);
	} else if (n == 0) {
		printf("Server da dong ket noi\n");
	} else {
		fprintf(stderr, "recv: %s\n", strerror(errno));
	}

	close(fd);
	return 0;
}
