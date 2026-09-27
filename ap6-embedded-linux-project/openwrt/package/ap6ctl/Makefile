# Makefile - package OpenWrt cho ap6ctl (Bao cao muc 14.2, 14.3)
#
# Cach dung:
#   1. Copy thu muc nay vao <openwrt>/package/utils/ap6ctl
#   2. cd <openwrt> && ./scripts/feeds update -a && ./scripts/feeds install -a
#   3. make menuconfig  ->  Utilities  --->  <*> ap6ctl
#   4. make package/ap6ctl/compile V=s
#   5. scp bin/packages/*/base/ap6ctl_*.ipk root@192.168.1.1:/tmp
#      ssh root@192.168.1.1 "opkg install /tmp/ap6ctl_*.ipk"
#
# OpenWrt Build System se tu chon toolchain cross-compile dung cho target
# (MIPS/ARM), dat CC/CFLAGS/LDFLAGS va staging dir - chuong trinh C khong
# can biet minh dang duoc build cho kien truc nao.

include $(TOPDIR)/rules.mk

PKG_NAME:=ap6ctl
PKG_VERSION:=1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=Bao cao thuc tap - Viettel High Tech
PKG_LICENSE:=GPL-2.0

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/ap6ctl
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Daemon quan ly Wi-Fi AP (apd/apctl/apmon)
  DEPENDS:=+libpthread +librt
  URL:=https://openwrt.org
endef

define Package/ap6ctl/description
  Daemon quan ly Access Point minh hoa cho bao cao thuc tap Embedded Linux:
  control path qua Unix domain socket va TCP, telemetry qua shared memory,
  xu ly signal, fork/exec de ap dung cau hinh, ioctl xuong driver Wi-Fi.
endef

# Duong dan toi thu muc du an (chua Makefile va src/).
# Sua bien nay cho phu hop, hoac tot hon la dung PKG_SOURCE_URL + PKG_SOURCE_VERSION
# de OpenWrt tu clone tu GitLab:
#   PKG_SOURCE_PROTO:=git
#   PKG_SOURCE_URL:=git@gitlab.company.vn:intern/ap6-embedded-linux.git
#   PKG_SOURCE_VERSION:=<sha hoac tag>
AP6_PROJECT_DIR?=$(TOPDIR)/../ap6-embedded-linux-project

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(AP6_PROJECT_DIR)/Makefile $(PKG_BUILD_DIR)/
	$(CP) $(AP6_PROJECT_DIR)/src $(PKG_BUILD_DIR)/
	$(CP) $(AP6_PROJECT_DIR)/kernel $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -Wall -Wextra -std=gnu11" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		apps
endef

define Package/ap6ctl/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/build/bin/apd   $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/build/bin/apctl $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/build/bin/apmon $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/etc/ap6
	$(INSTALL_DATA) ./files/ap6.dat $(1)/etc/ap6/ap6.dat

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_DATA) ./files/ap6.config $(1)/etc/config/ap6

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/ap6d.init $(1)/etc/init.d/ap6d

	$(INSTALL_DIR) $(1)/usr/share/ap6
	$(INSTALL_BIN) ./files/apply_profile.sh $(1)/usr/share/ap6/apply_profile.sh
endef

# File cau hinh duoc giu lai khi sysupgrade (khong bi ghi de).
define Package/ap6ctl/conffiles
/etc/config/ap6
/etc/ap6/ap6.dat
endef

$(eval $(call BuildPackage,ap6ctl))
