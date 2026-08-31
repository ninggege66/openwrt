include $(TOPDIR)/rules.mk

LUCI_TITLE:=Ventusky interactive weather map
LUCI_DEPENDS:=+luci-base
LUCI_PKGARCH:=all
PKG_LICENSE:=Apache-2.0
PKG_MAINTAINER:=ninggege66

include $(TOPDIR)/feeds/luci/luci.mk

define Package/$(PKG_NAME)/conffiles
/etc/config/ventusky
endef

# call BuildPackage - OpenWrt buildroot signature
