include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-mipiscreen
PKG_VERSION:=2.0.0
PKG_RELEASE:=1

PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=ning

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-mipiscreen
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=Commercial MIPI touch dashboard for LuCI
  DEPENDS:=+libubox +uci +rpcd +luci-base +kmod-hwmon-pwmfan \
	+kmod-input-evdev +kmod-input-touchscreen-edt-ft5x06 +i2c-tools
endef

define Package/luci-app-mipiscreen/description
  LuCI interface and background native UI daemon for MIPI screens.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) $(TARGET_LDFLAGS) \
		-Wall -O2 -std=c99 \
		-o $(PKG_BUILD_DIR)/mipiscreen-ui \
		$(PKG_BUILD_DIR)/mipiscreen-ui.c -lm
endef

define Package/luci-app-mipiscreen/install
	# Install background daemon
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/mipiscreen-ui $(1)/usr/sbin/mipiscreen-ui

	# Install system configs, init script, rpcd script
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./root/etc/config/mipiscreen $(1)/etc/config/mipiscreen
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./root/etc/init.d/mipiscreen $(1)/etc/init.d/mipiscreen
	$(INSTALL_DIR) $(1)/usr/libexec/rpcd
	$(INSTALL_BIN) ./root/usr/libexec/rpcd/mipiscreen $(1)/usr/libexec/rpcd/mipiscreen

	# Install LuCI JSON menu and ACLs
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_CONF) ./root/usr/share/luci/menu.d/luci-app-mipiscreen.json $(1)/usr/share/luci/menu.d/luci-app-mipiscreen.json
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_CONF) ./root/usr/share/rpcd/acl.d/luci-app-mipiscreen.json $(1)/usr/share/rpcd/acl.d/luci-app-mipiscreen.json

	# Install LuCI JS views and CSS files
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/mipiscreen
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/mipiscreen/settings.js $(1)/www/luci-static/resources/view/mipiscreen/settings.js
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/mipiscreen/settings.css $(1)/www/luci-static/resources/view/mipiscreen/settings.css
endef

define Package/luci-app-mipiscreen/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || /etc/init.d/rpcd restart 2>/dev/null || true
exit 0
endef

$(eval $(call BuildPackage,luci-app-mipiscreen))
