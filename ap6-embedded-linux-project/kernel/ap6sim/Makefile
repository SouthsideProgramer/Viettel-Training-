# Makefile kbuild cho module ap6sim (Bao cao muc 11.2)
#
#   make            -> build module cho kernel dang chay
#   make load       -> insmod + hien thi dmesg
#   make unload     -> rmmod
#   make clean
#
# Cross-compile cho board ARM:
#   make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
#        KDIR=/duong/dan/den/kernel-source

obj-m += ap6sim.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

load: all
	sudo insmod ap6sim.ko
	@sleep 1
	@dmesg | tail -5
	@ls -l /dev/ap6sim

unload:
	-sudo rmmod ap6sim
	@dmesg | tail -3

.PHONY: all clean load unload
