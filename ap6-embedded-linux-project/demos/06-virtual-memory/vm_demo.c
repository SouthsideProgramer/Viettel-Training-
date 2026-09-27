/*
 * vm_demo.c - virtual memory: page, page fault, demand paging, mmap
 *             (Bao cao muc 6.1 - 6.5).
 *
 * Chuong trinh minh hoa:
 *   1. Kich thuoc page va cach tach dia chi ao thanh (so trang, offset).
 *   2. Demand paging: cap phat 64 MB nhung RSS chi tang khi thuc su cham vao.
 *   3. Minor/major page fault dem bang getrusage().
 *   4. mmap file: doc file nhu doc mang, khong goi read() tung lan.
 *   5. MAP_SHARED giua process cha va con - nen tang cua shared memory.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define MB(x)	((size_t)(x) * 1024 * 1024)

static long rss_kb(void)
{
	FILE *f = fopen("/proc/self/statm", "re");
	long size = 0, resident = 0;
	long page_kb = sysconf(_SC_PAGESIZE) / 1024;

	if (!f)
		return -1;
	if (fscanf(f, "%ld %ld", &size, &resident) != 2)
		resident = 0;
	fclose(f);
	return resident * page_kb;
}

static void faults(long *minor, long *major)
{
	struct rusage ru;

	getrusage(RUSAGE_SELF, &ru);
	*minor = ru.ru_minflt;
	*major = ru.ru_majflt;
}

static void muc_1_page(void)
{
	long ps = sysconf(_SC_PAGESIZE);
	unsigned long addr = (unsigned long)&ps;

	printf("1) Page va dia chi ao\n");
	printf("   page size            = %ld byte (%ld KiB)\n", ps, ps / 1024);
	printf("   dia chi bien stack   = 0x%lx\n", addr);
	printf("   so trang ao (VPN)    = 0x%lx\n", addr / (unsigned long)ps);
	printf("   offset trong trang   = 0x%lx\n", addr % (unsigned long)ps);
	printf("   -> MMU dung VPN tra page table de ra frame vat ly;\n"
	       "      TLB cache lai ket qua tra bang nay.\n\n");
}

static void muc_2_demand_paging(void)
{
	const size_t len = MB(64);
	long ps = sysconf(_SC_PAGESIZE);
	long mi0, ma0, mi1, ma1;
	long rss0, rss1, rss2;
	char *p;
	size_t i;

	printf("2) Demand paging: xin 64 MB nhung chua dung\n");
	rss0 = rss_kb();
	faults(&mi0, &ma0);

	/* MAP_ANONYMOUS|MAP_PRIVATE: kernel chi tao mapping, chua cap frame
	 * vat ly nao (Bao cao muc 6.3). */
	p = mmap(NULL, len, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		perror("   mmap");
		return;
	}
	rss1 = rss_kb();
	printf("   sau mmap 64MB        : RSS %ld KB -> %ld KB (gan nhu khong doi)\n",
	       rss0, rss1);

	/* Cham vao 1 byte moi trang -> moi lan sinh mot minor page fault. */
	for (i = 0; i < len; i += (size_t)ps)
		p[i] = 1;

	rss2 = rss_kb();
	faults(&mi1, &ma1);
	printf("   sau khi ghi tung trang: RSS %ld KB (+%ld KB)\n", rss2,
	       rss2 - rss1);
	printf("   minor fault tang     : %ld  (nap trang, khong cham dia)\n",
	       mi1 - mi0);
	printf("   major fault tang     : %ld  (phai doc tu disk/swap)\n",
	       ma1 - ma0);
	printf("   -> RAM chi bi chiem khi process THUC SU cham vao trang.\n\n");

	munmap(p, len);
}

static void muc_3_mmap_file(void)
{
	const char *path = "/tmp/vm_demo_file.bin";
	const size_t len = 4096;
	char *map;
	int fd;

	printf("3) mmap file: doc/ghi file nhu mang byte\n");
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		perror("   open");
		return;
	}
	if (ftruncate(fd, (off_t)len) < 0) {
		perror("   ftruncate");
		close(fd);
		return;
	}

	map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);		/* mapping van con hieu luc sau khi dong fd */
	if (map == MAP_FAILED) {
		perror("   mmap");
		return;
	}

	strcpy(map, "Du lieu duoc ghi qua con tro, khong goi write()");
	msync(map, len, MS_SYNC);	/* day xuong file (muc 5.4) */
	printf("   noi dung trong file  : \"%s\"\n", map);
	munmap(map, len);

	/* Kiem tra lai bang read() thong thuong. */
	fd = open(path, O_RDONLY);
	if (fd >= 0) {
		char buf[64] = { 0 };

		if (read(fd, buf, sizeof(buf) - 1) > 0)
			printf("   doc lai bang read()  : \"%s\"\n", buf);
		close(fd);
	}
	unlink(path);
	printf("   -> voi file lon hoac truy cap ngau nhien, mmap tiet kiem\n"
	       "      duoc nhieu lan copy giua kernel va user space.\n\n");
}

static void muc_4_map_shared_fork(void)
{
	int *shared, *private_map;
	pid_t pid;

	printf("4) MAP_SHARED vs MAP_PRIVATE qua fork()\n");
	shared = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	private_map = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (shared == MAP_FAILED || private_map == MAP_FAILED) {
		perror("   mmap");
		return;
	}
	*shared = 100;
	*private_map = 100;

	pid = fork();
	if (pid == 0) {
		*shared = 999;
		*private_map = 999;	/* copy-on-write: chi con thay doi */
		_exit(0);
	}
	waitpid(pid, NULL, 0);

	printf("   MAP_SHARED  sau khi con ghi 999 : %d  <- cha thay thay doi\n",
	       *shared);
	printf("   MAP_PRIVATE sau khi con ghi 999 : %d  <- copy-on-write\n",
	       *private_map);
	printf("   -> MAP_SHARED chinh la co che dung cho shared memory IPC.\n");

	munmap(shared, sizeof(int));
	munmap(private_map, sizeof(int));
}

int main(void)
{
	printf("=== Virtual memory tren Linux ===\n\n");
	muc_1_page();
	muc_2_demand_paging();
	muc_3_mmap_file();
	muc_4_map_shared_fork();
	printf("\nGoi y quan sat them: pmap %ld | head -20\n", (long)getpid());
	return 0;
}
