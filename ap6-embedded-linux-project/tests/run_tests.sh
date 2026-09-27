#!/bin/sh
# run_tests.sh - kiem thu tu dong toan bo du an.
#
# Chay:  make test     hoac     sh tests/run_tests.sh
#
# Script kiem tra:
#   - cac demo theo tung chuong chay duoc va in dung noi dung mong doi
#   - daemon apd: khoi dong, control path (unix + tcp), validate tham so,
#     APPLY (fork/exec), SAVE (ghi atomic), RELOAD, telemetry qua shm,
#     xu ly SIGHUP va SIGTERM (thoat sach, khong de lai socket/shm)

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="$ROOT/build/bin"
DEMO="$ROOT/build/demos"
RUNDIR=/tmp/ap6-test
SOCK="$RUNDIR/apd.sock"
PROFILE="$RUNDIR/ap6.dat"
LOG="$RUNDIR/apd.log"
TCP_PORT=19099

PASS=0
FAIL=0

ok()   { PASS=$((PASS + 1)); printf "  \033[32mPASS\033[0m  %s\n" "$1"; }
bad()  { FAIL=$((FAIL + 1)); printf "  \033[31mFAIL\033[0m  %s\n" "$1"; }
head_() { printf "\n\033[1m%s\033[0m\n" "$1"; }

# check <mo ta> <chuoi mong doi> <output>
check() {
	desc=$1; want=$2; got=$3
	if printf '%s' "$got" | grep -q -- "$want"; then
		ok "$desc"
	else
		bad "$desc (khong thay '$want')"
		printf '        ---\n%s\n        ---\n' "$got" | head -12
	fi
}

cleanup() {
	[ -n "$APD_PID" ] && kill "$APD_PID" 2>/dev/null
	sleep 0.3
	[ -n "$APD_PID" ] && kill -9 "$APD_PID" 2>/dev/null
	rm -f /dev/shm/ap6_stats 2>/dev/null
}
trap cleanup EXIT INT TERM

rm -rf "$RUNDIR"
mkdir -p "$RUNDIR"
cp "$ROOT/config/ap6.dat" "$PROFILE"

if [ ! -x "$BIN/apd" ]; then
	echo "Chua build. Chay 'make' truoc."
	exit 1
fi

########################################################################
head_ "[1] Demo cac chuong ly thuyet"
########################################################################

out=$("$DEMO/memlayout" 2>&1)
check "muc 3  - memlayout in du cac vung nho"  "stack : bien cuc bo" "$out"
check "muc 3  - BSS duoc kernel xoa ve 0"      "g_bss_uninit = 0"    "$out"

out=$("$DEMO/syscall_demo" 2>&1)
check "muc 4  - system call that bai co errno" "errno=2"             "$out"
check "muc 4  - doc /proc qua open/read"       "/proc/version"       "$out"

out=$("$DEMO/vm_demo" 2>&1)
check "muc 6  - demand paging: minor fault"    "minor fault tang"    "$out"
check "muc 6  - MAP_SHARED thay thay doi"      "MAP_SHARED  sau khi con ghi 999 : 999" "$out"
check "muc 6  - MAP_PRIVATE copy-on-write"     "MAP_PRIVATE sau khi con ghi 999 : 100" "$out"

out=$("$DEMO/process_demo" 2>&1)
check "muc 7  - fork tra ve 0 o process con"   "fork() tra ve 0"     "$out"
check "muc 7  - cha khong bi con sua bien"     "g_counter = 100"     "$out"
check "muc 7  - exec thay the image"           "Linux"               "$out"

out=$("$DEMO/thread_demo" 2>&1)
check "muc 8  - mutex cho ket qua dung"        "co mutex bao ve           : 800000" "$out"
check "muc 8  - producer/consumer dung tong"   "tong tat ca item = 210"  "$out"
check "muc 8  - khong deadlock khi lock dung thu tu" "khong deadlock" "$out"

out=$("$DEMO/pipe_fifo_demo" 2>&1)
check "muc 9  - pipe truyen du lieu"           "cau hinh: Channel=11" "$out"
check "muc 9  - pipe bao EOF khi dong dau ghi" "read() lan hai tra ve 0" "$out"
check "muc 9  - FIFO co ten trong filesystem"  "EVENT sta_connect"   "$out"

