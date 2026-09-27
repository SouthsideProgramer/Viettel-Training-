# Dự án minh họa kèm Báo cáo thực tập — Embedded Linux (Viettel High Tech)

Mã nguồn thực hành đi kèm báo cáo thực tập *"Basic C và Linux Programming cho
Embedded Linux"*. Mỗi phần của báo cáo được cụ thể hóa bằng chương trình C chạy
được, có thể biên dịch, chạy và chụp lại kết quả để đưa vào phần **Kết quả đạt
được** của báo cáo.

Báo cáo (mục 20) nêu rằng khó khăn lớn nhất là các khái niệm không tồn tại độc
lập, và hướng khắc phục là *"kết hợp đọc tài liệu với thực hành nhỏ"*. Dự án này
chính là phần thực hành đó, được tổ chức theo hai lớp:

| Lớp | Nội dung | Mục tiêu |
|---|---|---|
| **Demo theo chương** | 12 chương trình nhỏ, mỗi chương trình minh họa một chương lý thuyết | Hiểu từng khái niệm riêng lẻ |
| **Ứng dụng tích hợp** | Daemon `apd` + `apctl` + `apmon` + kernel module `ap6sim` | Thấy các khái niệm phối hợp trong một hệ thống thật |

---

## 1. Xây dựng nhanh

```bash
make            # build toàn bộ (apd, apctl, apmon + 12 demo)
make test       # chạy bộ kiểm thử tự động (53 test case)
make run        # chạy thử daemon ở foreground
make module     # build kernel module ap6sim (cần kernel headers)
make stages     # xem từng giai đoạn biên dịch C
```

Yêu cầu: `gcc`, `make`, `libc` (có `pthread`, `librt`).
Build kernel module cần thêm `linux-headers-$(uname -r)`.

Kết quả build nằm trong `build/bin/` và `build/demos/`.

---

## 2. Ứng dụng tích hợp: quản lý Access Point (MediaTek AP 6)

Ba chương trình mô phỏng cách phần mềm quản lý Wi-Fi hoạt động trên một thiết bị
Access Point thật (báo cáo mục 16):

```
   ┌──────────┐  Unix socket / TCP   ┌───────────────────────────┐
   │  apctl   │ ───────────────────► │           apd             │
   │  (CLI)   │ ◄─────────────────── │  (daemon, epoll + thread) │
   └──────────┘                      └────────┬────────┬─────────┘
                                              │        │
   ┌──────────┐   shared memory              │        │ ioctl
   │  apmon   │ ◄────────────────────────────┘        ▼
   │(telemetry)│      /dev/shm/ap6_stats        ┌──────────────┐
   └──────────┘                                 │  /dev/ap6sim │  kernel
                                       fork+exec│  (char dev)  │  module
                                          │     └──────────────┘
                                          ▼
                                 scripts/apply_profile.sh
                                   (iwpriv / uci commit)
```

### Chạy thử

```bash
mkdir -p /tmp/ap6 && cp config/ap6.dat /tmp/ap6/
./build/bin/apd -p /tmp/ap6/ap6.dat -s /tmp/ap6/apd.sock -t 9000 -v &

./build/bin/apctl STATUS                    # trạng thái daemon
./build/bin/apctl SHOW                      # toàn bộ profile SoftAP
./build/bin/apctl SET SSID Viettel_Lab      # có kiểm tra hợp lệ
./build/bin/apctl SET Channel 999           # -> ERR, ngoài khoảng cho phép
./build/bin/apctl APPLY                     # ioctl + fork/exec script
./build/bin/apctl RADIO                     # trạng thái radio từ driver
./build/bin/apctl SAVE                      # ghi profile (atomic)
./build/bin/apmon                           # đọc telemetry qua shared memory

./build/bin/apctl -H 127.0.0.1 -P 9000 GET Channel   # cùng lệnh qua TCP

kill -HUP  %1      # nạp lại cấu hình, không restart
kill -TERM %1      # thoát mềm: đóng socket, giải phóng shm, xóa pid file
```

### Mỗi phần của daemon minh họa điều gì

| Thành phần | File | Mục báo cáo |
|---|---|---|
| epoll loop, Unix + TCP socket, non-blocking I/O | `src/apd/server.c` | 9.6, 13.3, 13.4 |
| Thread telemetry, mutex, condition variable | `src/apd/telemetry.c` | 8.1, 8.3, 8.4 |
| Shared memory + mutex process-shared (robust) | `src/common/stats.c` | 6.4, 9.4, 9.5 |
| `sigaction`, self-pipe, SIGCHLD, SIGHUP, SIGTERM | `src/apd/apd.c` | 10.1 – 10.5 |
| `fork` + `exec` + `waitpid` khi APPLY | `src/apd/apd.c` | 7.3, 7.4 |
| `ioctl` xuống driver, có fallback mô phỏng | `src/apd/hwctl.c` | 11.4, 16.2 |
| Ghi cấu hình kiểu tmp → `fsync` → `rename` | `src/common/util.c` | 5.4 |
| Validate tham số SoftAP theo 4 nhóm | `src/common/profile.c` | 16.3 |
| Daemonize, pid file, syslog | `src/common/util.c`, `log.c` | 7.5 |

---

## 3. Demo theo từng chương của báo cáo

