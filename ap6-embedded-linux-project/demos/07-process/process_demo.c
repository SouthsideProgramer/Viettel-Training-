/*
 * process_demo.c - process trong Linux (Bao cao muc 7.1 - 7.5).
 *
 *   1. fork(): hai process, hai gia tri tra ve khac nhau
 *   2. copy-on-write: bien toan cuc khong duoc chia se sau fork
 *   3. fork + exec + waitpid: mo hinh shell chay mot lenh
 *   4. zombie: con ket thuc nhung cha chua wait
 *   5. orphan: cha ket thuc truoc con -> con duoc init/systemd nhan nuoi
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static int g_counter = 100;	/* bien toan cuc: kiem chung copy-on-write */

static void muc_1_fork(void)
{
	pid_t pid;

	printf("1) fork(): tao process con\n");
	printf("   truoc fork: pid=%ld ppid=%ld\n", (long)getpid(),
	       (long)getppid());

	pid = fork();
	if (pid < 0) {
		perror("   fork");
		return;
	}

	if (pid == 0) {
		printf("   [con]  fork() tra ve 0, pid=%ld, ppid=%ld\n",
		       (long)getpid(), (long)getppid());
		/* Process con thua huong ca BUFFER stdout cua cha. Khi stdout
		 * la pipe/file, buffer o che do day khoi; _exit() khong flush
		 * nen dong printf o tren se bien mat. Phai fflush truoc. */
		fflush(stdout);
		_exit(0);
	}

	printf("   [cha]  fork() tra ve %ld (= PID con)\n", (long)pid);
	waitpid(pid, NULL, 0);
	printf("\n");
}

static void muc_2_copy_on_write(void)
{
	pid_t pid;
	int status;

	printf("2) Khong gian dia chi rieng + copy-on-write\n");
	printf("   truoc fork : g_counter = %d\n", g_counter);

	pid = fork();
	if (pid == 0) {
		g_counter += 900;	/* ghi -> kernel copy trang rieng */
		printf("   [con]  g_counter = %d (chi doi trong process con)\n",
		       g_counter);
		fflush(stdout);
		_exit(3);
	}
	waitpid(pid, &status, 0);
	printf("   [cha]  g_counter = %d (khong bi con lam thay doi)\n",
	       g_counter);
	if (WIFEXITED(status))
		printf("   [cha]  exit status cua con = %d\n",
		       WEXITSTATUS(status));
	printf("   -> muon chia se du lieu that su phai dung IPC (muc 9).\n\n");
}

static void muc_3_fork_exec(void)
{
	pid_t pid;
	int status;

	printf("3) fork + exec + waitpid (cach shell chay mot lenh)\n");
	pid = fork();
	if (pid < 0) {
		perror("   fork");
		return;
	}
	if (pid == 0) {
		printf("   [con]  pid=%ld, chuan bi exec 'uname -sr'\n",
		       (long)getpid());
		fflush(stdout);
		/* exec KHONG tao process moi: PID giu nguyen, chi thay toan bo
		 * text/data/heap/stack bang image moi (Bao cao muc 7.4). */
		execlp("uname", "uname", "-sr", (char *)NULL);
		perror("   execlp");	/* chi chay khi exec that bai */
		_exit(127);
	}

	waitpid(pid, &status, 0);
	printf("   [cha]  con %ld ket thuc voi exit=%d\n", (long)pid,
	       WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	printf("   -> neu shell exec truc tiep, chinh shell se bi thay the.\n\n");
}

static void muc_4_zombie(void)
{
	char cmd[128];
	pid_t pid;

	printf("4) Zombie process\n");
	pid = fork();
	if (pid == 0)
		_exit(0);		/* con ket thuc ngay */

	sleep(1);			/* cha co tinh chua wait */
	snprintf(cmd, sizeof(cmd), "ps -o pid=,stat=,comm= -p %ld 2>/dev/null",
		 (long)pid);
	printf("   trang thai con khi cha chua wait (Z = zombie):\n   ");
	fflush(stdout);
	if (system(cmd) != 0)
		printf("   (khong chay duoc ps)\n");

	waitpid(pid, NULL, 0);		/* thu hoi -> zombie bien mat */
	printf("   sau waitpid(): entry trong bang process da duoc giai phong\n");
	printf("   -> daemon tao nhieu con ma khong wait se tich luy zombie\n"
	       "      (Bao cao muc 10.4 - xu ly SIGCHLD).\n\n");
}

static void muc_5_orphan(void)
{
	pid_t pid;

	printf("5) Orphan process\n");
	pid = fork();
	if (pid == 0) {
		pid_t old_ppid = getppid();

		sleep(2);		/* cha se thoat truoc */
		printf("   [con]  ppid truoc = %ld, ppid sau = %ld\n",
		       (long)old_ppid, (long)getppid());
		printf("   [con]  -> da duoc init/systemd (PID 1 hoac reaper)"
		       " nhan nuoi\n");
		fflush(stdout);
		_exit(0);
	}

	/* Process cha "thoat" bang cach tach ra: o day mo phong bang cach
	 * khong wait va de con tiep tuc chay. */
	printf("   [cha]  khong wait, con %ld se thanh orphan\n", (long)pid);
	sleep(3);
	printf("   -> daemon dung dung mo hinh nay: fork roi cha thoat.\n");
}

int main(void)
{
	printf("=== Process trong Linux ===\n\n");
	muc_1_fork();
	muc_2_copy_on_write();
	muc_3_fork_exec();
	muc_4_zombie();
	muc_5_orphan();
	return 0;
}
