'use strict';
'require view';
'require uci';
'require rpc';
'require form';
'require poll';
'require ui';

document.head.appendChild(E('link', {
	'rel': 'stylesheet',
	'type': 'text/css',
	'href': L.resource('view/mipiscreen/settings.css')
}));

const CFG = 'mipiscreen';
const SECTION = 'global';

const callStatus = rpc.declare({
	object: 'mipiscreen',
	method: 'get_status',
	expect: {}
});

const callControl = rpc.declare({
	object: 'mipiscreen',
	method: 'control_device',
	params: [ 'action' ],
	expect: {}
});

const callTestScreen = rpc.declare({
	object: 'mipiscreen',
	method: 'test_screen',
	params: [ 'pattern' ],
	expect: {}
});

const callInitAction = rpc.declare({
	object: 'service',
	method: 'reload',
	params: [ 'name' ],
	expect: {}
});

return view.extend({
	load: function() {
		return Promise.all([
			L.resolveDefault(uci.load(CFG), null),
			L.resolveDefault(callStatus(), {})
		]);
	},

	renderStatusCard: function(status) {
		const rotationText = (status.rotation == 90 || status.rotation == 270)
			? _('横屏') : _('竖屏');
		const touchText = status.touch_present
			? (status.touch_name || _('已连接')) : _('未检测到');
		const card = E('div', { 'class': 'ms-card ms-status-card' }, [
			E('h3', {}, _('NWRT 设备运行总览')),
			E('div', { 'class': 'ms-status-grid' }, [
				E('div', { 'class': 'ms-status-item' }, [
					E('span', { 'class': 'label' }, _('显示方向')),
					E('span', { 'class': 'value', 'id': 'ms-cur-rotation' }, rotationText)
				]),
				E('div', { 'class': 'ms-status-item' }, [
					E('span', { 'class': 'label' }, _('当前背光状态')),
					E('span', { 'class': 'value text-success', 'id': 'ms-cur-brightness' }, (status.backlight == 1 ? _('已开启') : _('已关闭')))
				]),
				E('div', { 'class': 'ms-status-item' }, [
					E('span', { 'class': 'label' }, _('CPU / 内存 / 存储')),
					E('span', { 'class': 'value', 'id': 'ms-cur-sys' }, (status.cpu_usage || '0') + '% / ' + (status.mem_usage || '0') + '% / ' + (status.storage_usage || '0') + '%')
				]),
				E('div', { 'class': 'ms-status-item' }, [
					E('span', { 'class': 'label' }, _('温度 / CPU 频率')),
					E('span', { 'class': 'value', 'id': 'ms-cur-thermal' }, (status.cpu_temp || '0') + '°C / ' + (status.cpu_freq || '0') + ' MHz')
				]),
				E('div', { 'class': 'ms-status-item' }, [
					E('span', { 'class': 'label' }, _('系统负载 / 运行时间')),
					E('span', { 'class': 'value', 'id': 'ms-cur-runtime' }, (status.load_avg || '--') + ' / ' + Math.floor((status.uptime || 0) / 60) + ' min')
				]),
				E('div', { 'class': 'ms-status-item' }, [
					E('span', { 'class': 'label' }, _('触摸控制器')),
					E('span', { 'class': 'value ' + (status.touch_present ? 'text-success' : 'text-danger'), 'id': 'ms-cur-touch' }, touchText)
				]),
				E('div', { 'class': 'ms-status-item' }, [
					E('span', { 'class': 'label' }, _('蜂窝网络')),
					E('span', { 'class': 'value ' + (status.cell_status === 'connected' ? 'text-success' : 'text-danger'), 'id': 'ms-cur-5g' }, status.cell_status === 'connected' ? _('已连接') : _('未就绪'))
				]),
				E('div', { 'class': 'ms-status-item' }, [
					E('span', { 'class': 'label' }, _('WAN / LAN')),
					E('span', { 'class': 'value', 'id': 'ms-cur-network' }, (status.wan_ip || '--') + ' / ' + (status.lan_ip || '--'))
				]),
				E('div', { 'class': 'ms-status-item' }, [
					E('span', { 'class': 'label' }, _('真实在线客户端 / 存储可用')),
					E('span', { 'class': 'value', 'id': 'ms-cur-clients' }, (status.clients || 0) + ' / ' + (status.storage_free || 0) + ' MB')
				])
			])
		]);
		return card;
	},

	getThemeName: function(theme) {
		switch (theme) {
			case 'orange': return _('活力橙');
			case 'cyan': return _('科技青蓝');
			case 'light': return _('简约明亮');
			case 'dark':
			default: return _('商务深黑');
		}
	},

	pollStatus: function() {
		return L.resolveDefault(callStatus(), {}).then(L.bind(function(status) {
			const bVal = document.getElementById('ms-cur-brightness');
			if (bVal) bVal.textContent = (status.backlight == 1 ? _('已开启') : _('已关闭'));
			
			const sysVal = document.getElementById('ms-cur-sys');
			if (sysVal) sysVal.textContent = (status.cpu_usage || '0') + '% / ' + (status.mem_usage || '0') + '% / ' + (status.storage_usage || '0') + '%';
			const thermalVal = document.getElementById('ms-cur-thermal');
			if (thermalVal) thermalVal.textContent = (status.cpu_temp || '0') + '°C / ' + (status.cpu_freq || '0') + ' MHz';
			const runtimeVal = document.getElementById('ms-cur-runtime');
			if (runtimeVal) runtimeVal.textContent = (status.load_avg || '--') + ' / ' + Math.floor((status.uptime || 0) / 60) + ' min';
			
			const gVal = document.getElementById('ms-cur-5g');
			if (gVal) {
				gVal.textContent = status.cell_status === 'connected' ? _('已连接') : _('未就绪');
				gVal.className = 'value ' + (status.cell_status === 'connected' ? 'text-success' : 'text-danger');
			}
			
			const touchVal = document.getElementById('ms-cur-touch');
			if (touchVal) {
				touchVal.textContent = status.touch_present ? (status.touch_name || _('已连接')) : _('未检测到');
				touchVal.className = 'value ' + (status.touch_present ? 'text-success' : 'text-danger');
			}
			const networkVal = document.getElementById('ms-cur-network');
			if (networkVal) networkVal.textContent = (status.wan_ip || '--') + ' / ' + (status.lan_ip || '--');
			const clientsVal = document.getElementById('ms-cur-clients');
			if (clientsVal) clientsVal.textContent = (status.clients || 0) + ' / ' + (status.storage_free || 0) + ' MB';
			const rotationVal = document.getElementById('ms-cur-rotation');
			if (rotationVal) rotationVal.textContent = (status.rotation == 90 || status.rotation == 270) ? _('横屏') : _('竖屏');
		}, this));
	},

	render: function(res) {
		const status = res[1] || {};

		// Form section
		const m = new form.Map(CFG, _('MIPI 商业智能控制台'), _('配置设备本地的屏幕展示参数。支持深色商业主题，可触摸交互，实时掌握核心运行数据。'));
		
		m.on_after_commit = L.bind(function() {
			return callInitAction('mipiscreen').then(() => {
				ui.addNotification(null, E('p', _('设置已保存并同步至物理屏幕。')), 'info');
			});
		}, this);

		const s = m.section(form.NamedSection, SECTION, 'mipiscreen');
		s.anonymous = true;

		// 1. Enabled
		let o = s.option(form.Flag, 'enabled', _('主屏幕电源'), _('关闭后屏幕完全熄灭，触控功能休眠。'));
		o.default = '1';
		o.rmempty = false;

		// 2. Backlight Switch
		o = s.option(form.Flag, 'backlight', _('屏幕背光开关'), _('开启或关闭屏幕物理背光。'));
		o.default = '1';
		o.rmempty = false;

		o = s.option(form.ListValue, 'rotation', _('屏幕方向'), _('默认使用适配当前安装方向的横屏，可随时切换为另一方向或竖屏。'));
		o.value('270', _('横屏（默认）'));
		o.value('90', _('横屏（反向）'));
		o.value('0', _('竖屏（接口位于底部）'));
		o.value('180', _('竖屏（倒置）'));
		o.default = '270';
		o.rmempty = false;

		o = s.option(form.Flag, 'touch_wake', _('触摸唤醒'), _('息屏后第一次触摸只唤醒屏幕，不触发界面按钮。'));
		o.default = '1';
		o.rmempty = false;

		o = s.option(form.Flag, 'boot_animation', _('商业开机动画'), _('设备启动时显示 NWRT 品牌动画和服务就绪进度。'));
		o.default = '1';
		o.rmempty = false;

		// Screen Timeout
		o = s.option(form.ListValue, 'timeout', _('无触摸自动熄屏'), _('每次触摸都会重新开始计时；默认无操作 5 分钟后关闭背光。'));
		o.value('0', _('始终常亮 (演示模式)'));
		o.value('15', _('15 秒'));
		o.value('30', _('30 秒'));
		o.value('60', _('1 分钟'));
		o.value('300', _('5 分钟（默认）'));
		o.value('600', _('10 分钟'));
		o.default = '300';

		// 4. GUI Themes
		o = s.option(form.ListValue, 'theme', _('界面色彩主题'), _('推荐使用“商务深黑”，配合商业场景效果最佳。'));
		o.value('dark', _('商务深黑 (推荐)'));
		o.value('cyan', _('科技青蓝'));
		o.value('orange', _('活力橙'));
		o.value('light', _('简约明亮'));
		o.default = 'dark';

		o = s.option(form.DummyValue, '_theme_preview', _('主题预览'));
		o.rawhtml = true;
		o.render = function() {
			return E('div', { 'class': 'ms-theme-preview' }, [
				E('div', { 'class': 'ms-theme-chip dark', 'title': 'dark' }),
				E('div', { 'class': 'ms-theme-chip cyan', 'title': 'cyan' }),
				E('div', { 'class': 'ms-theme-chip orange', 'title': 'orange' }),
				E('div', { 'class': 'ms-theme-chip light', 'title': 'light' })
			]);
		};

		// 5. Pages / Display modules
		o = s.option(form.Flag, 'show_system', _('启用：硬件监控面板'), _('允许在屏幕展示核心状态 (CPU/内存/温度)。'));
		o.default = '1';
		o.rmempty = false;

		o = s.option(form.Flag, 'show_network', _('启用：5G通信面板'), _('允许在屏幕展示蜂窝网络、速率等。'));
		o.default = '1';
		o.rmempty = false;

		// Actions Grid
		const actionsCard = E('div', { 'class': 'ms-card ms-actions-card' }, [
			E('h3', {}, _('硬件级快捷运维操作')),
			E('div', { 'class': 'ms-btn-grid' }, [
				E('button', {
					'class': 'cbi-button cbi-button-action',
					'click': ui.createHandlerFn(this, function() {
						return callInitAction('mipiscreen').then(() => {
							ui.addNotification(null, E('p', _('屏幕界面进程已刷新')), 'info');
						});
					})
				}, _('重载屏幕进程')),
				E('button', {
					'class': 'cbi-button cbi-button-neutral',
					'click': ui.createHandlerFn(this, function() {
						return callControl('reconnect_5g').then(() => {
							ui.addNotification(null, E('p', _('已发送 5G 模块重启指令')), 'info');
						});
					})
				}, _('重启 5G 模块')),
				E('button', {
					'class': 'cbi-button cbi-button-neutral',
					'click': ui.createHandlerFn(this, function() {
						if (!confirm(_('清理文件系统缓存可能短暂增加磁盘读取，确定继续吗？')))
							return Promise.resolve();
						return callControl('clear_ram').then(() => {
							ui.addNotification(null, E('p', _('缓存已清理，内存已释放')), 'info');
						});
					})
				}, _('一键清理内存')),
				E('button', {
					'class': 'cbi-button cbi-button-reset',
					'click': ui.createHandlerFn(this, function() {
						if (confirm(_('即将向主板发送强制重启信号，确定吗？'))) {
							return callControl('reboot');
						}
					})
				}, _('重启主板设备'))
			])
		]);

		const diagnosticsCard = E('div', { 'class': 'ms-card ms-actions-card' }, [
			E('h3', {}, _('屏幕硬件诊断')),
			E('p', { 'class': 'ms-card-help' }, _('测试画面持续 5 秒，用于检查颜色顺序、固定亮线与面板扫描。')),
			E('div', { 'class': 'ms-btn-grid ms-test-grid' }, [
				['red', _('纯红')], ['green', _('纯绿')], ['blue', _('纯蓝')],
				['white', _('纯白')], ['gray', _('灰阶')], ['grid', _('网格')]
			].map(function(item) {
				return E('button', {
					'class': 'cbi-button cbi-button-neutral ms-test-' + item[0],
					'click': ui.createHandlerFn(this, function() {
						return callTestScreen(item[0]);
					})
				}, item[1]);
			}))
		]);

		// Layout render assembly
		return m.render().then(L.bind(function(mapEl) {
			const container = E('div', { 'class': 'ms-settings-layout' }, [
				this.renderStatusCard(status),
				mapEl,
				diagnosticsCard,
				actionsCard
			]);

			// Setup polling
			poll.add(L.bind(this.pollStatus, this), 3);

			return container;
		}, this));
	}
});
