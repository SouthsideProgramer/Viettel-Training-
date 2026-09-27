/* stats.h - vung telemetry dung chung giua cac process
 *
 * Bao cao muc 6.4 va 9.4: shared memory (shm_open + mmap) cho phep nhieu
 * process cung nhin mot vung nho vat ly, khong phai copy qua kernel moi lan
 * doc. Nhung shared memory KHONG tu dong bo -> phai kem mutex process-shared
 * (Bao cao muc 9.5).
 */
#ifndef AP6_STATS_H
#define AP6_STATS_H

#include <pthread.h>
#include <stdint.h>

#define AP6_SHM_NAME	"/ap6_stats"
#define AP6_STATS_MAGIC	0x41503653u	/* "AP6S" */

struct ap6_stats {
	uint32_t magic;
	uint32_t version;
	pthread_mutex_t lock;		/* PTHREAD_PROCESS_SHARED */
	uint64_t seq;			/* tang moi lan cap nhat */
	uint64_t uptime_s;
	uint32_t clients;		/* so station dang ket noi */
	uint32_t channel;
	uint32_t txpower;
	int32_t  noise_dbm;
	uint64_t tx_bytes;
	uint64_t rx_bytes;
	uint64_t tx_errors;
	uint64_t rx_errors;
	char     ssid[33];
	char     ifname[16];
};

/* Tao (hoac tao lai) vung shm; chi daemon goi ham nay. */
struct ap6_stats *stats_create(const char *name);

/* Mo vung shm da ton tai o che do doc-ghi; process quan sat goi ham nay. */
struct ap6_stats *stats_open(const char *name);

void stats_close(struct ap6_stats *st);

/* Huy vung shm khoi /dev/shm (goi khi daemon thoat). */
void stats_unlink(const char *name);

#endif /* AP6_STATS_H */
