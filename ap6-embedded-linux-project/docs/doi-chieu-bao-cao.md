# Bảng đối chiếu: mục báo cáo ↔ mã nguồn ↔ cách kiểm chứng

Bảng này dùng khi trình bày seminar hoặc bảo vệ báo cáo: mỗi khẳng định lý
thuyết trong báo cáo đều có một đoạn mã và một lệnh chạy để chứng minh.

| Mục báo cáo | Nội dung lý thuyết | Mã nguồn | Lệnh kiểm chứng |
|---|---|---|---|
| 2.1 | Tiền xử lý → biên dịch → hợp dịch → liên kết | `demos/02-basic-c/build_stages.sh` | `make stages` |
| 2.2 | Liên kết tĩnh vs động, kích thước file thực thi | `demos/02-basic-c/mathlib.c` | `make stages` (bước 4–6) |
| 3.1 | text/rodata/data/bss/heap/mmap/stack | `demos/03-memory-layout/memlayout.c` | `./build/demos/memlayout` |
| 3.1 | BSS được kernel xóa về 0 khi nạp | như trên | in `g_bss_uninit = 0` |
| 3.2 | Lỗi thuộc compile-time / load-time / run-time | `demos/02-basic-c/build_stages.sh` | `nm hello.o \| grep " U "` |
| 4.1 | Ranh giới user space – kernel space | `demos/04-syscall/syscall_demo.c` | `strace -c ./build/demos/syscall_demo` |
| 4.2 | System call, `errno`, "everything is a file" | như trên | đọc `/proc`, `/dev/urandom` |
| 5.3 | File descriptor, VFS, cùng API cho nhiều loại đối tượng | `demos/04-syscall/syscall_demo.c` | so sánh `/proc/version` và `/dev/urandom` |
| 5.4 | Cache, `fsync`, cập nhật nguyên tử trên flash | `src/common/util.c:write_file_atomic` | `apctl SAVE` rồi kiểm tra không còn `.tmp` |
| 6.1–6.2 | Page, page table, MMU, TLB | `demos/06-virtual-memory/vm_demo.c` mục 1 | `./build/demos/vm_demo` |
| 6.3 | Demand paging, minor/major page fault | `vm_demo.c` mục 2 | RSS gần như không đổi sau `mmap` 64 MB |
| 6.4 | `mmap` file, MAP_SHARED, chia sẻ dữ liệu | `vm_demo.c` mục 3–4 | MAP_SHARED = 999, MAP_PRIVATE = 100 |
| 7.1–7.2 | Process, PID/PPID, trạng thái | `demos/07-process/process_demo.c` | `./build/demos/process_demo` |
| 7.3 | `fork`, copy-on-write | `process_demo.c` mục 1–2 | biến toàn cục của cha không đổi |
| 7.4 | `exec`, `wait`, exit status | `process_demo.c` mục 3 | PID giữ nguyên, image bị thay |
| 7.2 | Zombie và orphan | `process_demo.c` mục 4–5 | `ps -o stat=` hiện `Z` |
| 7.5 | Daemon: `setsid`, hai lần `fork`, pid file | `src/common/util.c:daemonize` | `apd -d` rồi `ps -o ppid=` |
| 8.1–8.2 | Thread chia sẻ địa chỉ, so với process | `demos/08-thread/thread_demo.c` | `./build/demos/thread_demo` |
| 8.3 | Race condition, critical section, mutex | `thread_demo.c` mục 1–2 | không khóa: mất hàng nghìn lần cập nhật |
| 8.4 | Condition variable, deadlock, thứ tự lock | `thread_demo.c` mục 3–4 | producer–consumer tổng = 210 |
| 8.5 | Thread trong ứng dụng nhúng | `src/apd/telemetry.c` | `apctl STATS` thấy `seq` tăng |
| 9.2 | pipe, FIFO, pipeline kiểu shell | `demos/09-ipc/pipe_fifo_demo.c` | `./build/demos/pipe_fifo_demo` |
| 9.3 | Message queue, ranh giới message, độ ưu tiên | `demos/09-ipc/mqueue_demo.c` | message `EMERGENCY` được nhận trước |
| 9.4 | Shared memory, không tự đồng bộ | `demos/09-ipc/shm_sem_demo.c` | lần 1 mất cập nhật, lần 2 đúng |
| 9.5 | Semaphore, rủi ro khi process giữ lock bị crash | `shm_sem_demo.c`, `src/common/stats.c` | mutex ROBUST + `EOWNERDEAD` |
| 9.6 | Unix domain socket làm IPC | `src/apd/server.c` | `apctl -s /tmp/ap6/apd.sock STATUS` |
| 9.7 | Chọn cơ chế IPC theo bài toán | `README.md` mục 2 | socket (lệnh) + shm (telemetry) |
| 10.1–10.2 | Signal, SIGTERM vs SIGKILL | `demos/10-signal/signal_demo.c` | `kill -TERM` vs `kill -9` |
| 10.3 | `sigaction`, `volatile sig_atomic_t`, self-pipe | `signal_demo.c`, `src/apd/apd.c` | handler chỉ đặt cờ |
| 10.4 | SIGCHLD + `waitpid(WNOHANG)` chống zombie | `src/apd/apd.c:reap_children` | `apctl APPLY` rồi `ps --ppid` |
| 10.5 | SIGHUP reload, SIGTERM dọn dẹp | `src/apd/apd.c` | `kill -HUP` đổi SSID không restart |
| 11.2 | Vòng đời module, init/exit đối xứng | `kernel/ap6sim/ap6sim.c` | `insmod` / `rmmod` nhiều lần |
| 11.3 | Char driver, device node trong `/dev` | như trên | `ls -l /dev/ap6sim` |
| 11.4 | `file_operations`, `copy_to_user`, wait queue | như trên | `cat /dev/ap6sim` (blocking read) |
| 11.5 | mutex cho process context, spinlock cho interrupt | như trên | `cfg_lock` vs `evt_lock` |
| 11.7 | Interrupt ngắn + deferred work (workqueue) | như trên | timer → `schedule_work` |
| 11.9 | Device model, sysfs | như trên | `ls /sys/class/ap6sim/` |
| 11.10 | Debug driver: dmesg, procfs; API kernel thay đổi | như trên | `cat /proc/ap6sim`, macro `LINUX_VERSION_CODE` |
| 12 | Git workflow | `docs/git-workflow.md` | — |
| 13.1–13.2 | Socket là file descriptor; TCP vs UDP | `demos/13-socket/` | `tcp_echo_server`, `udp_telemetry` |
| 13.3 | socket→bind→listen→accept / socket→connect | `tcp_echo_server.c`, `tcp_client.c` | chạy song song hai chương trình |
| 13.4 | Byte order, blocking/non-blocking, đóng fd | `src/apd/server.c`, `src/apctl/apctl.c` | `htons`, `poll`, `SO_RCVTIMEO` |
| 14.2 | procd, UCI, opkg | `openwrt/package/ap6ctl/` | `/etc/init.d/ap6d start` |
| 14.3 | Cross-compile ứng dụng C cho OpenWrt | `Makefile`, package recipe | `make CROSS_COMPILE=...` |
| 16.2 | Profile SoftAP và `iwpriv` | `config/ap6.dat`, `scripts/apply_profile.sh` | `apctl APPLY` |
| 16.3 | 4 nhóm tham số quan trọng của AP 6 | `src/common/profile.c:k_defs` | `apctl SET Channel 999` → ERR |
| 16.4 | Daemon quản lý + socket điều khiển | `src/apd/` | `apctl -H 127.0.0.1 -P 9000 ...` |
| 16.5 | Quy trình bring-up và kiểm thử theo lớp | `tests/run_tests.sh` | `make test` |

