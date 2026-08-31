'use strict';
'require view';
'require uci';
'require rpc';
'require poll';
'require ui';

document.head.appendChild(E('link', {
	'rel': 'stylesheet',
	'type': 'text/css',
	'href': L.resource('view/libernet/fan.css')
}));

const CFG = 'libernet-fan';
const SECTION = 'main';
const HISTORY_LIMIT = 30;

const DEFAULTS = {
	enabled: '1',
	mode: 'smart',
	silent_pwm: '15',
	turbo_pwm: '100',
	manual_pwm: '35',
	smart_target_temp: '50',
	smart_min_pwm: '15',
	smart_max_temp: '80',
	smart_kp: '3',
	smooth_step: '4',
	interval: '2'
};

const callBoard = rpc.declare({
	object: 'system',
	method: 'board'
});

const callStatus = rpc.declare({
	object: 'libernet_fan',
	method: 'status',
	expect: {}
});

const callSetSpeed = rpc.declare({
	object: 'libernet_fan',
	method: 'set_speed',
	expect: {}
});

const callApply = rpc.declare({
	object: 'libernet_fan',
	method: 'apply',
	params: {
		enabled: true,
		mode: true,
		silent_pwm: true,
		turbo_pwm: true,
		manual_pwm: true,
		smart_target_temp: true,
		smart_min_pwm: true,
		smart_max_temp: true,
		smart_kp: true,
		smooth_step: true,
		interval: true
	},
	expect: {}
});

function toInt(value, fallback) {
	const n = parseInt(value, 10);
	return Number.isFinite(n) ? n : fallback;
}

function clamp(value, min, max) {
	return Math.max(min, Math.min(max, value));
}

function getConfigValue(name) {
	const value = uci.get(CFG, SECTION, name);
	return (value == null || value === '') ? DEFAULTS[name] : value;
}

function formatPercent(value) {
	return clamp(toInt(value, 0), 0, 100) + '%';
}

function formatTemp(value) {
	return clamp(toInt(value, 0), 0, 200) + '°C';
}

function formatMode(mode) {
	switch (mode) {
		case 'silent':
			return _('静音模式');
		case 'turbo':
			return _('极速模式');
		case 'manual':
			return _('自定义');
		case 'smart':
		default:
			return _('智能模式');
	}
}

function spinDuration(speed) {
	const pct = clamp(toInt(speed, 0), 0, 100);

	if (pct <= 0)
		return 999;

	return Math.max(0.55, 13 - (pct * 0.125));
}

function thermalInfo(temp) {
	const value = clamp(toInt(temp, 0), 0, 200);

	if (value >= 75)
		return { key: 'critical', label: _('高温告警'), color: '#ff5d5d', glow: 'rgba(255, 93, 93, 0.45)', hint: _('立即加强散热') };

	if (value >= 60)
		return { key: 'hot', label: _('高温运行'), color: '#ff8a4c', glow: 'rgba(255, 138, 76, 0.38)', hint: _('温度偏高，风扇提速中') };

	if (value >= 45)
		return { key: 'warm', label: _('温度上升'), color: '#f5b94c', glow: 'rgba(245, 185, 76, 0.30)', hint: _('正在平滑调速') };

	return { key: 'cool', label: _('温度正常'), color: '#5ea69b', glow: 'rgba(94, 166, 155, 0.24)', hint: _('运行稳定') };
}

function sparklinePath(values, width, height, maxValue) {
	if (!Array.isArray(values) || values.length === 0)
		return '';

	const max = Math.max(1, maxValue || 100);
	const points = [];

	for (let i = 0; i < values.length; i++) {
		const x = (values.length === 1) ? width / 2 : (i * width / (values.length - 1));
		const y = height - ((clamp(values[i], 0, max) / max) * height);
		points.push([x.toFixed(1), y.toFixed(1)]);
	}

	return points.map(function (point, idx) {
		return (idx === 0 ? 'M' : 'L') + point[0] + ' ' + point[1];
	}).join(' ');
}

function sparklineFillPath(values, width, height, maxValue) {
	const line = sparklinePath(values, width, height, maxValue);

	if (!line)
		return '';

	const lastX = width.toFixed(1);
	const firstX = '0.0';
	const baseY = height.toFixed(1);

	return line + ' L ' + lastX + ' ' + baseY + ' L ' + firstX + ' ' + baseY + ' Z';
}