| Mục | Nội dung | Chạy |
|---|---|---|
| 2 | 4 giai đoạn biên dịch, static vs dynamic linking | `make stages` |
| 3 | Memory layout: text/rodata/data/bss/heap/mmap/stack | `./build/demos/memlayout` |
| 4 | System call, `errno`, "everything is a file" | `./build/demos/syscall_demo` |
| 6 | Page, demand paging, page fault, `mmap`, COW | `./build/demos/vm_demo` |
| 7 | `fork`, `exec`, `wait`, zombie, orphan | `./build/demos/process_demo` |
| 8 | Race condition, mutex, condvar, thứ tự lock | `./build/demos/thread_demo` |
| 9 | pipe, FIFO, pipeline kiểu shell | `./build/demos/pipe_fifo_demo` |
| 9 | POSIX message queue, độ ưu tiên message | `./build/demos/mqueue_demo` |
| 9 | Shared memory + semaphore (có/không đồng bộ) | `./build/demos/shm_sem_demo` |
| 10 | `sigaction`, SIGCHLD, self-pipe, SIGTERM vs SIGKILL | `./build/demos/signal_demo` |
| 11 | Char driver: `file_operations`, ioctl, wait queue | `make module` |
| 13 | TCP server (poll) + TCP client | `./build/demos/tcp_echo_server` |
| 13 | UDP telemetry (server/client) | `./build/demos/udp_telemetry` |
| 14 | Đóng gói OpenWrt, procd init, UCI | `openwrt/` |

Kết quả chạy thực tế đã được ghi sẵn trong `docs/ket-qua-chay.txt`
(sinh lại bằng `sh scripts/collect_results.sh`).

---

## 4. Kernel module `ap6sim` (mục 11)

Character driver mô phỏng *control path* của một chip Wi-Fi: có device node,
`file_operations`, `ioctl`, wait queue, procfs, timer đóng vai trò nguồn
interrupt và workqueue làm deferred work.

```bash
make module
cd kernel/ap6sim
sudo insmod ap6sim.ko channel=11
dmesg | tail -5             # log từ hàm init
cat /proc/ap6sim            # trạng thái radio + counter
ls /sys/class/ap6sim/       # device model (mục 11.9)
echo "channel 6" | sudo tee /dev/ap6sim
sudo cat /dev/ap6sim        # read() blocking -> ngủ trên wait queue
sudo rmmod ap6sim           # cleanup đối xứng với init
```

Khi module đã nạp, `apd` tự động phát hiện `/dev/ap6sim` và chuyển control path
từ chế độ mô phỏng sang **ioctl thật xuống kernel** — kiểm chứng bằng
`apctl STATUS` (dòng `backend=`).

> **Lưu ý:** hệ thống kbuild của Linux không chấp nhận đường dẫn có dấu cách.
> `make module` gọi `scripts/build_module.sh`, script này tự copy sang thư mục
> tạm khi cần. Module đã được kiểm tra biên dịch trên kernel 7.0; các macro
> tương thích trong `ap6sim.c` xử lý những thay đổi API
> (`from_timer` → `timer_container_of`, `del_timer_sync` → `timer_delete_sync`,
> `no_llseek` bị gỡ bỏ) — đúng như cảnh báo ở mục 11.10 của báo cáo.

---

## 5. Triển khai lên OpenWrt (mục 14)

`openwrt/package/ap6ctl/` là package OpenWrt hoàn chỉnh:

- `Makefile` — package recipe, cross-compile bằng toolchain của target
- `files/ap6d.init` — init script kiểu **procd** (không phải systemd)
- `files/ap6.config` — cấu hình **UCI** (`/etc/config/ap6`)
- `files/ap6.dat` — profile SoftAP mặc định

```bash
cp -r openwrt/package/ap6ctl <openwrt>/package/utils/
cd <openwrt>
make menuconfig            # Utilities ---> <*> ap6ctl
make package/ap6ctl/compile V=s
scp bin/packages/*/base/ap6ctl_*.ipk root@192.168.1.1:/tmp
ssh root@192.168.1.1 "opkg install /tmp/ap6ctl_*.ipk && /etc/init.d/ap6d enable && /etc/init.d/ap6d start"
```

Cross-compile trực tiếp không qua build system:

```bash
make CROSS_COMPILE=arm-linux-gnueabihf-
make CC=mipsel-openwrt-linux-gcc
```

---

## 6. Cấu trúc thư mục

```
ap6-embedded-linux-project/
├── Makefile                  build toàn bộ, có hỗ trợ CROSS_COMPILE
├── config/ap6.dat            profile SoftAP kiểu MediaTek (4 nhóm tham số)
├── src/
│   ├── common/               log, util (I/O an toàn, daemonize, ghi atomic),
│   │                         profile (parse + validate), stats (shm)
│   ├── apd/                  daemon: main/signal, server (epoll), telemetry,
│   │                         hwctl (ioctl xuống driver)
│   ├── apctl/                CLI client (Unix socket + TCP)
│   └── apmon/                đọc telemetry qua shared memory
├── demos/                    12 demo theo từng chương báo cáo
├── kernel/ap6sim/            character device driver + uapi header
├── openwrt/                  package recipe, procd init, UCI config
├── scripts/                  apply_profile.sh, build_module.sh, collect_results.sh
├── tests/run_tests.sh        53 test case tự động
└── docs/                     bảng đối chiếu báo cáo ↔ mã nguồn, kết quả chạy
```

---

## 7. Quy ước mã nguồn

- Theo **Linux kernel coding style**: thụt đầu dòng bằng tab, hàm ngắn, tên
  hàm dạng `snake_case`, xử lý lỗi bằng `goto err_*` trong kernel module.
- Chú thích bằng tiếng Việt không dấu để tránh lỗi mã hóa khi mở trên board
  hoặc qua serial console.
- Biên dịch với `-Wall -Wextra`, **không có cảnh báo nào**.
- Mọi system call đều kiểm tra giá trị trả về; mọi tài nguyên (fd, mmap, thread,
  shm, pid file) đều được giải phóng theo thứ tự ngược với khi cấp phát.