## Những điểm có thể bị hỏi khi bảo vệ

**"Tại sao dùng self-pipe thay vì xử lý thẳng trong signal handler?"**
Signal handler có thể chen vào bất kỳ điểm nào của luồng thực thi. Chỉ một tập
nhỏ hàm là async-signal-safe (`write`, `_exit`, ...); gọi `printf` hay `malloc`
trong handler có thể gây deadlock nếu luồng chính đang giữ khóa nội bộ của
glibc. Handler chỉ ghi 1 byte vào pipe để đánh thức `epoll_wait`, còn xử lý thật
nằm ở vòng lặp chính — xem `src/apd/apd.c:sig_handler`.

**"Tại sao mutex trong shared memory phải khai báo PROCESS_SHARED?"**
Mặc định `pthread_mutex_t` chỉ hợp lệ trong phạm vi một process. Khi mutex nằm
trong vùng `mmap` chia sẻ, phải đặt `PTHREAD_PROCESS_SHARED`, nếu không mỗi
process sẽ khóa trên bản sao riêng và cơ chế bảo vệ mất tác dụng. Dự án dùng
thêm `PTHREAD_MUTEX_ROBUST` để process kế tiếp nhận `EOWNERDEAD` thay vì chờ vô
hạn khi chủ sở hữu bị kill — xem `src/common/stats.c`.

**"Vì sao ghi cấu hình phải qua file tạm rồi `rename`?"**
`rename()` trong cùng filesystem là thao tác nguyên tử. Nếu ghi đè trực tiếp mà
mất điện giữa chừng, thiết bị sẽ boot lên với file cấu hình hỏng. Trình tự
ghi tmp → `fsync` → `rename` bảo đảm hoặc thấy cấu hình cũ, hoặc thấy cấu hình
mới, không có trạng thái trung gian — xem `src/common/util.c` và mục 5.4.

**"`fork()` trong chương trình có nhiều thread có an toàn không?"**
Process con chỉ thừa hưởng thread đã gọi `fork`; các mutex do thread khác giữ
sẽ vĩnh viễn bị khóa trong process con. Vì vậy giữa `fork` và `exec`, process
con trong `apd_apply_profile()` chỉ gọi hàm async-signal-safe và `exec` ngay.

**"Tại sao process con dùng `_exit()` chứ không `exit()`?"**
`exit()` chạy các handler `atexit` và flush buffer stdio mà process con thừa
hưởng từ cha — dữ liệu trong buffer sẽ bị in ra hai lần. `_exit()` kết thúc
ngay, không đụng vào buffer. Nếu process con thật sự cần in ra, phải
`fflush(stdout)` một cách chủ động — xem `demos/07-process/process_demo.c`.
