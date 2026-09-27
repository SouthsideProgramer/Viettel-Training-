/* SPDX-License-Identifier: GPL-2.0 */
/* ap6sim_uapi.h - giao dien dung chung giua user space va kernel module
 *
 * Bao cao muc 11.4: user space khong dieu khien phan cung truc tiep, ma goi
 * open/read/write/ioctl tren device node; VFS chuyen loi goi den callback
 * trong file_operations cua driver.
 *
 * File nay duoc include ca o kernel (module) va o user space (daemon apd),
 * giong cach cac uapi header cua kernel Linux hoat dong.
 */
#ifndef AP6SIM_UAPI_H
#define AP6SIM_UAPI_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <linux/types.h>
#include <sys/ioctl.h>
#endif

#define AP6SIM_DEV_NAME		"ap6sim"
#define AP6SIM_DEV_PATH		"/dev/" AP6SIM_DEV_NAME
#define AP6SIM_SSID_LEN		33

/* Trang thai radio ma "driver" dang giu. */
struct ap6sim_radio {
	__u32 channel;		/* 1..14 cho 2.4G, 36..165 cho 5G */
	__u32 txpower;		/* 1..100 (%) */
	__u32 enabled;		/* 0 = radio off, 1 = radio on */
	__u32 clients;		/* so station dang associate (mo phong) */
	char  ssid[AP6SIM_SSID_LEN];
	char  _pad[3];
};

/* Counter cua data path (Bao cao muc 11.8: DMA / queue). */
struct ap6sim_counters {
	__u64 tx_packets;
	__u64 rx_packets;
	__u64 tx_bytes;
	__u64 rx_bytes;
	__u64 tx_errors;
	__u64 rx_errors;
	__u64 irq_count;	/* so lan "interrupt" gia lap da chay */
};

#define AP6SIM_IOC_MAGIC	'A'

#define AP6SIM_IOC_GET_RADIO	_IOR(AP6SIM_IOC_MAGIC, 1, struct ap6sim_radio)
#define AP6SIM_IOC_SET_RADIO	_IOW(AP6SIM_IOC_MAGIC, 2, struct ap6sim_radio)
#define AP6SIM_IOC_GET_COUNTERS	_IOR(AP6SIM_IOC_MAGIC, 3, struct ap6sim_counters)
#define AP6SIM_IOC_RESET	_IO(AP6SIM_IOC_MAGIC, 4)
/* Sinh mot event de user space dang doc (blocking read) duoc danh thuc:
 * mo phong interrupt -> wake_up wait queue (Bao cao muc 11.4, 11.7). */
#define AP6SIM_IOC_TRIGGER_EVT	_IOW(AP6SIM_IOC_MAGIC, 5, __u32)
#define AP6SIM_IOC_MAXNR	5

/* Ma event tra ve qua read(). */
enum ap6sim_event {
	AP6SIM_EVT_NONE = 0,
	AP6SIM_EVT_STA_CONNECT,
	AP6SIM_EVT_STA_DISCONNECT,
	AP6SIM_EVT_CHANNEL_CHANGED,
	AP6SIM_EVT_RADAR_DETECTED,
};

#endif /* AP6SIM_UAPI_H */
