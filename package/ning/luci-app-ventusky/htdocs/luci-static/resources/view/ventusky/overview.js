'use strict';
'require view';
'require uci';
'require ui';

const CFG = 'ventusky', SECTION = 'main', DEFAULT_URL = 'https://www.ventusky.com/zh';
const esc = x => String(x == null ? '' : x).replace(/[&<>"']/g, c => ({ '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;' })[c]);
function validURL(value) {
	try { const u = new URL(value); return u.protocol === 'https:' && (u.hostname === 'ventusky.com' || u.hostname.endsWith('.ventusky.com')); }
	catch (e) { return false; }
}

return view.extend({
	load: function() { return uci.load(CFG); },
	render: function() {
		const title = uci.get(CFG, SECTION, 'title') || _('Ventusky 天气地图');
		const configured = uci.get(CFG, SECTION, 'url') || DEFAULT_URL;
		const enabled = uci.get(CFG, SECTION, 'enabled') !== '0';
		const url = validURL(configured) ? configured : DEFAULT_URL;
		const page = E('div', { class: 'ventusky-page' });
		page.innerHTML = '<style>.ventusky-page{display:grid;gap:16px}.ventusky-card{padding:18px;border:1px solid var(--border-color-medium,#ddd);border-radius:12px;background:var(--background-color-medium,#fff)}.ventusky-head{display:flex;gap:15px;align-items:flex-start;justify-content:space-between}.ventusky-head h2{margin:4px 0}.ventusky-head p,.ventusky-note{color:var(--text-color-low,#777)}.ventusky-frame{width:100%;height:min(76vh,900px);border:0;border-radius:10px;background:#101820}.ventusky-settings{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin-top:14px}.ventusky-settings label{display:grid;gap:6px;font-weight:600}.ventusky-settings input{width:100%;box-sizing:border-box;padding:8px}.ventusky-actions{display:flex;gap:8px;align-items:end;flex-wrap:wrap}.ventusky-actions button,.ventusky-actions a{padding:8px 12px;border-radius:7px;border:1px solid var(--primary-color,#1677ff);background:var(--primary-color,#1677ff);color:#fff;text-decoration:none;cursor:pointer}.ventusky-actions .secondary{background:transparent;color:var(--text-color-high,#222);border-color:var(--border-color-medium,#aaa)}@media(max-width:800px){.ventusky-settings{grid-template-columns:1fr}.ventusky-frame{height:66vh}}</style>' +
			'<section class="ventusky-card"><div class="ventusky-head"><div><small>' + _('交互式天气服务') + '</small><h2 data-title>' + esc(title) + '</h2><p>' + _('风、降水、雷达、卫星与空气污染图层由 Ventusky 网站实时提供。') + '</p></div><span>' + (enabled ? _('已启用') : _('已停用')) + '</span></div><iframe class="ventusky-frame" data-frame referrerpolicy="strict-origin-when-cross-origin" title="Ventusky" src="' + (enabled ? esc(url) : 'about:blank') + '"></iframe><p class="ventusky-note">' + _('此组件只嵌入官方网页，不采集位置、不缓存地图数据。若网站阻止嵌入，可用“新窗口打开”。') + '</p></section>' +
			'<section class="ventusky-card"><h3>' + _('组件设置') + '</h3><div class="ventusky-settings"><label>' + _('显示名称') + '<input data-title-input value="' + esc(title) + '"></label><label>' + _('官方地址') + '<input data-url value="' + esc(configured) + '"></label><label><input type="checkbox" data-enabled ' + (enabled ? 'checked' : '') + '> ' + _('在 LuCI 中启用天气地图') + '</label></div><div class="ventusky-actions"><button data-save>' + _('保存并应用') + '</button><button class="secondary" data-reload>' + _('重新载入地图') + '</button><a class="secondary" data-external target="_blank" rel="noopener noreferrer">' + _('新窗口打开') + '</a></div></section>';
		const setExternal = () => { page.querySelector('[data-external]').href = validURL(page.querySelector('[data-url]').value) ? page.querySelector('[data-url]').value : DEFAULT_URL; };
		page.querySelector('[data-url]').addEventListener('input', setExternal); setExternal();
		page.querySelector('[data-reload]').addEventListener('click', () => { const candidate=page.querySelector('[data-url]').value; if (!validURL(candidate)) { ui.addNotification(null,E('p',{},_('仅允许 Ventusky 的 HTTPS 官方地址。')));return; } page.querySelector('[data-frame]').src=candidate; });
		page.querySelector('[data-save]').addEventListener('click', () => { const candidate=page.querySelector('[data-url]').value.trim(); if (!validURL(candidate)) { ui.addNotification(null,E('p',{},_('仅允许 Ventusky 的 HTTPS 官方地址。')));return; } uci.set(CFG,SECTION,'title',page.querySelector('[data-title-input]').value.trim() || _('Ventusky 天气地图'));uci.set(CFG,SECTION,'url',candidate);uci.set(CFG,SECTION,'enabled',page.querySelector('[data-enabled]').checked?'1':'0');uci.save().then(()=>uci.apply()).then(()=>{page.querySelector('[data-frame]').src=page.querySelector('[data-enabled]').checked?candidate:'about:blank';page.querySelector('[data-title]').textContent=page.querySelector('[data-title-input]').value.trim()||_('Ventusky 天气地图');ui.addNotification(null,E('p',{},_('天气地图设置已保存。')));}).catch(e=>ui.addNotification(null,E('p',{},String(e)))); });
		return page;
	}
});
