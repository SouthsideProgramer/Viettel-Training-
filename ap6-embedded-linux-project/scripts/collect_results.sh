#!/bin/sh
# collect_results.sh - chay toan bo demo va ghi ket qua ra mot file text
# de dan vao phan phu luc / "Ket qua dat duoc" cua bao cao.
#
#   sh scripts/collect_results.sh            -> docs/ket-qua-chay.txt

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/docs/ket-qua-chay.txt"
BIN="$ROOT/build/bin"
DEMO="$ROOT/build/demos"
RUNDIR=/tmp/ap6-collect

if [ ! -x "$BIN/apd" ]; then
	echo "Chua build. Chay 'make' truoc."
	exit 1
fi

rm -rf "$RUNDIR"; mkdir -p "$RUNDIR"
cp "$ROOT/config/ap6.dat" "$RUNDIR/ap6.dat"
mkdir -p "$ROOT/docs"

section() {
	printf '\n\n================================================================\n' >>"$OUT"
	printf '%s\n' "$1" >>"$OUT"
	printf '================================================================\n' >>"$OUT"
}

{
	echo "KET QUA CHAY CHUONG TRINH - DU AN KEM BAO CAO THUC TAP"
	echo "Ngay chay : $(date '+%d/%m/%Y %H:%M')"
	echo "Kernel    : $(uname -sr)"
	echo "Kien truc : $(uname -m)"
	echo "Compiler  : $(gcc --version | head -1)"
} >"$OUT"

section "Muc 2 - Cac giai doan bien dich va linking"
sh "$ROOT/demos/02-basic-c/build_stages.sh" >>"$OUT" 2>&1

section "Muc 3 - Memory layout cua process"
"$DEMO/memlayout" >>"$OUT" 2>&1

section "Muc 4 - System call va ranh gioi user/kernel space"
"$DEMO/syscall_demo" >>"$OUT" 2>&1
if command -v strace >/dev/null 2>&1; then
	echo "" >>"$OUT"
	echo "--- strace -c (thong ke so lan goi system call) ---" >>"$OUT"
	strace -c -f "$DEMO/syscall_demo" >/dev/null 2>>"$OUT"
fi

section "Muc 6 - Virtual memory, page fault, mmap"
"$DEMO/vm_demo" >>"$OUT" 2>&1

section "Muc 7 - Process: fork, exec, wait, zombie, orphan"
"$DEMO/process_demo" >>"$OUT" 2>&1

section "Muc 8 - Thread: race condition, mutex, condition variable"
"$DEMO/thread_demo" >>"$OUT" 2>&1

section "Muc 9 - IPC: pipe, FIFO"
"$DEMO/pipe_fifo_demo" >>"$OUT" 2>&1

section "Muc 9 - IPC: POSIX message queue"
"$DEMO/mqueue_demo" >>"$OUT" 2>&1

section "Muc 9 - IPC: shared memory + semaphore"
"$DEMO/shm_sem_demo" >>"$OUT" 2>&1

section "Muc 10 - Signal: sigaction, SIGCHLD, SIGTERM"
timeout 15 "$DEMO/signal_demo" >>"$OUT" 2>&1

section "Muc 13 - Socket TCP (server + client)"
"$DEMO/tcp_echo_server" 19300 >>"$OUT" 2>&1 &
SPID=$!
sleep 1
"$DEMO/tcp_client" 127.0.0.1 19300 "xin chao tu client" >>"$OUT" 2>&1
sleep 0.5
kill $SPID 2>/dev/null
wait $SPID 2>/dev/null

section "Muc 13 - Socket UDP telemetry"
"$DEMO/udp_telemetry" server 19301 >>"$OUT" 2>&1 &
UPID=$!
sleep 0.5
"$DEMO/udp_telemetry" client 127.0.0.1 19301 3 >>"$OUT" 2>&1
sleep 1
kill $UPID 2>/dev/null
wait $UPID 2>/dev/null		# cho server ghi het log truoc khi sang muc sau

section "Muc 16 - Daemon apd: control path, APPLY, telemetry"
"$BIN/apd" -p "$RUNDIR/ap6.dat" -s "$RUNDIR/apd.sock" -t 19302 \
	-i "$RUNDIR/apd.pid" -S "$ROOT/scripts/apply_profile.sh" \
	>"$RUNDIR/apd.log" 2>&1 &
APID=$!
sleep 1

for c in "STATUS" "SHOW" "GET SSID" "SET SSID Viettel_Lab" "SET Channel 11" \
	 "SET Channel 999" "SET AuthMode WEP" "APPLY" "RADIO" "STATS" "SAVE"
do
	echo "" >>"$OUT"
	echo "\$ apctl $c" >>"$OUT"
	"$BIN/apctl" -s "$RUNDIR/apd.sock" $c >>"$OUT" 2>&1
done

echo "" >>"$OUT"
echo "\$ apctl -H 127.0.0.1 -P 19302 GET Channel   # cung lenh qua TCP" >>"$OUT"
"$BIN/apctl" -H 127.0.0.1 -P 19302 GET Channel >>"$OUT" 2>&1

echo "" >>"$OUT"
echo "\$ apmon 3 500   # process khac doc telemetry qua shared memory" >>"$OUT"
"$BIN/apmon" 3 500 >>"$OUT" 2>&1

echo "" >>"$OUT"
echo "\$ kill -HUP  <apd>   # nap lai profile" >>"$OUT"
kill -HUP $APID; sleep 1
echo "\$ kill -TERM <apd>   # thoat mem, don dep tai nguyen" >>"$OUT"
kill -TERM $APID; sleep 1

echo "" >>"$OUT"
echo "--- log cua daemon ---" >>"$OUT"
cat "$RUNDIR/apd.log" >>"$OUT"

echo "" >>"$OUT"
echo "--- kiem tra don dep sau khi thoat ---" >>"$OUT"
[ -S "$RUNDIR/apd.sock" ] && echo "socket con sot lai" >>"$OUT" \
	|| echo "socket da duoc xoa: OK" >>"$OUT"
[ -e /dev/shm/ap6_stats ] && echo "shm con sot lai" >>"$OUT" \
	|| echo "shared memory da duoc giai phong: OK" >>"$OUT"

section "Muc 11 - Kernel module ap6sim"
if [ -c /dev/ap6sim ]; then
	echo "\$ cat /proc/ap6sim" >>"$OUT"
	cat /proc/ap6sim >>"$OUT" 2>&1
	echo "" >>"$OUT"
	echo "\$ ls /sys/class/ap6sim/" >>"$OUT"
	ls /sys/class/ap6sim/ >>"$OUT" 2>&1
	echo "" >>"$OUT"
	echo "\$ dmesg | grep ap6sim | tail -5" >>"$OUT"
	dmesg 2>/dev/null | grep ap6sim | tail -5 >>"$OUT"
else
	echo "Module chua duoc nap." >>"$OUT"
	echo "Chay: make module && cd kernel/ap6sim && sudo insmod ap6sim.ko" >>"$OUT"
fi

section "Bo test tu dong"
sh "$ROOT/tests/run_tests.sh" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' >>"$OUT"

echo ""
echo "Da ghi ket qua vao: $OUT"
echo "So dong: $(wc -l < "$OUT")"
