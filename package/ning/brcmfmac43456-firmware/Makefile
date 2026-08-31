# SPDX-License-Identifier: GPL-2.0-or-later
# BCM43456 WiFi firmware package for 100ASK DshanPI-R1 board

include $(TOPDIR)/rules.mk

PKG_NAME:=brcmfmac43456-firmware
PKG_VERSION:=1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=ning

include $(INCLUDE_DIR)/package.mk

define Package/brcmfmac43456-firmware
  SECTION:=firmware
  CATEGORY:=Firmware
  TITLE:=BCM43456 WiFi firmware for 100ASK DshanPI-R1
  URL:=https://github.com/100askTeam/
endef

define Package/brcmfmac43456-firmware/description
  BCM43456 / CYW43456 SDIO WiFi firmware and NVRAM config for the
  100ASK DshanPI-R1 (RK3568B2) board.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./files/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	true
endef

define Package/brcmfmac43456-firmware/install
	$(INSTALL_DIR) $(1)/lib/firmware/brcm
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/brcmfmac43456-sdio.bin \
		$(1)/lib/firmware/brcm/brcmfmac43456-sdio.bin
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/brcmfmac43456-sdio.txt \
		$(1)/lib/firmware/brcm/brcmfmac43456-sdio.txt
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/brcmfmac43456-sdio.clm_blob \
		$(1)/lib/firmware/brcm/brcmfmac43456-sdio.clm_blob
	# Board-specific NVRAM symlink for 100ask,dshanpi-r1
	$(LN) brcmfmac43456-sdio.txt \
		$(1)/lib/firmware/brcm/brcmfmac43456-sdio.100ask,dshanpi-r1.txt
endef

$(eval $(call BuildPackage,brcmfmac43456-firmware))
