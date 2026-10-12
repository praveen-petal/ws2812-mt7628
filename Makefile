#
# OpenWrt Package Makefile for ws2812-draiveris
# Target: ramips/mt76x8 (MediaTek MT7628 / MT7688)
#
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=ws2812-draiveris
PKG_VERSION:=0.1
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)
PKG_CHECK_FORMAT_SECURITY:=0

include $(INCLUDE_DIR)/package.mk

define KernelPackage/$(PKG_NAME)
  SUBMENU:=Other modules
  TITLE:=MediaTek MT7628 WS2812 NeoPixel Driver
  FILES:=$(PKG_BUILD_DIR)/ws2812-draiveris.ko
  AUTOLOAD:=$(call AutoLoad,92,ws2812-draiveris)
  KCONFIG:=
endef

define KernelPackage/$(PKG_NAME)/description
  Bitbang kernel driver for 24-LED WS2812 ring on MediaTek MT7628 GPIO 11.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

MAKE_OPTS:= \
	ARCH="$(LINUX_KARCH)" \
	CROSS_COMPILE="$(TARGET_CROSS)" \
	M="$(PKG_BUILD_DIR)" \
	SUBDIRS="$(PKG_BUILD_DIR)"

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" \
		$(MAKE_OPTS) \
		modules
endef

$(eval $(call KernelPackage,$(PKG_NAME)))
