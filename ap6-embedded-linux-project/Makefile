# Makefile - du an minh hoa cho bao cao thuc tap Embedded Linux
#
#   make            build apd, apctl, apmon va toan bo demo
#   make apps       chi build 3 chuong trinh chinh
#   make demos      chi build cac demo theo tung chuong bao cao
#   make module     build kernel module ap6sim (can kernel headers)
#   make test       chay bo test tu dong
#   make run        chay thu daemon o foreground
#   make clean
#
# Cross-compile cho board ARM / OpenWrt:
#   make CROSS_COMPILE=arm-linux-gnueabihf-
#   make CC=mipsel-openwrt-linux-gcc

CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
BUILD   := build
BIN     := $(BUILD)/bin
DEMOBIN := $(BUILD)/demos

CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 \
           -D_FILE_OFFSET_BITS=64
LDFLAGS ?=
LIBS    := -lpthread -lrt

COMMON_SRC := src/common/log.c src/common/util.c src/common/profile.c \
              src/common/stats.c
APD_SRC    := src/apd/apd.c src/apd/server.c src/apd/telemetry.c \
              src/apd/hwctl.c

DEMOS := \
	$(DEMOBIN)/memlayout \
	$(DEMOBIN)/syscall_demo \
	$(DEMOBIN)/vm_demo \
	$(DEMOBIN)/process_demo \
	$(DEMOBIN)/thread_demo \
	$(DEMOBIN)/pipe_fifo_demo \
	$(DEMOBIN)/mqueue_demo \
	$(DEMOBIN)/shm_sem_demo \
	$(DEMOBIN)/signal_demo \
	$(DEMOBIN)/tcp_echo_server \
	$(DEMOBIN)/tcp_client \
	$(DEMOBIN)/udp_telemetry

.PHONY: all apps demos module test run clean help

all: apps demos

apps: $(BIN)/apd $(BIN)/apctl $(BIN)/apmon

demos: $(DEMOS)

$(BIN) $(DEMOBIN):
	@mkdir -p $@

# ---------------- chuong trinh chinh ----------------

$(BIN)/apd: $(APD_SRC) $(COMMON_SRC) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

$(BIN)/apctl: src/apctl/apctl.c src/common/util.c | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN)/apmon: src/apmon/apmon.c src/common/stats.c | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

# ---------------- demo theo tung chuong ----------------

$(DEMOBIN)/memlayout: demos/03-memory-layout/memlayout.c | $(DEMOBIN)
	$(CC) $(CFLAGS) $< -o $@

$(DEMOBIN)/syscall_demo: demos/04-syscall/syscall_demo.c | $(DEMOBIN)
	$(CC) $(CFLAGS) $< -o $@

$(DEMOBIN)/vm_demo: demos/06-virtual-memory/vm_demo.c | $(DEMOBIN)
	$(CC) $(CFLAGS) $< -o $@

$(DEMOBIN)/process_demo: demos/07-process/process_demo.c | $(DEMOBIN)
	$(CC) $(CFLAGS) $< -o $@

$(DEMOBIN)/thread_demo: demos/08-thread/thread_demo.c | $(DEMOBIN)
	$(CC) $(CFLAGS) -pthread $< -o $@

$(DEMOBIN)/pipe_fifo_demo: demos/09-ipc/pipe_fifo_demo.c | $(DEMOBIN)
	$(CC) $(CFLAGS) $< -o $@

$(DEMOBIN)/mqueue_demo: demos/09-ipc/mqueue_demo.c | $(DEMOBIN)
	$(CC) $(CFLAGS) $< -o $@ -lrt

$(DEMOBIN)/shm_sem_demo: demos/09-ipc/shm_sem_demo.c | $(DEMOBIN)
	$(CC) $(CFLAGS) -pthread $< -o $@ -lrt

$(DEMOBIN)/signal_demo: demos/10-signal/signal_demo.c | $(DEMOBIN)
	$(CC) $(CFLAGS) $< -o $@

$(DEMOBIN)/tcp_echo_server: demos/13-socket/tcp_echo_server.c | $(DEMOBIN)
	$(CC) $(CFLAGS) $< -o $@

$(DEMOBIN)/tcp_client: demos/13-socket/tcp_client.c | $(DEMOBIN)
	$(CC) $(CFLAGS) $< -o $@

$(DEMOBIN)/udp_telemetry: demos/13-socket/udp_telemetry.c | $(DEMOBIN)
	$(CC) $(CFLAGS) $< -o $@

# ---------------- kernel module ----------------

module:
	@sh scripts/build_module.sh

# ---------------- chay va kiem thu ----------------

test: all
	@sh tests/run_tests.sh

run: apps
	@mkdir -p /tmp/ap6
	@cp -n config/ap6.dat /tmp/ap6/ap6.dat 2>/dev/null || true
	$(BIN)/apd -p /tmp/ap6/ap6.dat -s /tmp/ap6/apd.sock -t 9000 -v

stages:
	@sh demos/02-basic-c/build_stages.sh

clean:
	rm -rf $(BUILD) demos/02-basic-c/out
	-$(MAKE) -C kernel/ap6sim clean 2>/dev/null || true

help:
	@sed -n '1,20p' Makefile
