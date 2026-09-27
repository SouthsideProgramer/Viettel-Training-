/* hwctl.h - lop truu tuong "control path" xuong driver Wi-Fi
 *
 * Bao cao muc 16.2/16.4: daemon user space khong dieu khien radio truc tiep.
 * No goi ioctl xuong driver (o day la /dev/ap6sim). Neu module chua duoc nap,
 * lop nay chuyen sang che do mo phong trong process de van test duoc phan con
 * lai cua he thong tren may phat trien.
 */
#ifndef AP6_HWCTL_H
#define AP6_HWCTL_H

#include <stdint.h>

struct hw_radio {
	uint32_t channel;
	uint32_t txpower;
	uint32_t enabled;
	uint32_t clients;
	char     ssid[33];
};

struct hw_counters {
	uint64_t tx_packets;
	uint64_t rx_packets;
	uint64_t tx_bytes;
	uint64_t rx_bytes;
	uint64_t tx_errors;
	uint64_t rx_errors;
	uint64_t irq_count;
};

struct hwctl;

/* Mo backend. Luon tra ve mot handle hop le:
 *   - "driver"    neu mo duoc /dev/ap6sim
 *   - "simulated" neu khong (module chua nap / khong co quyen) */
struct hwctl *hwctl_open(const char *devpath);
void hwctl_close(struct hwctl *hw);

/* Ten backend dang dung, de bao cao trong lenh STATUS. */
const char *hwctl_backend(const struct hwctl *hw);

int hwctl_get_radio(struct hwctl *hw, struct hw_radio *r);
int hwctl_set_radio(struct hwctl *hw, const struct hw_radio *r);
int hwctl_get_counters(struct hwctl *hw, struct hw_counters *c);

#endif /* AP6_HWCTL_H */
