include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-modemwebui
PKG_RELEASE:=1

PKG_LICENSE:=Apache-2.0
PKG_MAINTAINER:=OpenWrt LuCI community

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-modemwebui
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI support for Modem WebUI
  DEPENDS:=+luci-base +luci-lua-runtime +libc +libpthread +coreutils-sha1sum +ubus-at-daemon
endef

define Package/luci-app-modemwebui/description
  LuCI support for Modem WebUI (includes webuiserver binary).
endef

define Build/Compile
endef

define Package/luci-app-modemwebui/install
	$(INSTALL_DIR) $(1)
	$(CP) ./root/. $(1)/
	$(INSTALL_BIN) ./root/etc/init.d/modemwebui $(1)/etc/init.d/modemwebui
	$(INSTALL_BIN) ./root/etc/modemwebui/spoof-board.sh $(1)/etc/modemwebui/spoof-board.sh
	$(INSTALL_BIN) ./root/usr/bin/webuiserver $(1)/usr/bin/webuiserver
	$(INSTALL_BIN) ./root/usr/bin/sendat $(1)/usr/bin/sendat
	$(CP) ./root/www/webui/webui000/config.json $(1)/www/webui/webui000/config.json.bak
	$(CP) ./root/www/webui/webui5700/config.json $(1)/www/webui/webui5700/config.json.bak
endef

$(eval $(call BuildPackage,luci-app-modemwebui))
