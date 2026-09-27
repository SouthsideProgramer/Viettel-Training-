// SPDX-License-Identifier: GPL-2.0
/*
 * ap6sim.c - character device driver mo phong control path cua mot Wi-Fi AP.
 *
 * Module nay minh hoa cac noi dung o muc 11 cua bao cao thuc tap:
 *   11.2 module kernel va vong doi driver (init/exit doi xung)
 *   11.3 char driver va device node trong /dev
 *   11.4 file_operations: open/read/write/ioctl/poll/release
 *   11.5 concurrency: mutex cho process context, spinlock chia se voi timer
 *   11.7 "interrupt" va deferred work: timer -> workqueue -> wake_up
 *   11.9 device model: class_create/device_create -> /sys/class/ap6sim
 *
 * Day la driver mo phong, khong dieu khien phan cung that. Timer kernel dong
 * vai tro nguon interrupt de sinh event va cap nhat counter, nho vay co the
 * chay thu tren may phat trien x86_64 truoc khi lam viec voi board that.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "ap6sim_uapi.h"

/*
 * Lop tuong thich giua cac phien ban kernel.
 * API kernel KHONG on dinh giua cac ban phat hanh - day la ly do bao cao
 * (muc 11.10) nhac phai doi chieu API voi kernel tree cua BSP dang dung.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 16, 0)
/* from_timer() doi ten thanh timer_container_of() tu kernel 6.16 */
#define ap6sim_timer_owner(p, t, field)	timer_container_of(p, t, field)
#else
#define ap6sim_timer_owner(p, t, field)	from_timer(p, t, field)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
/* del_timer_sync() doi ten thanh timer_delete_sync() tu kernel 6.2 */
#define ap6sim_timer_delete_sync(t)	timer_delete_sync(t)
#else
#define ap6sim_timer_delete_sync(t)	del_timer_sync(t)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
/* no_llseek bi go bo tu kernel 6.12 */
#define AP6SIM_LLSEEK	noop_llseek
#else
#define AP6SIM_LLSEEK	no_llseek
#endif

#define AP6SIM_CLASS_NAME	"ap6sim"
#define AP6SIM_EVT_RING_SIZE	16
#define AP6SIM_TICK_MS		1000

/* Module parameter: cho phep doi cau hinh khi insmod, huu ich khi debug
 * (Bao cao muc 11.2). */
static unsigned int channel = 6;
module_param(channel, uint, 0444);
MODULE_PARM_DESC(channel, "Kenh Wi-Fi khoi tao (mac dinh 6)");

static bool sim_events = true;
module_param(sim_events, bool, 0644);
MODULE_PARM_DESC(sim_events, "Sinh event dinh ky de mo phong interrupt");

struct ap6sim_dev {
	struct cdev		cdev;
	struct class		*class;
	struct device		*device;
	dev_t			devt;

	/* mutex bao ve du lieu cau hinh - truy cap tu process context,
	 * co the ngu (Bao cao muc 11.5). */
	struct mutex		cfg_lock;
	struct ap6sim_radio	radio;

	/* spinlock bao ve ring event va counter - chia se voi timer context,
	 * noi khong duoc phep ngu. */
	spinlock_t		evt_lock;
	u8			evt_ring[AP6SIM_EVT_RING_SIZE];
	unsigned int		evt_head, evt_tail;
	struct ap6sim_counters	counters;

	wait_queue_head_t	readq;	/* process doc bi block se ngu o day */
	struct timer_list	tick;	/* "nguon interrupt" gia lap */
	struct work_struct	work;	/* deferred work: phan viec nang */
	atomic_t		open_count;
};

static struct ap6sim_dev *ap6;

/* ------------------------------------------------------------------ */
/* Event ring - duoc goi tu ca process context lan timer context	*/
/* ------------------------------------------------------------------ */

