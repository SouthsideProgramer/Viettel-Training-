/*
 * shm_sem_demo.c - shared memory + semaphore (Bao cao muc 9.4, 9.5).
 *
 * Chuong trinh chay hai lan cung mot phep thu: nhieu process con cung tang
 * mot bien dem trong shared memory.
 *   - Lan 1: KHONG dong bo  -> mat cap nhat (giong race condition o thread)
 *   - Lan 2: co POSIX semaphore -> ket qua dung
 *
 * Diem quan trong: shared memory chi cho vung nho chung, KHONG cho dong bo.
 *
 * Bien dich: gcc -Wall -o shm_sem_demo shm_sem_demo.c -lrt -pthread
 * Quan sat  : ls -l /dev/shm/
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define SHM_NAME	"/ap6_demo_shm"
#define SEM_NAME	"/ap6_demo_sem"
#define NPROC		4
#define NLOOPS		50000

struct shared_area {
	long counter;
	long updates;
	char last_writer[32];
};

static struct shared_area *map_shm(int create)
{
	struct shared_area *p;
	int fd;

	fd = shm_open(SHM_NAME, create ? (O_CREAT | O_RDWR) : O_RDWR, 0660);
	if (fd < 0)
		return NULL;
	if (create && ftruncate(fd, sizeof(*p)) < 0) {
		close(fd);
		return NULL;
	}
	/* MAP_SHARED: moi thay doi deu nhin thay boi cac process khac cung
	 * mmap vung nay - khong ton lan copy nao qua kernel (muc 9.4). */
	p = mmap(NULL, sizeof(*p), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	return (p == MAP_FAILED) ? NULL : p;
}

static void run_children(struct shared_area *sh, sem_t *sem)
{
	pid_t pids[NPROC];
	int i, j;

	for (i = 0; i < NPROC; i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			for (j = 0; j < NLOOPS; j++) {
				if (sem)
					sem_wait(sem);	/* vao critical section */

				sh->counter++;
				sh->updates++;

				if (sem)
					sem_post(sem);	/* roi critical section */
			}
			snprintf(sh->last_writer, sizeof(sh->last_writer),
				 "pid %ld", (long)getpid());
			_exit(0);
		}
	}
	for (i = 0; i < NPROC; i++)
		waitpid(pids[i], NULL, 0);
}

int main(void)
{
	struct shared_area *sh;
	sem_t *sem;
	long expected = (long)NPROC * NLOOPS;

	printf("=== IPC: shared memory + semaphore ===\n\n");

	shm_unlink(SHM_NAME);
	sem_unlink(SEM_NAME);

	sh = map_shm(1);
	if (!sh) {
		fprintf(stderr, "shm_open/mmap that bai: %s\n",
			strerror(errno));
		return 1;
	}
	memset(sh, 0, sizeof(*sh));

	/* --- Lan 1: khong dong bo --- */
	printf("Lan 1: %d process x %d vong, KHONG dong bo\n", NPROC, NLOOPS);
	run_children(sh, NULL);
	printf("   mong doi %ld, thuc te %ld  -> mat %ld lan cap nhat\n\n",
	       expected, sh->counter, expected - sh->counter);

	/* --- Lan 2: co semaphore --- */
	memset(sh, 0, sizeof(*sh));
	sem = sem_open(SEM_NAME, O_CREAT, 0660, 1);	/* gia tri ban dau 1 */
	if (sem == SEM_FAILED) {
		fprintf(stderr, "sem_open that bai: %s\n", strerror(errno));
		munmap(sh, sizeof(*sh));
		shm_unlink(SHM_NAME);
		return 1;
	}

	printf("Lan 2: cung phep thu, co POSIX semaphore bao ve\n");
	run_children(sh, sem);
	printf("   mong doi %ld, thuc te %ld  -> %s\n", expected, sh->counter,
	       sh->counter == expected ? "dung" : "van sai");
	printf("   process ghi cuoi cung: %s\n\n", sh->last_writer);

	printf("Luu y khi trien khai:\n");
	printf("  - shm va sem ton tai doc lap voi process; neu khong unlink,\n"
	       "    chung con lai trong /dev/shm sau khi chuong trinh thoat.\n");
	printf("  - process giu semaphore ma crash se lam process khac cho\n"
	       "    vo han -> can timeout (sem_timedwait) hoac mutex ROBUST.\n");

	sem_close(sem);
	sem_unlink(SEM_NAME);
	munmap(sh, sizeof(*sh));
	shm_unlink(SHM_NAME);
	return 0;
}
