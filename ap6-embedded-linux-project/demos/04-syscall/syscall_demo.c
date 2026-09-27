/*
 * syscall_demo.c - ranh gioi user space / kernel space (Bao cao muc 4.1, 4.2).
 *
 * Chuong trinh cho thay:
 *   1. printf (thu vien C, co buffer o user space) khac write (system call).
 *   2. Moi system call deu co the that bai -> phai kiem tra tra ve + errno.
 *   3. "Everything is a file": doc /proc, /dev/urandom bang cung mot bo lenh.
 *
 * Quan sat so lan chuyen sang kernel mode:
 *   strace -c ./syscall_demo
 *   strace -e trace=write,openat ./syscall_demo
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>

static void muc_1_printf_vs_write(void)
{
	const char *msg = "  [write]  di thang xuong kernel, khong qua buffer\n";

	printf("1) printf va write\n");
	/* printf ghi vao buffer cua glibc; khi stdout la terminal thi buffer
	 * theo dong, khi la file/pipe thi buffer theo khoi -> thu tu in ra
	 * co the khac neu khong flush. */
	printf("  [printf] duoc glibc dem lai o user space\n");
	fflush(stdout);
	if (write(STDOUT_FILENO, msg, strlen(msg)) < 0)
		perror("  write");
	printf("  -> chay: strace -c ./syscall_demo de dem so system call\n\n");
}

static void muc_2_loi_va_errno(void)
{
	int fd;

	printf("2) Kiem tra loi cua system call\n");
	fd = open("/khong/ton/tai/tren/rootfs", O_RDONLY);
	if (fd < 0) {
		printf("  open() tra ve -1, errno=%d (%s)\n", errno,
		       strerror(errno));
	} else {
		close(fd);
	}

	if (write(-1, "x", 1) < 0)
		printf("  write(fd sai) -> errno=%d (%s)\n", errno,
		       strerror(errno));
	printf("  -> tren thiet bi nhung, bo qua gia tri tra ve la nguyen nhan\n"
	       "     pho bien cua loi kho tai hien\n\n");
}

static void muc_3_moi_thu_la_file(void)
{
	const char *paths[] = { "/proc/version", "/proc/uptime", NULL };
	unsigned char rnd[8];
	int i, fd;

	printf("3) 'Everything is a file' - cung open/read cho nhieu loai\n");
	for (i = 0; paths[i]; i++) {
		char buf[256];
		ssize_t n;

		fd = open(paths[i], O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			printf("  %-16s : khong mo duoc (%s)\n", paths[i],
			       strerror(errno));
			continue;
		}
		n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n > 0) {
			buf[n] = '\0';
			if (strchr(buf, '\n'))
				*strchr(buf, '\n') = '\0';
			printf("  %-16s : %.60s\n", paths[i], buf);
		}
	}

	/* /dev/urandom la device node -> read() cua no chay trong driver,
	 * khong doc du lieu tren dia (Bao cao muc 4.2, 11.4). */
	fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd >= 0) {
		if (read(fd, rnd, sizeof(rnd)) == (ssize_t)sizeof(rnd)) {
			printf("  %-16s : ", "/dev/urandom");
			for (i = 0; i < (int)sizeof(rnd); i++)
				printf("%02x", rnd[i]);
			printf("  <- du lieu do driver sinh ra\n");
		}
		close(fd);
	}
	printf("  -> tren board that, /dev/ttyS0, /dev/i2c-1, /dev/spidev0.0\n"
	       "     cung duoc thao tac theo dung mo hinh nay\n\n");
}

static void muc_4_syscall_truc_tiep(void)
{
	struct utsname u;
	long tid;

	printf("4) Goi system call truc tiep bang syscall()\n");
	/* gettid khong co wrapper tren mot so phien ban glibc cu -> goi thang
	 * so hieu system call, giong cach cac thu vien he thong lam. */
	tid = syscall(SYS_gettid);
	printf("  syscall(SYS_gettid) = %ld (pid=%ld)\n", tid, (long)getpid());

	if (uname(&u) == 0)
		printf("  uname(): %s %s %s\n", u.sysname, u.release,
		       u.machine);
	printf("  -> user space luon phai di qua system call de vao kernel\n");
}

int main(void)
{
	printf("=== Ranh gioi user space <-> kernel space ===\n\n");
	muc_1_printf_vs_write();
	muc_2_loi_va_errno();
	muc_3_moi_thu_la_file();
	muc_4_syscall_truc_tiep();
	return 0;
}