static void ap6sim_push_event_locked(struct ap6sim_dev *dev, u8 evt)
{
	unsigned int next = (dev->evt_head + 1) % AP6SIM_EVT_RING_SIZE;

	if (next == dev->evt_tail) {
		/* Ring day: bo event cu nhat, giong cach driver bo packet khi
		 * queue day thay vi block interrupt handler. */
		dev->evt_tail = (dev->evt_tail + 1) % AP6SIM_EVT_RING_SIZE;
		dev->counters.rx_errors++;
	}
	dev->evt_ring[dev->evt_head] = evt;
	dev->evt_head = next;
}

static void ap6sim_push_event(struct ap6sim_dev *dev, u8 evt)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->evt_lock, flags);
	ap6sim_push_event_locked(dev, evt);
	spin_unlock_irqrestore(&dev->evt_lock, flags);

	/* Danh thuc process dang ngu trong read()/poll(). */
	wake_up_interruptible(&dev->readq);
}

static bool ap6sim_evt_available(struct ap6sim_dev *dev)
{
	unsigned long flags;
	bool avail;

	spin_lock_irqsave(&dev->evt_lock, flags);
	avail = dev->evt_head != dev->evt_tail;
	spin_unlock_irqrestore(&dev->evt_lock, flags);
	return avail;
}

/* ------------------------------------------------------------------ */
/* "Interrupt" gia lap: timer -> deferred work (Bao cao muc 11.7)	*/
/* ------------------------------------------------------------------ */

/* Phan viec nang chay o process context cua workqueue: duoc phep ngu,
 * duoc phep lay mutex. */
static void ap6sim_work_fn(struct work_struct *w)
{
	struct ap6sim_dev *dev = container_of(w, struct ap6sim_dev, work);
	u32 clients;

	mutex_lock(&dev->cfg_lock);
	clients = dev->radio.clients;
	if (dev->radio.enabled) {
		/* Mo phong so client dao dong quanh 0..4. */
		dev->radio.clients = (clients + 1) % 5;
		clients = dev->radio.clients;
	}
	mutex_unlock(&dev->cfg_lock);

	ap6sim_push_event(dev, clients ? AP6SIM_EVT_STA_CONNECT :
				       AP6SIM_EVT_STA_DISCONNECT);
}

/* "Interrupt handler": chay o softirq context, phai that ngan, khong ngu. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
static void ap6sim_tick(struct timer_list *t)
{
	struct ap6sim_dev *dev = ap6sim_timer_owner(dev, t, tick);
#else
static void ap6sim_tick(unsigned long data)
{
	struct ap6sim_dev *dev = (struct ap6sim_dev *)data;
#endif
	unsigned long flags;

	spin_lock_irqsave(&dev->evt_lock, flags);
	dev->counters.irq_count++;
	dev->counters.rx_packets += 8;
	dev->counters.tx_packets += 5;
	dev->counters.rx_bytes += 8 * 1500;
	dev->counters.tx_bytes += 5 * 1500;
	spin_unlock_irqrestore(&dev->evt_lock, flags);

	/* Chuyen phan viec co the ngu sang workqueue. */
	if (sim_events)
		schedule_work(&dev->work);

	mod_timer(&dev->tick, jiffies + msecs_to_jiffies(AP6SIM_TICK_MS));
}

/* ------------------------------------------------------------------ */
/* file_operations							*/
/* ------------------------------------------------------------------ */

static int ap6sim_open(struct inode *inode, struct file *filp)
{
	struct ap6sim_dev *dev = container_of(inode->i_cdev,
					      struct ap6sim_dev, cdev);

	filp->private_data = dev;
	atomic_inc(&dev->open_count);
	pr_debug("ap6sim: open (open_count=%d)\n",
		 atomic_read(&dev->open_count));
	return 0;
}

static int ap6sim_release(struct inode *inode, struct file *filp)
{
	struct ap6sim_dev *dev = filp->private_data;

	atomic_dec(&dev->open_count);
	return 0;
}

