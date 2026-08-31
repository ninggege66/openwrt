module("luci.controller.modemwebui", package.seeall)

function index()
	-- 始终显示菜单，不依赖模组是否存在
	entry({"admin", "modem", "modemwebui"}, template("modemwebui/modemwebui"), _("模组管理UI"), 10).leaf = true
end
