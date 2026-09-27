/*
 * telemetry.c - worker thread lay so lieu tu driver va cap nhat shared memory.
 *
 * Bao cao muc 8.1/8.5: thread chia se khong gian dia chi voi main loop nen
 * doc profile rat re, nhung moi truy cap du lieu dung chung deu phai qua
 * mutex (muc 8.3).
 * Bao cao muc 8.4: thay vi sleep(1) roi kiem tra co, thread ngu tren
 * pthread_cond_timedwait de thoat ngay khi daemon nhan SIGTERM.
 * Bao cao muc 9.4/9.5: ket qua duoc ghi vao shm co mutex process-shared de
 * process khac (apmon) doc duoc snapshot nhat quan.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "apd.h"
#include "../common/log.h"
#include "../common/util.h"

#define TELEMETRY_PERIOD_MS	500

static void update_stats(struct apd_ctx *ctx)
{
	struct hw_counters cnt;
	struct hw_radio radio;
	char ssid[33];
	uint64_t uptime;

	if (hwctl_get_radio(ctx->hw, &radio) < 0)
		return;
	if (hwctl_get_counters(ctx->hw, &cnt) < 0)
		return;

	/* Doc SSID tu profile - du lieu dung chung voi main loop. */
	pthread_mutex_lock(&ctx->lock);
	snprintf(ssid, sizeof(ssid), "%s",
		 profile_get(&ctx->profile, "SSID") ?: "");
	pthread_mutex_unlock(&ctx->lock);

	uptime = (uint64_t)(monotonic_now() - ctx->start_time);

	/* Critical section thu hai: vung shm chia se voi process khac. */
	pthread_mutex_lock(&ctx->stats->lock);
	ctx->stats->seq++;
	ctx->stats->uptime_s = uptime;
	ctx->stats->clients = radio.clients;
	ctx->stats->channel = radio.channel;
	ctx->stats->txpower = radio.txpower;
	ctx->stats->noise_dbm = -95 + (int32_t)(ctx->stats->seq % 7);
	ctx->stats->tx_bytes = cnt.tx_bytes;
	ctx->stats->rx_bytes = cnt.rx_bytes;
	ctx->stats->tx_errors = cnt.tx_errors;
	ctx->stats->rx_errors = cnt.rx_errors;
	snprintf(ctx->stats->ssid, sizeof(ctx->stats->ssid), "%s", ssid);
	snprintf(ctx->stats->ifname, sizeof(ctx->stats->ifname), "ra0");
	pthread_mutex_unlock(&ctx->stats->lock);
}

static void *telemetry_thread(void *arg)
{
	struct apd_ctx *ctx = arg;

	log_info("thread telemetry bat dau (chu ky %d ms)",
		 TELEMETRY_PERIOD_MS);

	for (;;) {
		struct timespec ts;
		int stop;

		update_stats(ctx);

		/* Cho co dieu kien "telemetry_stop" hoac het timeout. */
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_nsec += (long)TELEMETRY_PERIOD_MS * 1000000L;
		if (ts.tv_nsec >= 1000000000L) {
			ts.tv_sec += ts.tv_nsec / 1000000000L;
			ts.tv_nsec %= 1000000000L;
		}

		pthread_mutex_lock(&ctx->lock);
		while (!ctx->telemetry_stop) {
			int rc = pthread_cond_timedwait(&ctx->cond, &ctx->lock,
							&ts);
			if (rc == ETIMEDOUT)
				break;
			if (rc != 0)
				break;
		}
		stop = ctx->telemetry_stop;
		pthread_mutex_unlock(&ctx->lock);

		if (stop)
			break;
	}

	log_info("thread telemetry ket thuc");
	return NULL;
}

int telemetry_start(struct apd_ctx *ctx)
{
	int rc;

	ctx->telemetry_stop = 0;
	rc = pthread_create(&ctx->telemetry_tid, NULL, telemetry_thread, ctx);
	if (rc != 0) {
		log_err("pthread_create that bai: %s", strerror(rc));
		return -1;
	}
	ctx->telemetry_started = 1;
	return 0;
}

void telemetry_stop(struct apd_ctx *ctx)
{
	if (!ctx->telemetry_started)
		return;

	pthread_mutex_lock(&ctx->lock);
	ctx->telemetry_stop = 1;
	pthread_cond_broadcast(&ctx->cond);
	pthread_mutex_unlock(&ctx->lock);

	/* pthread_join: dam bao thread khong con dung shm truoc khi munmap
	 * (Bao cao muc 8.1). */
	pthread_join(ctx->telemetry_tid, NULL);
	ctx->telemetry_started = 0;
}
