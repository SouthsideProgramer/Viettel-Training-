#!/bin/sh
# build_module.sh - build kernel module ap6sim.
#
# Luu y: he thong kbuild cua Linux KHONG chap nhan duong dan co dau cach.
# Neu du an nam trong thu muc kieu "thuc tap/", script se tu dong copy sang
# mot thu muc tam khong dau cach roi build o do, sau do copy .ko tro lai.

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/kernel/ap6sim"

case "$SRC" in
*" "*)
	TMP=/tmp/ap6sim-build
	echo "[build_module] duong dan co dau cach -> build tam tai $TMP"
	rm -rf "$TMP"
	cp -r "$SRC" "$TMP"
	make -C "$TMP" "$@"
	cp "$TMP"/*.ko "$SRC"/ 2>/dev/null || true
	echo "[build_module] da tao: $SRC/ap6sim.ko"
	;;
*)
	make -C "$SRC" "$@"
	;;
esac

cat <<'EOF'

Nap module va kiem tra:
  sudo insmod ap6sim.ko            # hoac: sudo insmod ap6sim.ko channel=11
  dmesg | tail -5                  # log tu pr_info trong ham init
  ls -l /dev/ap6sim                # device node do udev tao
  ls /sys/class/ap6sim/            # device model (muc 11.9)
  cat /proc/ap6sim                 # trang thai qua procfs (muc 11.10)
  sudo cat /dev/ap6sim             # read() blocking -> ngu tren wait queue
  echo "channel 11" | sudo tee /dev/ap6sim
  sudo rmmod ap6sim                # cleanup doi xung voi init
EOF