/*
 * read(): tra ve mot dong text mo ta event.
 * Neu chua co event: process ngu tren wait queue thay vi busy-wait
 * (Bao cao muc 11.4). O(non-blocking) thi tra ve -EAGAIN.
 */
static ssize_t ap6sim_read(struct file *filp, char __user *ubuf, size_t count,
			   loff_t *ppos)
{
	struct ap6sim_dev *dev = filp->private_data;
	char line[64];
	unsigned long flags;
	u8 evt = AP6SIM_EVT_NONE;
	int len;

	if (!ap6sim_evt_available(dev)) {
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(dev->readq,
					     ap6sim_evt_available(dev)))
			return -ERESTARTSYS;	/* bi signal danh thuc */
	}

	spin_lock_irqsave(&dev->evt_lock, flags);
	if (dev->evt_head != dev->evt_tail) {
		evt = dev->evt_ring[dev->evt_tail];
		dev->evt_tail = (dev->evt_tail + 1) % AP6SIM_EVT_RING_SIZE;
	}
	spin_unlock_irqrestore(&dev->evt_lock, flags);

	len = scnprintf(line, sizeof(line), "EVENT %u\n", evt);
	if (count < (size_t)len)
		return -EINVAL;

	/* Khong bao gio dereference con tro user truc tiep. */
	if (copy_to_user(ubuf, line, len))
		return -EFAULT;

	*ppos += len;
	return len;
}

/*
 * write(): nhan lenh text dang "channel <n>" hoac "ssid <ten>".
 * Day la duong cau hinh don gian giong cach nhieu driver dung debugfs/procfs.
 */
static ssize_t ap6sim_write(struct file *filp, const char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	struct ap6sim_dev *dev = filp->private_data;
	char kbuf[80];
	unsigned int val;
	size_t len = min(count, sizeof(kbuf) - 1);

	if (copy_from_user(kbuf, ubuf, len))
		return -EFAULT;
	kbuf[len] = '\0';

	if (sscanf(kbuf, "channel %u", &val) == 1) {
		if (val > 196)
			return -EINVAL;
		mutex_lock(&dev->cfg_lock);
		dev->radio.channel = val;
		mutex_unlock(&dev->cfg_lock);
		ap6sim_push_event(dev, AP6SIM_EVT_CHANNEL_CHANGED);
	} else if (strncmp(kbuf, "ssid ", 5) == 0) {
		char *nl;

		mutex_lock(&dev->cfg_lock);
		strscpy(dev->radio.ssid, kbuf + 5, sizeof(dev->radio.ssid));
		nl = strchr(dev->radio.ssid, '\n');
		if (nl)
			*nl = '\0';
		mutex_unlock(&dev->cfg_lock);
	} else {
		return -EINVAL;
	}

	*ppos += count;
	return count;
}

