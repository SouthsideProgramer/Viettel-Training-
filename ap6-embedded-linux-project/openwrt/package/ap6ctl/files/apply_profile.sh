#!/bin/sh
# apply_profile.sh - script duoc apd goi bang fork() + exec() khi nhan lenh
# APPLY (Bao cao muc 7.3, 7.4, 16.2).
#
# Tren may phat trien, script chi in ra cac lenh se chay.
# Tren thiet bi that (MediaTek AP / OpenWrt), bo comment cac dong iwpriv/uci.
#
# Tham so: $1 = SSID, $2 = channel, $3 = txpower

SSID="$1"
CHANNEL="$2"
TXPOWER="$3"
IFACE="${AP6_IFACE:-ra0}"

log() { echo "[apply_profile] $*"; }

log "ap dung cau hinh: ssid='$SSID' channel=$CHANNEL txpower=$TXPOWER iface=$IFACE"

if command -v iwpriv >/dev/null 2>&1 && [ -d "/sys/class/net/$IFACE" ]; then
	# Duong cau hinh runtime cua driver MediaTek (muc 16.2)
	iwpriv "$IFACE" set SSID="$SSID"
	iwpriv "$IFACE" set Channel="$CHANNEL"
	iwpriv "$IFACE" set TxPower="$TXPOWER"
elif [ -w /dev/ap6sim ]; then
	# Che do demo: ghi lenh text xuong char device cua module ap6sim
	echo "channel $CHANNEL" > /dev/ap6sim
	echo "ssid $SSID" > /dev/ap6sim
	log "da ghi cau hinh xuong /dev/ap6sim"
else
	log "(mo phong) iwpriv $IFACE set SSID=$SSID"
	log "(mo phong) iwpriv $IFACE set Channel=$CHANNEL"
	log "(mo phong) iwpriv $IFACE set TxPower=$TXPOWER"
fi

# Tren OpenWrt, cach thong dung hon la qua UCI roi reload service:
#   uci set wireless.@wifi-iface[0].ssid="$SSID"
#   uci set wireless.@wifi-device[0].channel="$CHANNEL"
#   uci commit wireless
#   wifi reload

log "hoan tat"
exit 0