out=$("$DEMO/mqueue_demo" 2>&1)
if printf '%s' "$out" | grep -q "mq_open that bai"; then
	ok "muc 9  - message queue (bo qua: he thong chua mount /dev/mqueue)"
else
	check "muc 9  - message queue giu ranh gioi message" "EMERGENCY" "$out"
fi

out=$("$DEMO/shm_sem_demo" 2>&1)
check "muc 9  - shm khong dong bo bi mat cap nhat" "KHONG dong bo"   "$out"
check "muc 9  - semaphore cho ket qua dung"    "thuc te 200000  -> dung" "$out"

out=$(timeout 15 "$DEMO/signal_demo" 2>&1)
check "muc 10 - SIGCHLD duoc thu hoi"          "\[SIGCHLD\]"         "$out"
check "muc 10 - SIGALRM tu timer"              "\[SIGALRM\]"         "$out"

########################################################################
head_ "[2] Socket demo (muc 13)"
########################################################################

"$DEMO/tcp_echo_server" 19100 >"$RUNDIR/echo.log" 2>&1 &
ECHO_PID=$!
sleep 0.5
out=$("$DEMO/tcp_client" 127.0.0.1 19100 "xin chao AP6" 2>&1)
check "muc 13 - TCP echo tra lai dung noi dung" "Nhan lai" "$out"
check "muc 13 - noi dung khop"                  "xin chao AP6" "$out"
kill $ECHO_PID 2>/dev/null

"$DEMO/udp_telemetry" server 19200 >"$RUNDIR/udp.log" 2>&1 &
UDP_PID=$!
sleep 0.5
"$DEMO/udp_telemetry" client 127.0.0.1 19200 3 >/dev/null 2>&1
sleep 0.5
kill $UDP_PID 2>/dev/null
sleep 0.3
check "muc 13 - UDP collector nhan duoc datagram" "ap6 telemetry seq=" "$(cat "$RUNDIR/udp.log")"

########################################################################
head_ "[3] Daemon apd - control path"
########################################################################

"$BIN/apd" -p "$PROFILE" -s "$SOCK" -t $TCP_PORT \
	-i "$RUNDIR/apd.pid" -S "$ROOT/scripts/apply_profile.sh" \
	>"$LOG" 2>&1 &
APD_PID=$!
sleep 1

if kill -0 "$APD_PID" 2>/dev/null; then
	ok "apd khoi dong (pid=$APD_PID)"
else
	bad "apd khong khoi dong duoc"
	cat "$LOG"
	exit 1
fi

[ -S "$SOCK" ] && ok "tao Unix domain socket" || bad "khong thay socket $SOCK"
[ -f "$RUNDIR/apd.pid" ] && ok "ghi pid file" || bad "khong ghi duoc pid file"

check "STATUS tra ve pid"        "pid="        "$("$BIN/apctl" -s "$SOCK" STATUS)"
check "SHOW in ra profile"       "SSID="       "$("$BIN/apctl" -s "$SOCK" SHOW)"
check "GET doc duoc tham so"     "Viettel_AP6" "$("$BIN/apctl" -s "$SOCK" GET SSID)"
check "GET tham so khong ton tai" "ERR"        "$("$BIN/apctl" -s "$SOCK" GET KhongCoThamSoNay)"

check "SET hop le"               "OK Channel=11" "$("$BIN/apctl" -s "$SOCK" SET Channel 11)"
check "SET ngoai khoang -> loi"  "ERR"           "$("$BIN/apctl" -s "$SOCK" SET Channel 999)"
check "SET SSID qua 32 ky tu -> loi" "ERR"       "$("$BIN/apctl" -s "$SOCK" SET SSID 123456789012345678901234567890123456)"
check "SET AuthMode sai enum -> loi" "ERR"       "$("$BIN/apctl" -s "$SOCK" SET AuthMode WEP_KHONG_HO_TRO)"
check "SET CountryCode sai -> loi"   "ERR"       "$("$BIN/apctl" -s "$SOCK" SET CountryCode VNM)"
check "SET WPAPSK ngan -> loi"       "ERR"       "$("$BIN/apctl" -s "$SOCK" SET WPAPSK 123)"
check "lenh khong ton tai -> loi"    "ERR"       "$("$BIN/apctl" -s "$SOCK" KHONGCOLENH)"