static __poll_t ap6sim_poll(struct file *filp, poll_table *wait)
{
	struct ap6sim_dev *dev = filp->private_data;
	__poll_t mask = EPOLLOUT | EPOLLWRNORM;

	poll_wait(filp, &dev->readq, wait);
	if (ap6sim_evt_available(dev))
		mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

static long ap6sim_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ap6sim_dev *dev = filp->private_data;
	struct ap6sim_counters cnt;
	struct ap6sim_radio radio;
	unsigned long flags;
	u32 evt;

	/* Kiem tra magic va so lenh truoc khi lam bat cu viec gi. */
	if (_IOC_TYPE(cmd) != AP6SIM_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > AP6SIM_IOC_MAXNR)
		return -ENOTTY;

	switch (cmd) {
	case AP6SIM_IOC_GET_RADIO:
		mutex_lock(&dev->cfg_lock);
		radio = dev->radio;
		mutex_unlock(&dev->cfg_lock);
		if (copy_to_user((void __user *)arg, &radio, sizeof(radio)))
			return -EFAULT;
		return 0;

	case AP6SIM_IOC_SET_RADIO:
		if (copy_from_user(&radio, (void __user *)arg, sizeof(radio)))
			return -EFAULT;
		if (radio.channel > 196 || radio.txpower > 100 ||
		    radio.txpower == 0)
			return -EINVAL;
		radio.ssid[AP6SIM_SSID_LEN - 1] = '\0';
		mutex_lock(&dev->cfg_lock);
		/* Giu nguyen counter clients do driver quan ly. */
		radio.clients = dev->radio.clients;
		dev->radio = radio;
		mutex_unlock(&dev->cfg_lock);
		ap6sim_push_event(dev, AP6SIM_EVT_CHANNEL_CHANGED);
		return 0;

	case AP6SIM_IOC_GET_COUNTERS:
		spin_lock_irqsave(&dev->evt_lock, flags);
		cnt = dev->counters;
		spin_unlock_irqrestore(&dev->evt_lock, flags);
		if (copy_to_user((void __user *)arg, &cnt, sizeof(cnt)))
			return -EFAULT;
		return 0;

	case AP6SIM_IOC_RESET:
		spin_lock_irqsave(&dev->evt_lock, flags);
		memset(&dev->counters, 0, sizeof(dev->counters));
		dev->evt_head = dev->evt_tail = 0;
		spin_unlock_irqrestore(&dev->evt_lock, flags);
		return 0;

	case AP6SIM_IOC_TRIGGER_EVT:
		if (copy_from_user(&evt, (void __user *)arg, sizeof(evt)))
			return -EFAULT;
		if (evt > AP6SIM_EVT_RADAR_DETECTED)
			return -EINVAL;
		ap6sim_push_event(dev, (u8)evt);
		return 0;
	}
	return -ENOTTY;
}

static const struct file_operations ap6sim_fops = {
	.owner		= THIS_MODULE,
	.open		= ap6sim_open,
	.release	= ap6sim_release,
	.read		= ap6sim_read,
	.write		= ap6sim_write,
	.poll		= ap6sim_poll,
	.unlocked_ioctl	= ap6sim_ioctl,
	.compat_ioctl	= ap6sim_ioctl,
	.llseek		= AP6SIM_LLSEEK,
};

/* ------------------------------------------------------------------ */
/* procfs: quan sat trang thai tu shell (Bao cao muc 11.10)		*/
/* ------------------------------------------------------------------ */

static int ap6sim_proc_show(struct seq_file *m, void *v)
{
	struct ap6sim_dev *dev = ap6;
	struct ap6sim_counters cnt;
	struct ap6sim_radio radio;
	unsigned long flags;

	mutex_lock(&dev->cfg_lock);
	radio = dev->radio;
	mutex_unlock(&dev->cfg_lock);

	spin_lock_irqsave(&dev->evt_lock, flags);
	cnt = dev->counters;
	spin_unlock_irqrestore(&dev->evt_lock, flags);

	seq_printf(m, "ssid       : %s\n", radio.ssid);
	seq_printf(m, "channel    : %u\n", radio.channel);
	seq_printf(m, "txpower    : %u\n", radio.txpower);
	seq_printf(m, "enabled    : %u\n", radio.enabled);
	seq_printf(m, "clients    : %u\n", radio.clients);
	seq_printf(m, "irq_count  : %llu\n", cnt.irq_count);
	seq_printf(m, "tx_packets : %llu\n", cnt.tx_packets);
	seq_printf(m, "rx_packets : %llu\n", cnt.rx_packets);
	seq_printf(m, "tx_bytes   : %llu\n", cnt.tx_bytes);
	seq_printf(m, "rx_bytes   : %llu\n", cnt.rx_bytes);
	return 0;
}

static int ap6sim_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ap6sim_proc_show, NULL);
}

