#!/bin/sh
# build_stages.sh - tach tung giai doan bien dich C (Bao cao muc 2.1, 2.2)
#
#   source .c -> (cpp) .i -> (cc1) .s -> (as) .o -> (ld) ELF
#
# Script chay duoc voi /bin/sh nen dung ca tren BusyBox cua OpenWrt.
set -e

CC=${CC:-gcc}
cd "$(dirname "$0")"		# lam viec trong thu muc chua ma nguon demo
OUT=${OUT:-./out}
mkdir -p "$OUT"

line() { echo "----------------------------------------------------------"; }

line
echo "[1/6] Tien xu ly: gcc -E  (mo rong #include, #define, #ifdef)"
$CC -E hello.c -I. -o "$OUT/hello.i"
echo "  hello.c : $(wc -l < hello.c) dong"
echo "  hello.i : $(wc -l < "$OUT/hello.i") dong  <- da chen noi dung stdio.h"
echo "  Kiem tra macro GREETING da duoc thay the:"
grep -n "Xin chao tu Embedded Linux" "$OUT/hello.i" | head -2 | sed 's/^/    /'

line
echo "[2/6] Bien dich: gcc -S  (C -> assembly)"
$CC -S "$OUT/hello.i" -o "$OUT/hello.s"
echo "  Vai dong assembly dau tien cua main:"
sed -n '/^main:/,/ret/p' "$OUT/hello.s" | head -12 | sed 's/^/    /'

line
echo "[3/6] Hop dich: gcc -c  (assembly -> object file relocatable)"
$CC -c "$OUT/hello.s" -o "$OUT/hello.o"
file "$OUT/hello.o" | sed 's/^/    /'
echo "  Symbol chua duoc giai quyet (U = undefined, can linker):"
nm "$OUT/hello.o" | grep " U " | sed 's/^/    /'

line
echo "[4/6] Lien ket TINH: dua ma thu vien vao thang trong file thuc thi"
$CC -c mathlib.c -o "$OUT/mathlib.o"
ar rcs "$OUT/libmath.a" "$OUT/mathlib.o"
$CC "$OUT/hello.o" "$OUT/libmath.a" -o "$OUT/hello_static_lib"
echo "  Kich thuoc: $(stat -c %s "$OUT/hello_static_lib") byte"
echo "  ldd:"
ldd "$OUT/hello_static_lib" 2>/dev/null | sed 's/^/    /' || echo "    (khong dong)"

line
echo "[5/6] Lien ket DONG: chi luu tham chieu, nap luc chay"
$CC -fPIC -c mathlib.c -o "$OUT/mathlib_pic.o"
$CC -shared "$OUT/mathlib_pic.o" -o "$OUT/libmath.so"
$CC "$OUT/hello.o" -L"$OUT" -lmath -Wl,-rpath,'$ORIGIN' -o "$OUT/hello_shared"
echo "  Kich thuoc: $(stat -c %s "$OUT/hello_shared") byte"
echo "  Thu vien can khi chay (ldd):"
ldd "$OUT/hello_shared" | sed 's/^/    /'
echo "  -> tren rootfs nhung, thieu libmath.so la chuong trinh khong chay duoc"

line
echo "[6/6] So sanh voi -static toan phan (khong can shared library nao)"
$CC hello.c mathlib.c -I. -static -o "$OUT/hello_fullstatic"
printf "  dong (shared)      : %8s byte\n" "$(stat -c %s "$OUT/hello_shared")"
printf "  tinh (libmath.a)   : %8s byte\n" "$(stat -c %s "$OUT/hello_static_lib")"
printf "  tinh toan phan     : %8s byte\n" "$(stat -c %s "$OUT/hello_fullstatic")"

line
echo "Cau truc ELF (readelf -h) va cac section chinh:"
readelf -h "$OUT/hello_shared" | grep -E "Type|Machine|Entry" | sed 's/^/    /'
readelf -S "$OUT/hello_shared" | grep -E "\.text|\.data|\.bss|\.rodata" | sed 's/^/    /'

line
echo "Chay thu ban dong:"
LD_LIBRARY_PATH="$OUT" "$OUT/hello_shared" | sed 's/^/    /'
line
echo "Ket qua trung gian nam trong: $OUT"
