include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-emulatorjs
PKG_RELEASE:=1

PKG_LICENSE:=MIT
PKG_MAINTAINER:=OpenWrt Community

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-emulatorjs
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=Retro Game Emulator (EmulatorJS)
  DEPENDS:=+luci-base +luci-lua-runtime
endef

define Package/luci-app-emulatorjs/description
  A completely offline web-based retro game emulator using EmulatorJS.
  Includes all emulator cores inside the package.
endef

define Build/Compile
endef

define Package/luci-app-emulatorjs/install
	# 安装 LuCI 路由和视图以及自带的 EmulatorJS 核心文件
	$(INSTALL_DIR) $(1)
	$(CP) ./root/* $(1)/
	
	# 创建 ROM 存放目录
	$(INSTALL_DIR) $(1)/www/emulatorjs/roms/nes
	$(INSTALL_DIR) $(1)/www/emulatorjs/roms/snes
	$(INSTALL_DIR) $(1)/www/emulatorjs/roms/gba
	$(INSTALL_DIR) $(1)/www/emulatorjs/roms/arcade
endef

$(eval $(call BuildPackage,luci-app-emulatorjs))
