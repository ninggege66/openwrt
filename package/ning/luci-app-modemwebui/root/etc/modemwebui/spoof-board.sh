#!/bin/sh
# spoof-board.sh - 为 webuiserver 准备运行环境
#
# webuiserver 需要：
#   1. board_name = "hiveton,h5000m"（硬件检测）
#   2. /dev/ttyUSBx 格式的端口（只识别 ttyUSB 格式提取端口号）
#   3. sendat 可执行
#   4. modemwebui UCI 配置 + network 接口映射（实时网速 / 流量统计）

SYSINFO_DIR="/tmp/sysinfo"
BOARD_NAME_FILE="$SYSINFO_DIR/board_name"
SPOOF_VALUE="hiveton,h5000m"

# 1. 欺骗 board_name
mkdir -p "$SYSINFO_DIR"
echo "$SPOOF_VALUE" > "$BOARD_NAME_FILE"

# 2. 获取真实 AT 端口（通用，支持 USB/PCIe 任意模组）
AT_DEV=$(uci -q get qmodem.@modem-device[0].at_port 2>/dev/null)
if [ -z "$AT_DEV" ]; then
    AT_DEV=$(ubus call at-daemon list 2>/dev/null | jsonfilter -e '@.ports[0].port' 2>/dev/null)
fi

# 3. 创建 /dev/ttyUSB0 软链接
# webuiserver 只识别 ttyUSBx 格式，用软链接让它通过端口检测
# sendat 脚本内部走 ubus at-daemon，不会真正独占该设备
if [ -n "$AT_DEV" ] && [ -e "$AT_DEV" ]; then
    ln -sf "$AT_DEV" /dev/ttyUSB0
fi

# 4. 确保 sendat 可执行
chmod +x /usr/bin/sendat 2>/dev/null

# 5. 欺骗 UCI 网络接口配置（解决实时网速和流量统计为0的问题）
#
# webuiserver 查询网速的真实命令链（从二进制内嵌JS提取）：
#   I=$(grep ... /etc/config/qmodem | grep data_interface | ...)  → 得到 "pcie"
#   I=$(echo "$I" | tr 'a-z' 'A-Z')                              → 转大写 "PCIE"
#   D=$(uci get network.$I.device)                                → 查 network.PCIE.device
#   cat /proc/net/dev | grep "$D:"                               → grep "wwan0:"
#
# 原始 h5000m 设备的 data_interface 就是网卡名(如usb0/eth1)，直接能查到。
# 我们的设备 data_interface='pcie'（连接类型不是网卡名），导致 network.PCIE 不存在。
# 需要建立 PCIE -> wwan0 的映射。

# 5a. 自动探测数据网络接口
DATA_IFACE="wwan0"
if ! ip link show "$DATA_IFACE" >/dev/null 2>&1; then
    DATA_IFACE=$(uci -q get qmodem.@modem-device[0].network 2>/dev/null)
fi

# 5b. 从 qmodem 配置读取 data_interface 字段（二进制实际用这个值查UCI）
DATA_INTF=$(uci -q get qmodem.@modem-device[0].data_interface 2>/dev/null)
if [ -z "$DATA_INTF" ]; then
    DATA_INTF="pcie"
fi

# 5c. 创建关键映射：network.<DATA_INTERFACE大写>.device = 实际网卡名
INTF_UPPER=$(echo "$DATA_INTF" | tr 'a-z' 'A-Z')
uci -q set "network.$INTF_UPPER=interface"
uci -q set "network.$INTF_UPPER.device=$DATA_IFACE"

# 5d. 也创建 modemwebui UCI 配置（部分命令路径可能用这个）
touch /etc/config/modemwebui 2>/dev/null
uci -q set modemwebui.main=main
uci -q set modemwebui.main.ifname="$DATA_IFACE"

# 5e. 额外创建 WWAN0 映射（兼容另一条命令链）
WWAN_UPPER=$(echo "$DATA_IFACE" | tr 'a-z' 'A-Z')
uci -q set "network.$WWAN_UPPER=interface" 2>/dev/null
uci -q set "network.$WWAN_UPPER.device=$DATA_IFACE" 2>/dev/null

uci commit modemwebui 2>/dev/null
uci commit network

exit 0