check "TCP control path hoat dong" "11" "$("$BIN/apctl" -H 127.0.0.1 -P $TCP_PORT GET Channel)"

########################################################################
head_ "[4] Daemon apd - fork/exec, luu cau hinh, telemetry"
########################################################################

"$BIN/apctl" -s "$SOCK" SET SSID Viettel_Test >/dev/null
check "APPLY fork process con"  "child_pid=" "$("$BIN/apctl" -s "$SOCK" APPLY)"
sleep 1
check "process con duoc thu hoi (khong zombie)" "ket thuc, exit=0" "$(cat "$LOG")"
ZOMBIES=$(ps -o stat= --ppid "$APD_PID" 2>/dev/null | grep -c Z)
[ "${ZOMBIES:-0}" -eq 0 ] && ok "khong con zombie duoi apd" || bad "con $ZOMBIES zombie"

check "RADIO doc trang thai qua backend" "channel=11" "$("$BIN/apctl" -s "$SOCK" RADIO)"
check "STATS lay tu shared memory"       "seq="       "$("$BIN/apctl" -s "$SOCK" STATS)"

out1=$("$BIN/apmon" 1 100 | tail -1 | awk '{print $1}')
sleep 1
out2=$("$BIN/apmon" 1 100 | tail -1 | awk '{print $1}')
if [ "${out2:-0}" -gt "${out1:-0}" ]; then
	ok "apmon (process khac) thay seq tang: $out1 -> $out2"
else
	bad "telemetry khong cap nhat (seq $out1 -> $out2)"
fi

"$BIN/apctl" -s "$SOCK" SAVE >/dev/null
grep -q "SSID=Viettel_Test" "$PROFILE" && ok "SAVE ghi cau hinh xuong file" \
	|| bad "SAVE khong ghi duoc"
[ ! -f "$PROFILE.tmp" ] && ok "SAVE khong de lai file tam (ghi atomic)" \
	|| bad "con file tam $PROFILE.tmp"

########################################################################
head_ "[5] Xu ly signal (muc 10.5)"
########################################################################

sed -i 's/^SSID=.*/SSID=Doi_Ngoai_Tuyen/' "$PROFILE"
kill -HUP "$APD_PID"
sleep 1
check "SIGHUP nap lai profile tu file" "Doi_Ngoai_Tuyen" \
	"$("$BIN/apctl" -s "$SOCK" GET SSID)"

kill -TERM "$APD_PID"
sleep 1
if kill -0 "$APD_PID" 2>/dev/null; then
	bad "SIGTERM khong lam apd thoat"
	kill -9 "$APD_PID" 2>/dev/null
else
	ok "SIGTERM lam apd thoat mem"
fi
APD_PID=""

[ ! -S "$SOCK" ] && ok "don dep: da xoa socket" || bad "socket con sot lai"
[ ! -f "$RUNDIR/apd.pid" ] && ok "don dep: da xoa pid file" || bad "pid file con sot lai"
[ ! -e /dev/shm/ap6_stats ] && ok "don dep: da giai phong shared memory" \
	|| bad "shm con sot lai trong /dev/shm"
grep -q "apd da thoat" "$LOG" && ok "log ghi nhan thoat co kiem soat" \
	|| bad "khong thay log thoat"

########################################################################
head_ "[6] Kernel module ap6sim (tuy chon)"
########################################################################

if [ -c /dev/ap6sim ]; then
	check "doc /proc/ap6sim" "channel" "$(cat /proc/ap6sim 2>&1)"
	ok "module da duoc nap - control path di qua driver that"
else
	echo "  SKIP  module chua nap (chay: cd kernel/ap6sim && make && make load)"
fi

########################################################################
printf "\n\033[1mTong ket: %d PASS, %d FAIL\033[0m\n" "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
