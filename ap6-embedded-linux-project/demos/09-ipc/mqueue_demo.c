/*
 * mqueue_demo.c - POSIX message queue (Bao cao muc 9.3).
 *
 * Khac voi pipe (dong byte lien tuc), message queue giu RANH GIOI tung
 * message va co do uu tien. Rat hop de gui command/event co cau truc cho
 * daemon.
 *
 * Bien dich: gcc -Wall -o mqueue_demo mqueue_demo.c -lrt
 * Quan sat  : ls /dev/mqueue/
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define MQ_NAME		"/ap6_demo_mq"
#define MSG_MAX		128
#define MSG_COUNT	5

/* Cau truc lenh gui xuong daemon quan ly Wi-Fi. */
struct ap_command {
	int  id;
	char key[32];
	char value[64];
};

static void child_sender(void)
{
	static const struct { const char *k, *v; unsigned prio; } cmds[] = {
		{ "SSID",        "Viettel_Lab", 1 },
		{ "Channel",     "11",          1 },
		{ "TxPower",     "80",          1 },
		{ "EMERGENCY",   "radar_detected", 9 },	/* uu tien cao nhat */
		{ "BeaconPeriod", "100",        1 },
	};
	mqd_t mq;
	size_t i;

	mq = mq_open(MQ_NAME, O_WRONLY);
	if (mq == (mqd_t)-1) {
		perror("   [con] mq_open");
		_exit(1);
	}

	for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
		struct ap_command c;

		memset(&c, 0, sizeof(c));
		c.id = (int)i + 1;
		snprintf(c.key, sizeof(c.key), "%s", cmds[i].k);
		snprintf(c.value, sizeof(c.value), "%s", cmds[i].v);

		if (mq_send(mq, (const char *)&c, sizeof(c), cmds[i].prio) < 0)
			perror("   [con] mq_send");
		else
			printf("   [gui]  #%d %-12s = %-16s (prio=%u)\n", c.id,
			       c.key, c.value, cmds[i].prio);
	}
	mq_close(mq);
	_exit(0);
}

int main(void)
{
	struct mq_attr attr;
	mqd_t mq;
	pid_t pid;
	int i;

	printf("=== IPC: POSIX message queue ===\n\n");

	memset(&attr, 0, sizeof(attr));
	attr.mq_maxmsg = 10;			/* so message toi da trong queue */
	attr.mq_msgsize = MSG_MAX;		/* kich thuoc toi da moi message */

	mq_unlink(MQ_NAME);
	mq = mq_open(MQ_NAME, O_CREAT | O_RDONLY, 0660, &attr);
	if (mq == (mqd_t)-1) {
		fprintf(stderr, "mq_open that bai: %s\n", strerror(errno));
		fprintf(stderr,
			"(mot so he thong can mount /dev/mqueue truoc)\n");
		return 1;
	}
	printf("Da tao queue %s (maxmsg=%ld, msgsize=%ld)\n\n", MQ_NAME,
	       attr.mq_maxmsg, attr.mq_msgsize);

	pid = fork();
	if (pid == 0)
		child_sender();

	sleep(1);	/* de process con gui het truoc khi nhan */

	printf("\n   Nhan (queue tra message uu tien cao truoc):\n");
	for (i = 0; i < MSG_COUNT; i++) {
		char buf[MSG_MAX];
		struct ap_command c;
		unsigned prio = 0;
		ssize_t n;

		n = mq_receive(mq, buf, sizeof(buf), &prio);
		if (n < 0) {
			perror("   mq_receive");
			break;
		}
		/* Ranh gioi message duoc giu nguyen: doc mot lan ra dung mot
		 * struct, khong phai tu ghep tu dong byte nhu pipe. */
		memcpy(&c, buf, sizeof(c));
		printf("   [nhan] #%d %-12s = %-16s (prio=%u, %zd byte)\n",
		       c.id, c.key, c.value, prio, n);
	}

	waitpid(pid, NULL, 0);
	mq_close(mq);
	mq_unlink(MQ_NAME);		/* khong unlink -> queue con lai sau khi thoat */

	printf("\n-> Message queue hop cho command/event roi rac; khong nen\n"
	       "   dung de truyen buffer lon (dung shared memory - muc 9.4).\n");
	return 0;
}