return view.extend({
	load: function () {
		return Promise.all([
			L.resolveDefault(uci.load(CFG), null),
			callBoard(),
			L.resolveDefault(callStatus(), {})
		]);
	},

	renderMetricCard: function (key, title, accent, maxValue) {
		const card = E('div', { 'class': 'lf-card lf-metric-card', 'data-key': key }, [
			E('div', { 'class': 'lf-card-head' }, [
				E('div', { 'class': 'lf-card-title' }, title),
				E('div', { 'class': 'lf-card-value', 'id': 'lf-' + key + '-value' }, '--')
			]),
			E('div', { 'class': 'lf-card-sub', 'id': 'lf-' + key + '-sub' }, '--'),
			E('div', { 'class': 'lf-thermal-row' }, [
				E('span', { 'class': 'lf-thermal-badge', 'id': 'lf-' + key + '-thermal' }, _('温度正常')),
				E('div', { 'class': 'lf-heat-meter' }, [
					E('span', { 'class': 'lf-heat-fill', 'id': 'lf-' + key + '-heat' }),
					E('i', { 'class': 'lf-heat-dot' })
				])
			]),
			E('svg', {
				'class': 'lf-sparkline',
				'viewBox': '0 0 100 34',
				'preserveAspectRatio': 'none'
			}, [
				E('path', {
					'id': 'lf-' + key + '-fill',
					'class': 'lf-spark-fill lf-accent-' + accent,
					'd': ''
				}),
				E('path', {
					'id': 'lf-' + key + '-line',
					'class': 'lf-spark-line lf-accent-' + accent,
					'd': ''
				})
			])
		]);

		this.refs.metrics[key] = {
			card: card,
			value: card.querySelector('#lf-' + key + '-value'),
			sub: card.querySelector('#lf-' + key + '-sub'),
			badge: card.querySelector('#lf-' + key + '-thermal'),
			heat: card.querySelector('#lf-' + key + '-heat'),
			fill: card.querySelector('#lf-' + key + '-fill'),
			line: card.querySelector('#lf-' + key + '-line'),
			maxValue: maxValue
		};

		return card;
	},

	updateMetric: function (key, value, sub, historyValue) {
		const ref = this.refs.metrics[key];

		if (!ref)
			return;

		if (ref.value)
			ref.value.textContent = value;

		if (ref.sub)
			ref.sub.textContent = sub;

		if (ref.heat)
			ref.heat.style.width = clamp(toInt(historyValue, 0), 0, 100) + '%';

		if (!Array.isArray(this.history[key]))
			this.history[key] = [];

		this.history[key].push(historyValue);

		while (this.history[key].length > HISTORY_LIMIT)
			this.history[key].shift();

		const line = sparklinePath(this.history[key], 100, 34, ref.maxValue);
		const fill = sparklineFillPath(this.history[key], 100, 34, ref.maxValue);

		if (ref.line)
			ref.line.setAttribute('d', line);

		if (ref.fill)
			ref.fill.setAttribute('d', fill);
	},

	updateThermalCard: function (key, temp) {
		const ref = this.refs.metrics[key];

		if (!ref || !ref.card)
			return;

		const thermal = thermalInfo(temp);

		ref.card.dataset.thermal = thermal.key;
		ref.card.style.setProperty('--thermal-color', thermal.color);
		ref.card.style.setProperty('--thermal-glow', thermal.glow);

		if (ref.badge)
			ref.badge.textContent = thermal.label;
	},

	updateThermalTheme: function (temp, fanSpeed) {
		const thermal = thermalInfo(temp);

		if (this.refs.shell) {
			this.refs.shell.dataset.thermal = thermal.key;
			this.refs.shell.style.setProperty('--thermal-color', thermal.color);
			this.refs.shell.style.setProperty('--thermal-glow', thermal.glow);
		}

		if (this.refs.hero)
			this.refs.hero.dataset.thermal = thermal.key;

		if (this.refs.thermalLine)
			this.refs.thermalLine.textContent = _('整机：') + thermal.label + ' · ' + formatTemp(temp);

		if (this.refs.fanWheel) {
			this.refs.fanWheel.style.setProperty('--fan-spin', spinDuration(fanSpeed) + 's');
			this.refs.fanWheel.style.setProperty('--fan-speed', clamp(toInt(fanSpeed, 0), 0, 100) + '%');
		}
	},

	updateModeButtons: function () {
		const activeMode = this.state.mode;

		Object.keys(this.refs.modeButtons).forEach(L.bind(function (mode) {
			const btn = this.refs.modeButtons[mode];

			if (!btn)
				return;

			if (mode === activeMode)
				btn.classList.add('is-active');
			else
				btn.classList.remove('is-active');
		}, this));

		if (this.refs.slider)
			this.refs.slider.disabled = (activeMode !== 'manual');

		if (this.refs.sliderHint)
			this.refs.sliderHint.textContent = (activeMode === 'manual')
				? _('拖动即可立即生效，系统会自动记住当前自定义转速')
				: _('当前为模式控制，点击自定义后再拖动会立即生效');

		if (this.refs.modeValue)
			this.refs.modeValue.textContent = formatMode(activeMode);
	},

	updateSwitch: function () {
		if (this.refs.switchInput)
			this.refs.switchInput.checked = !!this.state.enabled;

		if (this.refs.switchState)
			this.refs.switchState.textContent = this.state.enabled ? _('开启') : _('关闭');
	},

	updateFanPreview: function () {
		const speed = clamp(toInt(this.state.preview_speed, 0), 0, 100);

		if (this.refs.fanWheel) {
			this.refs.fanWheel.style.setProperty('--fan-spin', spinDuration(speed) + 's');
			this.refs.fanWheel.style.setProperty('--fan-speed', speed + '%');

			if (speed <= 0)
				this.refs.fanWheel.classList.add('is-off');
			else
				this.refs.fanWheel.classList.remove('is-off');
		}

		if (this.refs.fanSpeed)
			this.refs.fanSpeed.textContent = speed + '%';
	},

	updateManualVisual: function (speed) {
		const pct = clamp(toInt(speed, 0), 0, 100);
		const progress = pct + '%';

		if (this.refs.manualSlider)
			this.refs.manualSlider.style.setProperty('--range-progress', progress);

		if (this.refs.manualDial)
			this.refs.manualDial.style.setProperty('--range-progress', progress);

		if (this.refs.manualDialValue)
			this.refs.manualDialValue.textContent = progress;

		if (this.refs.manualDialLabel)
			this.refs.manualDialLabel.textContent = pct <= 0
				? _('停止')
				: pct < 25
					? _('低速')
					: pct < 60
						? _('中速')
						: pct < 90
							? _('高速')
							: _('满速');
	},

	handleManualSliderValue: function (value, commit, input) {
		const speed = clamp(toInt(value, 35), 0, 100);

		this.state.manual_pwm = speed;
		this.state.mode = 'manual';
		this.state.dirty = true;
		this.updateModeButtons();

		if (input)
			input.value = String(speed);
		else if (this.refs.manualSlider)
			this.refs.manualSlider.value = String(speed);

		if (this.refs.manualValue)
			this.refs.manualValue.textContent = speed + '%';

		this.updateManualVisual(speed);
		this.setPreviewFromConfig();
		this.scheduleRuntimeCommit(commit ? 30 : 80);
	},

	bindManualSlider: function (input) {
		if (!input)
			return;

		const getValueFromEvent = L.bind(function (ev) {
			const rect = input.getBoundingClientRect();
			const x = clamp(ev.clientX - rect.left, 0, rect.width || 1);
			return Math.round((x / (rect.width || 1)) * 100);
		}, this);

		let dragging = false;

		input.addEventListener('pointerdown', L.bind(function (ev) {
			dragging = true;

			if (input.setPointerCapture)
				input.setPointerCapture(ev.pointerId);

			this.handleManualSliderValue(getValueFromEvent(ev), true, input);
			ev.preventDefault();
		}, this));

		input.addEventListener('pointermove', L.bind(function (ev) {
			if (!dragging)
				return;

			this.handleManualSliderValue(getValueFromEvent(ev), false, input);
			ev.preventDefault();
		}, this));

		input.addEventListener('pointerup', L.bind(function (ev) {
			if (!dragging)
				return;

			dragging = false;
			this.handleManualSliderValue(getValueFromEvent(ev), true, input);
			ev.preventDefault();
		}, this));

		input.addEventListener('pointercancel', function () {
			dragging = false;
		});
	},

	scheduleManualApply: function () {
		this.scheduleRuntimeCommit(80);
	},

	scheduleRuntimeCommit: function (delay) {
		if (this.runtimeTimer)
			window.clearTimeout(this.runtimeTimer);

		this.runtimeTimer = window.setTimeout(L.bind(function () {
			this.runtimeTimer = null;
			this.commitRuntimeConfig();
		}, this), delay || 60);
	},

	applyConfigNow: function () {
		return L.resolveDefault(callApply(this.readFormState()), null)
			.then(L.bind(function () {
				return this.pollStatus();
			}, this));
	},

	setPreviewFromConfig: function () {
		const mode = this.state.mode;
		let speed = 0;

		if (!this.state.enabled) {
			speed = 0;
		}
		else {
			switch (mode) {
				case 'silent':
					speed = toInt(this.state.silent_pwm, 15);
					break;
				case 'turbo':
					speed = toInt(this.state.turbo_pwm, 100);
					break;
				case 'manual':
					speed = toInt(this.state.manual_pwm, 35);
					break;
				case 'smart':
				default:
					speed = toInt(this.state.last_speed, toInt(this.state.silent_pwm, 15));
					break;
			}
		}

		this.state.preview_speed = speed;
		this.updateFanPreview();
	},

	pollStatus: function () {
		return L.resolveDefault(callStatus(), {}).then(L.bind(function (status) {
			status = status || {};

			this.status = status;

			const cpuUsage = toInt(status.cpu_usage, 0);
			const cpuTemp = toInt(status.cpu_temp, 0);
			const memUsage = toInt(status.mem_usage, 0);
			const boardTemp = toInt(status.board_temp, cpuTemp);
			const fanSpeed = toInt(status.fan_speed, 0);
			const enabled = toInt(status.enabled, this.state.enabled ? 1 : 0);
			const mode = status.mode || this.state.mode;

			this.state.last_speed = fanSpeed;

			if (!this.state.dirty) {
				this.state.enabled = enabled;
				this.state.mode = mode;
				this.updateSwitch();
				this.updateModeButtons();
				this.state.preview_speed = enabled ? fanSpeed : 0;
				this.updateFanPreview();
			}

			this.updateMetric('cpu', formatPercent(cpuUsage), _('温度 ') + formatTemp(cpuTemp), clamp(cpuUsage, 0, 100));
			this.updateMetric('mem', formatPercent(memUsage), _('板温 ') + formatTemp(boardTemp), clamp(memUsage, 0, 100));
			this.updateMetric('fan', formatPercent(fanSpeed), _('目标 ') + formatMode(mode), clamp(fanSpeed, 0, 100));
			this.updateThermalCard('cpu', cpuTemp);
			this.updateThermalCard('mem', boardTemp);
			this.updateThermalCard('fan', boardTemp);
			this.updateThermalTheme(Math.max(cpuTemp, boardTemp), fanSpeed);

			if (this.refs.statusLine) {
				const pwmReady = toInt(status.pwm_ready, 0);
				const chip = status.pwmchip || '--';
				this.refs.statusLine.textContent = pwmReady
					? _('风扇设备：') + chip
					: _('风扇设备：未就绪');
			}

			if (this.refs.modeValue)
				this.refs.modeValue.textContent = formatMode(this.state.mode);

			return status;
		}, this));
	},

	readFormState: function () {
		return {
			enabled: this.refs.switchInput ? (this.refs.switchInput.checked ? 1 : 0) : this.state.enabled,
			mode: this.state.mode,
			silent_pwm: this.refs.silentPwm ? toInt(this.refs.silentPwm.value, 15) : toInt(this.state.silent_pwm, 15),
			turbo_pwm: this.refs.turboPwm ? toInt(this.refs.turboPwm.value, 100) : toInt(this.state.turbo_pwm, 100),
			manual_pwm: this.refs.manualSlider ? toInt(this.refs.manualSlider.value, 35) : toInt(this.state.manual_pwm, 35),
			smart_target_temp: this.refs.smartTarget ? toInt(this.refs.smartTarget.value, 50) : toInt(this.state.smart_target_temp, 50),
			smart_min_pwm: this.refs.smartMin ? toInt(this.refs.smartMin.value, 15) : toInt(this.state.smart_min_pwm, 15),
			smart_max_temp: this.refs.smartMax ? toInt(this.refs.smartMax.value, 80) : toInt(this.state.smart_max_temp, 80),
			smart_kp: this.refs.smartKp ? toInt(this.refs.smartKp.value, 3) : toInt(this.state.smart_kp, 3),
			smooth_step: this.refs.smoothStep ? toInt(this.refs.smoothStep.value, 4) : toInt(this.state.smooth_step, 4),
			interval: this.refs.interval ? toInt(this.refs.interval.value, 2) : toInt(this.state.interval, 2)
		};
	},

	commitRuntimeConfig: function () {
		const data = this.readFormState();

		this.state.enabled = data.enabled;
		this.state.mode = data.mode;
		this.state.silent_pwm = String(clamp(data.silent_pwm, 0, 100));
		this.state.turbo_pwm = String(clamp(data.turbo_pwm, 0, 100));
		this.state.manual_pwm = String(clamp(data.manual_pwm, 0, 100));
		this.state.smart_target_temp = String(clamp(data.smart_target_temp, 30, 90));
		this.state.smart_min_pwm = String(clamp(data.smart_min_pwm, 0, 100));
		this.state.smart_max_temp = String(clamp(data.smart_max_temp, 30, 100));
		this.state.smart_kp = String(clamp(data.smart_kp, 1, 10));
		this.state.smooth_step = String(clamp(data.smooth_step, 1, 20));
		this.state.interval = String(clamp(data.interval, 1, 30));

		this.state.dirty = true;

		return this.applyConfigNow()
			.then(L.bind(function () {
				this.state.dirty = false;
			}, this))
			.catch(L.bind(function (err) {
				this.state.dirty = false;
				ui.addNotification(null, E('p', _('保存失败：') + (err.message || err)), 'error');
				throw err;
			}, this));
	},

	commitConfig: function () {
		const data = this.readFormState();

		this.state.enabled = data.enabled;
		this.state.mode = data.mode;
		this.state.silent_pwm = String(clamp(data.silent_pwm, 0, 100));
		this.state.turbo_pwm = String(clamp(data.turbo_pwm, 0, 100));
		this.state.manual_pwm = String(clamp(data.manual_pwm, 0, 100));
		this.state.smart_target_temp = String(clamp(data.smart_target_temp, 30, 90));
		this.state.smart_min_pwm = String(clamp(data.smart_min_pwm, 0, 100));
		this.state.smart_max_temp = String(clamp(data.smart_max_temp, 30, 100));
		this.state.smart_kp = String(clamp(data.smart_kp, 1, 10));
		this.state.smooth_step = String(clamp(data.smooth_step, 1, 20));
		this.state.interval = String(clamp(data.interval, 1, 30));
		this.setPreviewFromConfig();

		return this.applyConfigNow()
			.then(L.bind(function () {
				ui.addNotification(null, E('p', _('风扇高级参数已保存并立即生效。')), 'info');
			}, this))
			.catch(L.bind(function (err) {
				ui.addNotification(null, E('p', _('保存失败：') + (err.message || err)), 'error');
				throw err;
			}, this));
	},

	renderControlButton: function (key, label, description, mode) {
		const btn = E('button', {
			'type': 'button',
			'class': 'lf-mode-btn',
			'data-mode': mode,
			'click': ui.createHandlerFn(this, function () {
				this.state.mode = mode;
				this.state.dirty = true;
				this.updateModeButtons();
				this.setPreviewFromConfig();
				this.scheduleRuntimeCommit(30);
			})
		}, [
			E('span', { 'class': 'lf-mode-label' }, label),
			E('small', { 'class': 'lf-mode-desc' }, description)
		]);

		this.refs.modeButtons[mode] = btn;
		return btn;
	},

	render: function (res) {
		const board = res[1] || {};
		const status = res[2] || {};

		this.state = {
			enabled: toInt(getConfigValue('enabled'), 1),
			mode: getConfigValue('mode'),
			silent_pwm: getConfigValue('silent_pwm'),
			turbo_pwm: getConfigValue('turbo_pwm'),
			manual_pwm: getConfigValue('manual_pwm'),
			smart_target_temp: getConfigValue('smart_target_temp'),
			smart_min_pwm: getConfigValue('smart_min_pwm'),
			smart_max_temp: getConfigValue('smart_max_temp'),
			smart_kp: getConfigValue('smart_kp'),
			smooth_step: getConfigValue('smooth_step'),
			interval: getConfigValue('interval'),
			last_speed: toInt(status.fan_speed, toInt(getConfigValue('manual_pwm'), 35)),
			preview_speed: toInt(status.fan_speed, toInt(getConfigValue('manual_pwm'), 35))
		};

		this.history = {
			cpu: [],
			mem: [],
			fan: []
		};

		this.refs = {
			metrics: {},
			modeButtons: {}
		};

		const model = board.model || _('LiberNet AX6000');
		const hostname = board.hostname || _('OpenWrt');

		const hero = E('div', { 'class': 'lf-hero lf-card' }, [
			E('div', { 'class': 'lf-hero-copy' }, [
				E('div', { 'class': 'lf-breadcrumb' }, _('风扇控制面板')),
				E('h2', { 'class': 'lf-title' }, _('LiberNet 风扇控制中心')),
				E('p', { 'class': 'lf-description' }, _('默认智能模式会围绕 50°C 平滑调速，静音、极速和自定义模式可直接保存生效。')),
				E('div', { 'class': 'lf-hero-meta' }, [
					E('span', { 'class': 'lf-pill' }, _('设备：') + model),
					E('span', { 'class': 'lf-pill' }, _('主机：') + hostname),
					E('span', { 'class': 'lf-pill', 'id': 'lf-status-line' }, _('风扇设备：检测中')),
					E('span', { 'class': 'lf-pill lf-thermal-pill', 'id': 'lf-thermal-line' }, _('整机：温度正常'))
				])
			]),
			E('div', { 'class': 'lf-hero-visual' }, [
				E('div', { 'class': 'lf-fan-stage' }, [
					E('div', { 'class': 'lf-fan-ring' }),
					E('div', { 'class': 'lf-fan-wheel is-off', 'id': 'lf-fan-wheel' }),
					E('div', { 'class': 'lf-fan-center' }),
					E('div', { 'class': 'lf-fan-speed', 'id': 'lf-fan-speed' }, '0%')
				]),
				E('div', { 'class': 'lf-hero-caption' }, _('当前风扇转速'))
			])
		]);

		this.refs.hero = hero;
		this.refs.statusLine = hero.querySelector('#lf-status-line');
		this.refs.thermalLine = hero.querySelector('#lf-thermal-line');
		this.refs.fanWheel = hero.querySelector('#lf-fan-wheel');
		this.refs.fanSpeed = hero.querySelector('#lf-fan-speed');

		const metrics = E('div', { 'class': 'lf-grid' }, [
			this.renderMetricCard('cpu', _('CPU 占用率'), 'blue', 100),
			this.renderMetricCard('mem', _('内存占用率'), 'teal', 100),
			this.renderMetricCard('fan', _('当前风扇转速'), 'green', 100)
		]);

		const modeButtons = E('div', { 'class': 'lf-mode-grid' }, [
			this.renderControlButton('silent', _('静音模式'), _('默认 15% 低噪运行'), 'silent'),
			this.renderControlButton('turbo', _('极速模式'), _('100% 满速散热'), 'turbo'),
			this.renderControlButton('smart', _('智能模式'), _('默认模式，50°C 到 80°C 平滑升速'), 'smart'),
			this.renderControlButton('manual', _('自定义'), _('手动滑条指定转速'), 'manual')
		]);

		const switchRow = E('label', { 'class': 'lf-switch' }, [
			E('input', {
				'type': 'checkbox',
				'id': 'lf-enabled-switch',
				'checked': !!this.state.enabled,
				'change': ui.createHandlerFn(this, function (ev) {
					this.state.enabled = ev.target.checked ? 1 : 0;
					this.state.dirty = true;
					this.updateSwitch();
					this.setPreviewFromConfig();
					this.scheduleRuntimeCommit(30);
				})
			}),
			E('span', { 'class': 'lf-switch-track' }),
			E('span', { 'class': 'lf-switch-copy' }, [
				E('strong', {}, _('风扇开关')),
				E('small', { 'id': 'lf-switch-state' }, this.state.enabled ? _('开启') : _('关闭'))
			])
		]);

		this.refs.switchInput = switchRow.querySelector('#lf-enabled-switch');
		this.refs.switchState = switchRow.querySelector('#lf-switch-state');

		const manualSlider = E('div', { 'class': 'lf-slider-card lf-card' }, [
			E('div', { 'class': 'lf-slider-layout' }, [
				E('div', { 'class': 'lf-slider-main' }, [
					E('div', { 'class': 'lf-card-head' }, [
						E('div', { 'class': 'lf-card-title' }, _('自定义转速')),
						E('div', { 'class': 'lf-card-value' }, [
							E('span', { 'id': 'lf-manual-value' }, toInt(this.state.manual_pwm, 35) + '%')
						])
					]),
					E('input', {
						'type': 'range',
						'min': '0',
						'max': '100',
						'step': '1',
						'value': toInt(this.state.manual_pwm, 35),
						'class': 'lf-range',
						'id': 'lf-manual-slider',
						'change': ui.createHandlerFn(this, function (ev) {
							this.handleManualSliderValue(ev.target.value, true, ev.target);
						}),
						'input': ui.createHandlerFn(this, function (ev) {
							this.handleManualSliderValue(ev.target.value, false, ev.target);
						})
					}),
					E('div', { 'class': 'lf-range-scale' }, [
						E('span', {}, '0'),
						E('span', {}, '25'),
						E('span', {}, '50'),
						E('span', {}, '75'),
						E('span', {}, '100')
					]),
					E('div', { 'class': 'lf-slider-hint', 'id': 'lf-slider-hint' }, _('当前为模式控制，滑条仅在自定义模式下生效'))
				]),
				E('div', { 'class': 'lf-slider-dial', 'id': 'lf-manual-dial' }, [
					E('div', { 'class': 'lf-slider-dial-ring' }),
					E('div', { 'class': 'lf-slider-dial-core' }, [
						E('div', { 'class': 'lf-slider-dial-value', 'id': 'lf-manual-dial-value' }, toInt(this.state.manual_pwm, 35) + '%'),
						E('div', { 'class': 'lf-slider-dial-label', 'id': 'lf-manual-dial-label' }, _('中速'))
					])
				])
			])
		]);

		this.refs.manualSlider = manualSlider.querySelector('#lf-manual-slider');
		this.refs.manualValue = manualSlider.querySelector('#lf-manual-value');
		this.refs.sliderHint = manualSlider.querySelector('#lf-slider-hint');
		this.refs.manualDial = manualSlider.querySelector('#lf-manual-dial');
		this.refs.manualDialValue = manualSlider.querySelector('#lf-manual-dial-value');
		this.refs.manualDialLabel = manualSlider.querySelector('#lf-manual-dial-label');
		this.refs.slider = this.refs.manualSlider;
		this.bindManualSlider(this.refs.manualSlider);
		this.updateManualVisual(toInt(this.state.manual_pwm, 35));

		const advanced = E('details', { 'class': 'lf-advanced' }, [
			E('summary', {}, _('高级调速参数')),
			E('div', { 'class': 'lf-advanced-grid' }, [
				E('label', {}, [
					E('span', {}, _('静音模式转速(%)')),
					E('input', { 'type': 'number', 'min': '0', 'max': '100', 'step': '1', 'value': toInt(this.state.silent_pwm, 15), 'id': 'lf-silent-pwm' })
				]),
				E('label', {}, [
					E('span', {}, _('极速模式转速(%)')),
					E('input', { 'type': 'number', 'min': '0', 'max': '100', 'step': '1', 'value': toInt(this.state.turbo_pwm, 100), 'id': 'lf-turbo-pwm' })
				]),
				E('label', {}, [
					E('span', {}, _('智能目标温度(°C)')),
					E('input', { 'type': 'number', 'min': '30', 'max': '90', 'step': '1', 'value': toInt(this.state.smart_target_temp, 50), 'id': 'lf-smart-target' })
				]),
				E('label', {}, [
					E('span', {}, _('智能最小转速(%)')),
					E('input', { 'type': 'number', 'min': '0', 'max': '100', 'step': '1', 'value': toInt(this.state.smart_min_pwm, 15), 'id': 'lf-smart-min' })
				]),
				E('label', {}, [
					E('span', {}, _('智能满速温度(°C)')),
					E('input', { 'type': 'number', 'min': '30', 'max': '100', 'step': '1', 'value': toInt(this.state.smart_max_temp, 80), 'id': 'lf-smart-max' })
				]),
				E('label', {}, [
					E('span', {}, _('平滑系数')),
					E('input', { 'type': 'number', 'min': '1', 'max': '10', 'step': '1', 'value': toInt(this.state.smart_kp, 3), 'id': 'lf-smart-kp' })
				]),
				E('label', {}, [
					E('span', {}, _('平滑步进')),
					E('input', { 'type': 'number', 'min': '1', 'max': '20', 'step': '1', 'value': toInt(this.state.smooth_step, 4), 'id': 'lf-smooth-step' })
				]),
				E('label', {}, [
					E('span', {}, _('刷新间隔(秒)')),
					E('input', { 'type': 'number', 'min': '1', 'max': '30', 'step': '1', 'value': toInt(this.state.interval, 2), 'id': 'lf-interval' })
				])
			])
		]);

		this.refs.silentPwm = advanced.querySelector('#lf-silent-pwm');
		this.refs.turboPwm = advanced.querySelector('#lf-turbo-pwm');
		this.refs.smartTarget = advanced.querySelector('#lf-smart-target');
		this.refs.smartMin = advanced.querySelector('#lf-smart-min');
		this.refs.smartMax = advanced.querySelector('#lf-smart-max');
		this.refs.smartKp = advanced.querySelector('#lf-smart-kp');
		this.refs.smoothStep = advanced.querySelector('#lf-smooth-step');
		this.refs.interval = advanced.querySelector('#lf-interval');

		const actions = E('div', { 'class': 'lf-actions' }, [
			E('button', {
				'class': 'lf-btn lf-btn-primary',
				'click': ui.createHandlerFn(this, function () {
					return this.commitConfig();
				})
			}, _('保存高级参数并立即生效')),
			E('button', {
				'class': 'lf-btn lf-btn-ghost',
				'click': ui.createHandlerFn(this, function () {
					window.location.reload();
				})
			}, _('重新加载'))
		]);

		const content = E('div', { 'class': 'lf-shell' }, [
			hero,
			metrics,
			E('div', { 'class': 'lf-ctrl-grid' }, [
				E('div', { 'class': 'lf-card' }, [
					E('div', { 'class': 'lf-card-head' }, [
					E('div', { 'class': 'lf-card-title' }, _('模式选择')),
					E('div', { 'class': 'lf-card-value', 'id': 'lf-mode-value' }, formatMode(this.state.mode))
				]),
					E('div', { 'class': 'lf-card-sub' }, _('模式、开关和滑条点击后立即生效，高级参数请在下方单独保存。')),
					modeButtons,
					switchRow
				]),
				manualSlider,
				advanced
			]),
			actions
		]);

		this.refs.shell = content;
		this.refs.modeValue = content.querySelector('#lf-mode-value');
		this.updateModeButtons();
		this.updateSwitch();
		this.setPreviewFromConfig();
		this.updateMetric('cpu', formatPercent(status.cpu_usage || 0), _('温度 ') + formatTemp(status.cpu_temp || 0), toInt(status.cpu_usage, 0));
		this.updateMetric('mem', formatPercent(status.mem_usage || 0), _('板温 ') + formatTemp(status.board_temp || status.cpu_temp || 0), toInt(status.mem_usage, 0));
		this.updateMetric('fan', formatPercent(status.fan_speed || 0), _('目标 ') + formatMode(this.state.mode), toInt(status.fan_speed, 0));
		this.updateThermalCard('cpu', status.cpu_temp || 0);
		this.updateThermalCard('mem', status.board_temp || status.cpu_temp || 0);
		this.updateThermalCard('fan', status.board_temp || status.cpu_temp || 0);
		this.updateThermalTheme(Math.max(toInt(status.cpu_temp, 0), toInt(status.board_temp, 0)), toInt(status.fan_speed, 0));

		poll.add(L.bind(this.pollStatus, this), toInt(this.state.interval, 2));

		return content;
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
