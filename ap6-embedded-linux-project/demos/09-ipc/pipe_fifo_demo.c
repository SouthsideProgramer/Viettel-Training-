/*
 * pipe_fifo_demo.c - pipe va FIFO (Bao cao muc 9.2).
 *
 *   1. pipe(): truyen du lieu mot chieu giua cha va con
 *   2. dup2 + pipe: dung lai chinh xac cach shell noi "ls | wc -l"
 *   3. FIFO (named pipe): hai process khong ho hang van giao tiep duoc
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define FIFO_PATH	"/tmp/ap6_demo_fifo"

static void muc_1_pipe(void)
{
	int fds[2];
	pid_t pid;
	char buf[128];
	ssize_t n;

	printf("1) pipe(): buffer trong kernel, mot chieu\n");
	if (pipe(fds) < 0) {
		perror("   pipe");
		return;
	}
	/* fds[0] = dau doc, fds[1] = dau ghi. Sau fork ca hai process deu
	 * thua huong ca hai fd -> moi ben phai dong dau khong dung, neu khong
	 * ben doc se khong bao gio thay EOF. */

	pid = fork();
	if (pid == 0) {
		const char *msg = "cau hinh: Channel=11\n";

		close(fds[0]);		/* con khong doc -> dong dau doc */
		if (write(fds[1], msg, strlen(msg)) < 0)
			perror("   [con] write");
		close(fds[1]);
		_exit(0);
	}

	close(fds[1]);			/* quan trong: cha dong dau ghi */
	n = read(fds[0], buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		printf("   [cha] nhan tu con: %s", buf);
	}
	/* Doc tiep -> tra ve 0 vi khong con dau ghi nao mo. */
	n = read(fds[0], buf, sizeof(buf) - 1);
	printf("   read() lan hai tra ve %zd (EOF vi moi dau ghi da dong)\n", n);
	close(fds[0]);
	waitpid(pid, NULL, 0);
	printf("\n");
}

static void muc_2_pipeline(void)
{
	int fds[2];
	pid_t p1, p2;

	printf("2) Pipeline kieu shell: ls /etc | wc -l\n");
	if (pipe(fds) < 0) {
		perror("   pipe");
		return;
	}

	p1 = fork();
	if (p1 == 0) {
		/* Process 1: stdout -> dau ghi cua pipe */
		dup2(fds[1], STDOUT_FILENO);
		close(fds[0]);
		close(fds[1]);
		execlp("ls", "ls", "/etc", (char *)NULL);
		_exit(127);
	}

	p2 = fork();
	if (p2 == 0) {
		/* Process 2: stdin <- dau doc cua pipe */
		dup2(fds[0], STDIN_FILENO);
		close(fds[0]);
		close(fds[1]);
		execlp("wc", "wc", "-l", (char *)NULL);
		_exit(127);
	}

	close(fds[0]);
	close(fds[1]);
	waitpid(p1, NULL, 0);
	waitpid(p2, NULL, 0);
	printf("   -> dau '|' cua shell chinh la pipe + dup2 + exec.\n\n");
}

static void muc_3_fifo(void)
{
	pid_t pid;
	char buf[128];
	int fd;
	ssize_t n;

	printf("3) FIFO (named pipe): co ten trong filesystem\n");
	unlink(FIFO_PATH);
	if (mkfifo(FIFO_PATH, 0660) < 0) {
		perror("   mkfifo");
		return;
	}
	printf("   da tao %s\n", FIFO_PATH);

	pid = fork();
	if (pid == 0) {
		/* "Process khac": mo FIFO theo ten, khong can quan he cha con.
		 * open() phia ghi se block cho den khi co ben doc. */
		int wfd = open(FIFO_PATH, O_WRONLY);

		if (wfd >= 0) {
			const char *msg = "EVENT sta_connect aa:bb:cc:dd:ee:ff\n";

			if (write(wfd, msg, strlen(msg)) < 0)
				perror("   [con] write FIFO");
			close(wfd);
		}
		_exit(0);
	}

	fd = open(FIFO_PATH, O_RDONLY);
	if (fd < 0) {
		perror("   open FIFO");
	} else {
		n = read(fd, buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = '\0';
			printf("   nhan qua FIFO: %s", buf);
		}
		close(fd);
	}
	waitpid(pid, NULL, 0);
	unlink(FIFO_PATH);
	printf("   -> FIFO hop voi kieu 'daemon ghi event, tool doc event',\n"
	       "      nhung van la dong byte mot chieu.\n");
}

int main(void)
{
	printf("=== IPC: pipe va FIFO ===\n\n");
	muc_1_pipe();
	muc_2_pipeline();
	muc_3_fifo();
	return 0;
}
