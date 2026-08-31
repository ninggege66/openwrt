include $(TOPDIR)/rules.mk

LUCI_TITLE:=LiberNet 风扇控制
LUCI_DEPENDS:=+luci-base +rpcd +coreutils-sleep +kmod-hwmon-pwmfan
LUCI_PKGARCH:=all
PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=LiberNet

include $(TOPDIR)/feeds/luci/luci.mk

define Package/$(PKG_NAME)/postinst
#!/bin/sh
ROOT="$${IPKG_INSTROOT}"
chmod 0755 "$${ROOT}/usr/libexec/rpcd/libernet_fan" 2>/dev/null || true
chmod 0755 "$${ROOT}/usr/libexec/libernet-fan-daemon" 2>/dev/null || true
chmod 0755 "$${ROOT}/etc/init.d/libernet-fan" 2>/dev/null || true
exit 0
endef

# call BuildPackage - OpenWrt buildroot signature
