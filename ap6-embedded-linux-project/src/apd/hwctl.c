#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "hwctl.h"
#include "../common/log.h"
#include "../common/util.h"
#include "../../kernel/ap6sim/ap6sim_uapi.h"

struct hwctl {
	int fd;				/* < 0 -> che do mo phong */
	struct hw_radio sim_radio;	/* trang thai khi khong co driver */
	struct hw_counters sim_cnt;
};

struct hwctl *hwctl_open(const char *devpath)
{
	struct hwctl *hw = calloc(1, sizeof(*hw));

	if (!hw)
		return NULL;

	hw->fd = open(devpath, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (hw->fd < 0) {
		log_warn("khong mo duoc %s (%s) -> dung backend mo phong",
			 devpath, strerror(errno));
		hw->sim_radio.channel = 6;
		hw->sim_radio.txpower = 100;
		hw->sim_radio.enabled = 1;
		snprintf(hw->sim_radio.ssid, sizeof(hw->sim_radio.ssid),
			 "Viettel_AP6");
	} else {
		log_info("mo %s thanh cong -> control path di qua driver",
			 devpath);
	}
	return hw;
}

void hwctl_close(struct hwctl *hw)
{
	if (!hw)
		return;
	if (hw->fd >= 0)
		close(hw->fd);
	free(hw);
}

const char *hwctl_backend(const struct hwctl *hw)
{
	return (hw && hw->fd >= 0) ? "driver(/dev/ap6sim)" : "simulated";
}

int hwctl_get_radio(struct hwctl *hw, struct hw_radio *r)
{
	struct ap6sim_radio kr;

	if (hw->fd < 0) {
		*r = hw->sim_radio;
		return 0;
	}
	if (ioctl(hw->fd, AP6SIM_IOC_GET_RADIO, &kr) < 0) {
		log_err("ioctl GET_RADIO that bai: %s", strerror(errno));
		return -1;
	}
	r->channel = kr.channel;
	r->txpower = kr.txpower;
	r->enabled = kr.enabled;
	r->clients = kr.clients;
	snprintf(r->ssid, sizeof(r->ssid), "%s", kr.ssid);
	return 0;
}

int hwctl_set_radio(struct hwctl *hw, const struct hw_radio *r)
{
	struct ap6sim_radio kr;

	if (hw->fd < 0) {
		hw->sim_radio.channel = r->channel;
		hw->sim_radio.txpower = r->txpower;
		hw->sim_radio.enabled = r->enabled;
		snprintf(hw->sim_radio.ssid, sizeof(hw->sim_radio.ssid), "%s",
			 r->ssid);
		return 0;
	}

	memset(&kr, 0, sizeof(kr));
	kr.channel = r->channel;
	kr.txpower = r->txpower;
	kr.enabled = r->enabled;
	snprintf(kr.ssid, sizeof(kr.ssid), "%s", r->ssid);

	if (ioctl(hw->fd, AP6SIM_IOC_SET_RADIO, &kr) < 0) {
		log_err("ioctl SET_RADIO that bai: %s", strerror(errno));
		return -1;
	}
	return 0;
}

int hwctl_get_counters(struct hwctl *hw, struct hw_counters *c)
{
	struct ap6sim_counters kc;

	if (hw->fd < 0) {
		/* Sinh so lieu tang dan de phan telemetry van co du lieu.
		 * So client dao dong 0..4 giong hanh vi cua module ap6sim. */
		hw->sim_radio.clients = (uint32_t)((hw->sim_cnt.irq_count + 1) % 5);
		hw->sim_cnt.irq_count++;
		hw->sim_cnt.rx_packets += 8;
		hw->sim_cnt.tx_packets += 5;
		hw->sim_cnt.rx_bytes += 8 * 1500;
		hw->sim_cnt.tx_bytes += 5 * 1500;
		*c = hw->sim_cnt;
		return 0;
	}
	if (ioctl(hw->fd, AP6SIM_IOC_GET_COUNTERS, &kc) < 0) {
		log_err("ioctl GET_COUNTERS that bai: %s", strerror(errno));
		return -1;
	}
	c->tx_packets = kc.tx_packets;
	c->rx_packets = kc.rx_packets;
	c->tx_bytes = kc.tx_bytes;
	c->rx_bytes = kc.rx_bytes;
	c->tx_errors = kc.tx_errors;
	c->rx_errors = kc.rx_errors;
	c->irq_count = kc.irq_count;
	return 0;
}
