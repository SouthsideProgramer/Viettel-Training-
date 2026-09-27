/*
 * apmon.c - process quan sat telemetry qua shared memory.
 *
 * Bao cao muc 9.4: apmon KHONG ket noi socket den apd. No mmap cung vung nho
 * vat ly ma apd dang ghi, nen viec doc so lieu khong ton them lan copy nao
 * qua kernel. Doi lai, hai process phai thong nhat mutex process-shared
 * (muc 9.5) va layout struct.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "../common/stats.h"

static volatile sig_atomic_t g_stop;

static void on_signal(int signo)
{
	(void)signo;
	g_stop = 1;
}

int main(int argc, char **argv)
{
	struct ap6_stats *st;
	struct sigaction sa;
	int count = 0, limit = 0;
	unsigned interval_ms = 1000;

	if (argc > 1)
		limit = atoi(argv[1]);		/* so lan doc, 0 = vo han */
	if (argc > 2)
		interval_ms = (unsigned)atoi(argv[2]);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	st = stats_open(AP6_SHM_NAME);
	if (!st) {
		fprintf(stderr,
			"khong mo duoc %s: %s\n"
			"(apd da chay chua? shm chi ton tai khi daemon song)\n",
			AP6_SHM_NAME, strerror(errno));
		return 1;
	}

	printf("%-6s %-18s %-4s %-4s %-7s %-6s %-12s %-12s\n", "seq", "ssid",
	       "ch", "pwr", "clients", "noise", "tx_bytes", "rx_bytes");

	while (!g_stop && (limit == 0 || count < limit)) {
		struct ap6_stats snap;
		int rc;

		/* Lay lock, copy snapshot, nha lock ngay: khong giu lock trong
		 * luc printf (Bao cao muc 8.3 - critical section phai ngan). */
		rc = pthread_mutex_lock(&st->lock);
		if (rc == EOWNERDEAD) {
			/* Process giu lock da chet -> khoi phuc trang thai. */
			fprintf(stderr, "canh bao: chu so huu mutex da chet\n");
			pthread_mutex_consistent(&st->lock);
		} else if (rc != 0) {
			fprintf(stderr, "lock that bai: %s\n", strerror(rc));
			break;
		}
		snap = *st;
		pthread_mutex_unlock(&st->lock);

		printf("%-6llu %-18s %-4u %-4u %-7u %-6d %-12llu %-12llu\n",
		       (unsigned long long)snap.seq, snap.ssid, snap.channel,
		       snap.txpower, snap.clients, snap.noise_dbm,
		       (unsigned long long)snap.tx_bytes,
		       (unsigned long long)snap.rx_bytes);
		fflush(stdout);

		count++;
		if (limit != 0 && count >= limit)
			break;
		usleep(interval_ms * 1000);
	}

	stats_close(st);
	return 0;
}