static const struct proc_ops ap6sim_proc_ops = {
	.proc_open	= ap6sim_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

/* ------------------------------------------------------------------ */
/* Vong doi module: init/exit phai doi xung (Bao cao muc 11.2)		*/
/* ------------------------------------------------------------------ */

static int __init ap6sim_init(void)
{
	int ret;

	ap6 = kzalloc(sizeof(*ap6), GFP_KERNEL);
	if (!ap6)
		return -ENOMEM;

	mutex_init(&ap6->cfg_lock);
	spin_lock_init(&ap6->evt_lock);
	init_waitqueue_head(&ap6->readq);
	INIT_WORK(&ap6->work, ap6sim_work_fn);
	atomic_set(&ap6->open_count, 0);

	ap6->radio.channel = channel;
	ap6->radio.txpower = 100;
	ap6->radio.enabled = 1;
	strscpy(ap6->radio.ssid, "Viettel_AP6", sizeof(ap6->radio.ssid));

	ret = alloc_chrdev_region(&ap6->devt, 0, 1, AP6SIM_DEV_NAME);
	if (ret < 0)
		goto err_free;

	cdev_init(&ap6->cdev, &ap6sim_fops);
	ap6->cdev.owner = THIS_MODULE;
	ret = cdev_add(&ap6->cdev, ap6->devt, 1);
	if (ret < 0)
		goto err_chrdev;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	ap6->class = class_create(AP6SIM_CLASS_NAME);
#else
	ap6->class = class_create(THIS_MODULE, AP6SIM_CLASS_NAME);
#endif
	if (IS_ERR(ap6->class)) {
		ret = PTR_ERR(ap6->class);
		goto err_cdev;
	}

	/* device_create -> udev tao /dev/ap6sim (Bao cao muc 11.9). */
	ap6->device = device_create(ap6->class, NULL, ap6->devt, NULL,
				    AP6SIM_DEV_NAME);
	if (IS_ERR(ap6->device)) {
		ret = PTR_ERR(ap6->device);
		goto err_class;
	}

	if (!proc_create(AP6SIM_DEV_NAME, 0444, NULL, &ap6sim_proc_ops)) {
		ret = -ENOMEM;
		goto err_device;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
	timer_setup(&ap6->tick, ap6sim_tick, 0);
#else
	setup_timer(&ap6->tick, ap6sim_tick, (unsigned long)ap6);
#endif
	mod_timer(&ap6->tick, jiffies + msecs_to_jiffies(AP6SIM_TICK_MS));

	pr_info("ap6sim: nap thanh cong, major=%d minor=%d channel=%u\n",
		MAJOR(ap6->devt), MINOR(ap6->devt), channel);
	return 0;

	/* Duong xu ly loi giai phong dung theo thu tu nguoc lai. */
err_device:
	device_destroy(ap6->class, ap6->devt);
err_class:
	class_destroy(ap6->class);
err_cdev:
	cdev_del(&ap6->cdev);
err_chrdev:
	unregister_chrdev_region(ap6->devt, 1);
err_free:
	kfree(ap6);
	ap6 = NULL;
	return ret;
}

static void __exit ap6sim_exit(void)
{
	/* Dung nguon su kien truoc, sau do moi thao go tai nguyen. */
	ap6sim_timer_delete_sync(&ap6->tick);
	cancel_work_sync(&ap6->work);

	remove_proc_entry(AP6SIM_DEV_NAME, NULL);
	device_destroy(ap6->class, ap6->devt);
	class_destroy(ap6->class);
	cdev_del(&ap6->cdev);
	unregister_chrdev_region(ap6->devt, 1);
	mutex_destroy(&ap6->cfg_lock);
	kfree(ap6);
	ap6 = NULL;

	pr_info("ap6sim: da go module\n");
}

module_init(ap6sim_init);
module_exit(ap6sim_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bao cao thuc tap - Viettel High Tech");
MODULE_DESCRIPTION("Char driver mo phong control path cua Wi-Fi AP (AP 6)");
MODULE_VERSION("1.0");
