/* ===========================================================================
   app.js —— Sonar 配置工具前端
   界面与 C++ 完全解耦：所有操作走 Backend.call(method, params)。
   在浏览器里打开也能跑（mock.js 提供假数据），方便调界面。
   =========================================================================== */
(function () {
'use strict';

const $  = (s, r) => (r || document).querySelector(s);
const $$ = (s, r) => [...(r || document).querySelectorAll(s)];

/* 中文按拼音排序。Chromium 的 ICU 原生支持 zh 拼音排序规则，
   所以不用像原 C++ 那样自己拿 LCMapStringW 生成排序键。 */
let COLL = null;
try { COLL = new Intl.Collator('zh-Hans-CN-u-co-pinyin', { numeric: true, sensitivity: 'base' }); }
catch (e) { try { COLL = new Intl.Collator('zh', { numeric: true }); } catch (e2) { COLL = null; } }
function cmpHan(a, b) {
  if (!a && !b) return 0;
  if (!a) return -1;
  if (!b) return 1;
  return COLL ? COLL.compare(a, b) : a.localeCompare(b);
}

/* 小工具：建 DOM */
function h(tag, props, ...kids) {
  const el = document.createElement(tag);
  if (props) for (const k in props) {
    if (k === 'class') el.className = props[k];
    else if (k === 'html') el.innerHTML = props[k];
    else if (k === 'text') el.textContent = props[k];
    else if (k.startsWith('on') && typeof props[k] === 'function') el.addEventListener(k.slice(2), props[k]);
    else if (props[k] === true) el.setAttribute(k, '');
    else if (props[k] != null && props[k] !== false) el.setAttribute(k, props[k]);
  }
  for (const kid of kids.flat()) {
    if (kid == null || kid === false) continue;
    el.appendChild(typeof kid === 'string' || typeof kid === 'number'
      ? document.createTextNode(String(kid)) : kid);
  }
  return el;
}

function toast(msg, kind) {
  const t = h('div', { class: 'toast' + (kind ? ' ' + kind : ''), text: msg });
  $('#toasts').appendChild(t);
  setTimeout(() => { t.style.opacity = '0'; t.style.transition = 'opacity .3s'; }, 2400);
  setTimeout(() => t.remove(), 2800);
}

/* 极简 Markdown → HTML，只覆盖发布说明里实际用到的那几种语法。
   ★ 说明文本是从 GitHub 拉回来的**外部内容**，一律先转义再套标签，
     绝不能直接当 HTML 塞进 DOM。
   ★ URL 渲染成 <code> 而不是 <a>：这是 WebView，点链接会把整个界面导航走，
     要开网页请用窗口里的「打开发布页」按钮（那个会调系统浏览器）。 */
function mdToHtml(src) {
  const esc = s => String(s).replace(/[&<>"]/g,
    c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const inline = s => s
    .replace(/\*\*([^*]+)\*\*/g, '<b>$1</b>')
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    .replace(/&lt;(https?:\/\/[^&\s]+)&gt;/g, '<code>$1</code>');
  const out = [];
  let inList = false;
  const closeList = () => { if (inList) { out.push('</ul>'); inList = false; } };
  for (const raw of String(src || '').replace(/\r\n?/g, '\n').split('\n')) {
    const t = esc(raw.replace(/\s+$/, ''));
    if (!t.trim()) { closeList(); continue; }
    let m;
    if ((m = t.match(/^###\s+(.*)$/))) { closeList(); out.push('<h4>' + inline(m[1]) + '</h4>'); continue; }
    if ((m = t.match(/^##\s+(.*)$/)))  { closeList(); out.push('<h3>' + inline(m[1]) + '</h3>'); continue; }
    if ((m = t.match(/^#\s+(.*)$/)))   { closeList(); out.push('<h2>' + inline(m[1]) + '</h2>'); continue; }
    if ((m = t.match(/^\s*[-*]\s+(.*)$/))) {
      if (!inList) { out.push('<ul>'); inList = true; }
      out.push('<li>' + inline(m[1]) + '</li>');
      continue;
    }
    closeList();
    out.push('<p>' + inline(t) + '</p>');
  }
  closeList();
  return out.join('');
}

/* 通用确认框：删除这种破坏性操作一律走它 */
function confirmBox({ title, message, okText, danger, input, placeholder }) {
  return new Promise(resolve => {
    const m = $('#mConfirm');
    $('#cfTitle').textContent = title || '确认';
    $('#cfMsg').innerHTML = message || '';
    const inp = $('#cfInput');
    inp.hidden = !input;
    inp.value = '';
    if (input) inp.placeholder = placeholder || '';
    const ok = $('#cfOk'), cancel = $('#cfCancel');
    ok.textContent = okText || '确定';
    ok.classList.toggle('primary', !danger);
    ok.style.borderColor = danger ? 'var(--c-red)' : '';
    ok.style.background = danger ? 'var(--c-red)' : '';
    ok.style.color = danger ? '#fff' : '';
    function done(v) {
      m.classList.remove('open');
      ok.removeEventListener('click', onOk);
      cancel.removeEventListener('click', onCancel);
      inp.removeEventListener('keydown', onKey);
      resolve(v);
    }
    function onOk() { done(input ? (inp.value.trim() || false) : true); }
    function onCancel() { done(false); }
    function onKey(e) { if (e.key === 'Enter') onOk(); }
    ok.addEventListener('click', onOk);
    cancel.addEventListener('click', onCancel);
    inp.addEventListener('keydown', onKey);
    m.classList.add('open');
    setTimeout(() => (input ? inp : ok).focus(), 30);
  });
}

/* ===========================================================================
   应用状态
   =========================================================================== */
const ST = {
  ready: false,
  theme: 'clean-light',
  themes: [],
  assetBase: '../',
  iconBase: '../weapons_icons/',
  logoUrl: '../sonar_icon.png',
  weapons: [],
  anyCount: 0,
  entryCount: 0,
  judges: [],
  global: {},
  hotkeys: {},
  active: {},
  weaponFilter: -1,
  onlyActive: false,
  search: '',
  sort: { key: 'name', asc: true },
  entries: [],
  visible: [],          // 当前筛选+排序后的 [{entry, index}]
  selIndex: -1,
  combos: [],           // 当前武器可选组合
  live: { attached: false, history: [] },
  histExpanded: false,
  upd: { state: 0, progress: 0, tag: '', notes: '', err: '', needRestart: false },
};

const wName = id => {
  if (id < 0) return '任意';
  const w = ST.weapons.find(x => x.id === id);
  return w ? w.name : ('武器' + id);
};

/* 武器图标文件名 —— 与 weapons_icons/ 下的文件一一对应。
   ★ 后端只给武器 id：它返回的 icon 字段是 "w0"/"wany" 这种**编号**，不是文件名。
     文件名统一由前端定，将来换图/改扩展名不用动 C++。
   【踩过的坑】之前直接拿后端的 "w0" 去拼 URL，拼出 ../weapons_icons/w0，
     全是 404，整个武器列看起来就是一排"碎图"占位符。 */
const ICON_FILES = {
  0:  'great-sword1.png',
  1:  'sword-and-shield1.png',
  2:  'dual-blades1.png',
  3:  'long-sword1.png',
  4:  'hammer1.png',
  5:  'hunting-horn1.png',
  6:  'lance1.png',
  7:  'gunlance1.png',
  8:  'switch-axe1.png',
  9:  'charge-blade1.png',
  10: 'insect-glaive1.png',
  11: 'bow1.png',
  12: 'light-bowgun1.png',
  13: 'heavy-bowgun1.png',
};
const wIconUrl = id => (ICON_FILES[id] ? ST.iconBase + ICON_FILES[id] : null);

/* 图标加载失败时**不要留碎图占位符**：就地换成文字/符号，并在控制台留一行原因。
   这样即使哪天资源没部署全，界面也是"能看"的，而不是一排碎图。 */
function iconImg(url, fallbackText) {
  const img = h('img', { src: url, alt: '' });
  img.onerror = () => {
    console.warn('[Sonar] 武器图标加载失败:', url);
    const span = document.createElement('span');
    span.className = 'icon-fallback';
    span.textContent = fallbackText || '◈';
    if (img.parentNode) img.parentNode.replaceChild(span, img);
  };
  return img;
}

const wchip = (id, cls) => {
  const u = wIconUrl(id);
  const c = 'wchip' + (cls ? ' ' + cls : '');
  if (!u) return h('div', { class: c + ' all' }, '◈');
  return h('div', { class: c }, iconImg(u, '◈'));
};
const wInline = id => {
  const u = wIconUrl(id);
  if (!u) return h('span', { class: 'tw', style: 'color:var(--muted)' }, '任意');
  const box = h('span', { class: 'tw' });
  box.appendChild(iconImg(u, '◈'));
  box.appendChild(document.createTextNode(wName(id)));
  return box;
};

/* ===========================================================================
   主题
   =========================================================================== */
function applyTheme(id) {
  ST.theme = id;
  document.documentElement.setAttribute('data-theme', id);
  $$('#themePicker .theme-sw').forEach(b => b.classList.toggle('on', b.dataset.t === id));
}
function renderThemePicker() {
  const box = $('#themePicker');
  box.innerHTML = '';
  (ST.themes.length ? ST.themes : [
    { id: 'clean-light', name: '极简浅色' }, { id: 'pro-dark', name: '深色专业' },
    { id: 'cute-pastel', name: '可爱风' }, { id: 'sticker-outline', name: '描边贴纸' },
  ]).forEach(t => {
    const b = h('button', {
      class: 'theme-sw' + (t.id === ST.theme ? ' on' : ''), 'data-t': t.id, title: t.name + ' — ' + (t.desc || ''),
      onclick: async () => {
        applyTheme(t.id);
        try { await Backend.call('theme.set', { theme: t.id }); toast('界面风格：' + t.name, 'ok'); }
        catch (e) { toast('保存主题失败：' + e.message, 'err'); }
      },
    }, h('i'), h('i'));
    box.appendChild(b);
  });
}

/* ===========================================================================
   左栏：武器 + 组合
   =========================================================================== */
function renderWeapons() {
  const box = $('#weaponList');
  box.innerHTML = '';
  const rows = [
    { id: -1, name: '全部条目', count: ST.entryCount },
    ...ST.weapons.map(w => ({ id: w.id, name: w.name, count: w.count, icon: w.icon })),
  ];
  if (ST.anyCount > 0) rows.push({ id: -2, name: '通用(任意)', count: ST.anyCount });

  rows.forEach(r => {
    const el = h('div', {
      class: 'wrow' + (ST.weaponFilter === r.id ? ' on' : ''),
      onclick: () => selectWeapon(r.id),
    }, wchip(r.id === -1 ? -99 : r.id), h('span', { class: 'wname', text: r.name }),
       h('span', { class: 'wcount', text: String(r.count) }));
    box.appendChild(el);
  });
}

async function selectWeapon(id) {
  ST.weaponFilter = id;
  ST.selIndex = -1;
  await loadCombos();
  renderWeapons();
  renderComboPanel();
  applyFilter();
  Backend.call('ui.set', { weaponFilter: id }).catch(() => {});
}

async function loadCombos() {
  const w = ST.weaponFilter;
  if (w < 0) { ST.combos = []; return; }
  try {
    const r = await Backend.call('combo.list', { weapon: w });
    ST.combos = r.combos && r.combos.length ? r.combos : [''];
    ST.active[w] = r.active || '';
  } catch { ST.combos = ['']; }
}

function renderComboPanel() {
  const panel = $('#comboPanel');
  const w = ST.weaponFilter;
  if (w < 0) { panel.hidden = true; return; }
  panel.hidden = false;
  const sel = $('#comboSelect');
  sel.innerHTML = '';
  ST.combos.forEach(c => sel.appendChild(
    h('option', { value: c, selected: (ST.active[w] || '') === c }, c === '' ? '默认' : c)));
  sel.onchange = async () => {
    await Backend.call('combo.setActive', { weapon: w, name: sel.value });
    ST.active[w] = sel.value;
    await refresh();
    toast('已切换到组合：' + (sel.value || '默认'));
  };
}

/* ===========================================================================
   中栏：条目表（筛选 + 排序）
   =========================================================================== */
function entryActive(e) {
  if (e.weaponType < 0) return true;
  return (e.combo || '') === (ST.active[e.weaponType] || '');
}

function displayName(e) {
  if (e.name) return e.name;
  return e.fsmId >= 0 ? ('FSM ' + e.fsmId) : '条目';
}
function soundSummary(e) {
  const list = e.def || [];
  const first = list[0];
  return { text: first ? first.path : '(无音效)', fixed: !!(first && first.fixed), count: list.length };
}
function firstLmt(e) { return e.lmtAny || !e.lmt || !e.lmt.length ? -1 : e.lmt[0]; }

function applyFilter() {
  const q = ST.search.trim().toLowerCase();
  let list = ST.entries.map((e, i) => ({ e, i }));

  // 武器筛选。
  // 【默认列出全部条目，不按激活组合隐藏】—— 原版只列激活组合的条目，
  // 结果条目一旦落到别的组合里，用户就以为"条目消失了"。现在默认全列，
  // 非激活的打个「未激活」标记；想要旧行为可以勾「只看激活组合」。
  if (ST.weaponFilter === -2) list = list.filter(x => x.e.weaponType < 0);
  else if (ST.weaponFilter >= 0) list = list.filter(x => x.e.weaponType === ST.weaponFilter);
  if (ST.onlyActive) list = list.filter(x => entryActive(x.e));

  // 搜索
  if (q) list = list.filter(x => {
    const e = x.e;
    const hay = [displayName(e), String(e.fsmId), (e.lmt || []).join(','),
                 (e.def || []).map(s => s.path).join(' ')].join(' ').toLowerCase();
    return hay.includes(q);
  });

  // 排序（同键保持加入顺序 = stable）
  const { key, asc } = ST.sort;
  list.sort((A, B) => {
    const a = A.e, b = B.e;
    let c = 0;
    if (key === 'name') c = cmpHan(displayName(a), displayName(b));
    else if (key === 'weapon') c = a.weaponType - b.weaponType;
    else if (key === 'lmt') c = firstLmt(a) - firstLmt(b);
    else if (key === 'fsm') c = a.fsmId - b.fsmId;
    if (c === 0) c = A.i - B.i;
    return asc ? c : -c;
  });
  ST.visible = list;
  renderTable();
}

function renderTable() {
  const tb = $('#entryBody');
  tb.innerHTML = '';
  const empty = $('#entryEmpty');

  if (!ST.visible.length) {
    empty.hidden = false;
    empty.textContent = ST.entries.length
      ? '没有匹配的条目（换个筛选或清空搜索）'
      : '还没有任何条目 — 点右上角「＋ 新增条目」开始';
  } else empty.hidden = true;

  ST.visible.forEach(({ e, i }) => {
    const sm = soundSummary(e);
    const inCombo = entryActive(e);
    const tr = h('tr', {
      class: (i === ST.selIndex ? 'sel ' : '') + (inCombo ? '' : 'inactive'),
      title: inCombo ? '' : '这条属于别的组合，当前不生效（切换组合后才会生效）',
    },
      h('td', {}, h('span', { class: 'nm', title: displayName(e), text: displayName(e) }),
        inCombo ? null : h('span', { class: 'badge warn', text: '未激活', style: 'margin-left:7px' })),
      h('td', {}, wInline(e.weaponType)),
      h('td', { class: 'lmt' }, e.lmtAny || !(e.lmt || []).length ? '不限' : e.lmt.join(',')),
      h('td', { class: 'fsm' }, String(e.fsmId)),
      h('td', { title: (e.def || []).map(s => s.path).join('\n') },
        h('span', { class: 'snd' }, sm.text,
        e.conds && e.conds.length ? h('span', { class: 'badge', text: '判定' }) : null,
        sm.fixed ? h('span', { class: 'badge', text: 'F' }) : null,
        sm.count > 1 ? h('span', { class: 'badge', text: '+' + (sm.count - 1) }) : null)),
      h('td', {}, h('div', { class: 'acts' },
        h('button', { onclick: ev => { ev.stopPropagation(); openEditor(i); } }, '编辑'),
        h('button', { onclick: async ev => {
          ev.stopPropagation();
          await Backend.call('entries.duplicate', { index: i });
          await refresh(); toast('已复制条目');
        } }, '复制'),
        h('button', { class: 'del', onclick: ev => { ev.stopPropagation(); askDelete([i]); } }, '删除'))),
    );
    tr.addEventListener('click', () => {
      ST.selIndex = i; renderTable();
      openEditor(i);
    });
    tb.appendChild(tr);
  });

  // 表头排序指示
  $$('#entryTable th.sortable').forEach(th => {
    th.classList.remove('asc', 'desc');
    if (th.dataset.sort === ST.sort.key) th.classList.add(ST.sort.asc ? 'asc' : 'desc');
  });

  const inactive = ST.visible.filter(x => !entryActive(x.e)).length;
  $('#entryFoot').textContent =
    `显示 ${ST.visible.length} / 共 ${ST.entries.length} 条` +
    (ST.weaponFilter >= 0 ? `　·　当前组合：${ST.active[ST.weaponFilter] || '默认'}` : '') +
    (inactive && !ST.onlyActive ? `　·　其中 ${inactive} 条属于别的组合（未激活）` : '') +
    `　·　排序：${{ name: '名称', weapon: '武器', lmt: 'LMT', fsm: 'FSMId' }[ST.sort.key]}` +
    (ST.sort.asc ? ' 升序' : ' 降序');
}

/* 删除二次确认 —— 明确列出要删什么，不可撤销 */
async function askDelete(indices) {
  const names = indices.map(i => displayName(ST.entries[i]));
  const one = names.length === 1;
  const list = names.slice(0, 8).map(n => '· ' + n).join('\n');
  const more = names.length > 8 ? `\n… 另外 ${names.length - 8} 条` : '';
  const ok = await confirmBox({
    title: one ? '删除条目' : `删除 ${names.length} 个条目`,
    message: `确定要删除${one ? '这个条目' : '这些条目'}吗？\n\n${list}${more}\n\n` +
             `<b>删除后不可撤销</b>（保存前仍可关掉 GUI 放弃改动）。`,
    okText: '删除', danger: true,
  });
  if (!ok) return;
  await Backend.call('entries.remove', { indices });
  ST.selIndex = -1;
  await refresh();
  toast(`已删除 ${names.length} 个条目`, 'ok');
}

/* ===========================================================================
   右栏：实时捕获
   =========================================================================== */
function renderLive() {
  const L = ST.live;
  $('#liveDot').className = 'dot' + (L.attached ? '' : ' off');
  $('#liveText').textContent = L.attached
    ? `已连接${L.pid ? ' (PID ' + L.pid + ')' : ''}${L.inScene ? ' · 已进场景' : ' · 未进场景'}`
    : '未连接';
  $('#liveNote').textContent = L.attached ? '' :
    '未找到 MonsterHunterWorld.exe（游戏没开，或还没进任务）。';

  const now = $('#liveNow');
  if (L.attached && L.fsm != null && L.fsm >= 0) {
    now.hidden = false;
    now.innerHTML = '';
    now.append(
      h('div', {}, 'weapon ', h('b', { text: String(L.weapon) }),
        '　fsm ', h('b', { text: String(L.fsm) }),
        '　lmt ', h('b', { text: String(L.lmt) })),
      h('div', { style: 'margin-top:6px;color:var(--muted);font-size:11px' },
        '想给这一招配音效，就把上面的 fsm / lmt 填进条目的 FSMId / LMT。'));
  } else now.hidden = true;

  const hist = $('#liveHist');
  hist.innerHTML = '';
  const H = L.history || [];
  if (!H.length) {
    hist.appendChild(h('div', { class: 'note' }, '还没有捕获记录：进游戏做派生动作后回来查看'));
    return;
  }
  hist.appendChild(h('div', { class: 'note', style: 'margin-bottom:6px' },
    `捕获历史（${H.length} 条，fsm≠0 已高亮）`));
  const show = ST.histExpanded ? H : H.slice(0, 8);
  show.forEach(r => hist.appendChild(h('div', { class: 'hrec' },
    h('b', { text: r.time || '' }),
    h('span', { text: `w${r.weapon} fsm ${r.fsm} lmt ${r.lmt}` }))));
  if (!ST.histExpanded && H.length > 8) {
    const b = h('div', { class: 'hrec more', text: `展开更早的 ${H.length - 8} 条 ▼` });
    b.onclick = () => { ST.histExpanded = true; renderLive();
                        Backend.call('ui.set', { histExpanded: true }).catch(() => {}); };
    hist.appendChild(b);
  } else if (ST.histExpanded && H.length > 8) {
    const b = h('div', { class: 'hrec more', text: '收起历史 ▲' });
    b.onclick = () => { ST.histExpanded = false; renderLive();
                        Backend.call('ui.set', { histExpanded: false }).catch(() => {}); };
    hist.appendChild(b);
  }
}

/* ===========================================================================
   编辑器
   =========================================================================== */
const ED = { open: false, index: null, draft: null, adv: false, judgeBox: false, poolOpen: {} };

const JUDGE_FALLBACK = [
  { i: 0, name: '不判定' },
  { i: 1, name: '造成伤害（命中/落空·任意武器）' },
  { i: 2, name: '太刀 · 登龙 命中/落空' },
  { i: 3, name: '太刀 · 大居 成功/失败' },
  { i: 4, name: '大剑 · 真蓄 命中/落空' },
  { i: 5, name: '自定义（自己写条件）' },
];

function blankEntry() {
  return { name: '', weaponType: ST.weaponFilter >= 0 ? ST.weaponFilter : -1, combo: '',
    fsmId: -1, fsmTarget: -1, group: '', stop: false, lmt: [], lmtAny: true,
    def: [], gauge: [[], [], [], []],
    checkDelayMs: 0, checkTimeoutMs: 2500, checkOffsetMs: 150, endOnAction: true,
    checkMode: 0, judgePreset: 0, conds: [] };
}

async function openEditor(index) {
  let e;
  if (index == null) e = blankEntry();
  else {
    const r = await Backend.call('entries.get', { index });
    e = r.entry;
  }
  ED.open = true; ED.index = index == null ? null : index;
  ED.draft = e;
  ED.adv = !!(e.group || e.stop || (e.fsmTarget >= 0) || (e.checkTimeoutMs > 0) ||
              (e.gauge || []).some(g => g && g.length));
  ED.judgeBox = ED.adv;
  $('#edTitle').textContent = index == null ? '新增条目' : '编辑条目';
  fillEditor();
  $('#editor').classList.add('open');
}

function fillEditor() {
  const e = ED.draft;
  $('#edName').value = e.name || '';
  $('#edFsm').value = e.fsmId;
  $('#edLmtAny').checked = !!e.lmtAny;
  $('#edLmt').value = (e.lmt || []).join(',');
  $('#edAdv').checked = ED.adv;
  $('#edAdvBox').hidden = !ED.adv;
  $('#edGroup').value = e.group || '';
  $('#edFsmTarget').value = e.fsmTarget;
  $('#edStop').checked = !!e.stop;

  // 武器下拉
  const ws = $('#edWeapon');
  ws.innerHTML = '';
  ws.appendChild(h('option', { value: '-1' }, '任意 (-1)'));
  ST.weapons.forEach(w => ws.appendChild(
    h('option', { value: String(w.id), selected: w.id === e.weaponType }, `${w.id} ${w.name}`)));
  ws.onchange = () => { e.weaponType = parseInt(ws.value, 10); e.combo = ''; fillComboSelect(); fillPools(); };

  // 判定下拉
  const js = $('#edJudge');
  js.innerHTML = '';
  (ST.judges.length ? ST.judges : JUDGE_FALLBACK).forEach(j => js.appendChild(
    h('option', { value: String(j.i), selected: j.i === (e.judgePreset || 0) }, j.name)));
  js.onchange = () => { e.judgePreset = parseInt(js.value, 10); judgeNote(); };

  $('#edAdvanced').checked = ED.judgeBox;
  $('#edJudgeBox').hidden = !ED.judgeBox;
  $('#edDelay').value = e.checkDelayMs;
  $('#edTimeout').value = e.checkTimeoutMs;
  $('#edOffset').value = e.checkOffsetMs;
  $('#edEndOnAction').checked = !!e.endOnAction;
  $('#edFinalMode').checked = e.checkMode === 1;

  // 事件（重新绑，避免重复）
  $('#edName').oninput = ev => e.name = ev.target.value;
  $('#edFsm').oninput = ev => e.fsmId = parseInt(ev.target.value, 10) || -1;
  $('#edLmtAny').onchange = ev => { e.lmtAny = ev.target.checked; lmtHint(); };
  $('#edLmt').oninput = ev => { e.lmt = parseLmt(ev.target.value); lmtHint(); };
  $('#edAdv').onchange = ev => { ED.adv = ev.target.checked; $('#edAdvBox').hidden = !ED.adv; fillPools(); };
  $('#edGroup').oninput = ev => e.group = ev.target.value;
  $('#edFsmTarget').oninput = ev => e.fsmTarget = parseInt(ev.target.value, 10);
  $('#edStop').onchange = ev => e.stop = ev.target.checked;
  $('#edDelay').oninput = ev => e.checkDelayMs = parseInt(ev.target.value, 10) || 0;
  $('#edTimeout').oninput = ev => e.checkTimeoutMs = parseInt(ev.target.value, 10) || 0;
  $('#edOffset').oninput = ev => e.checkOffsetMs = parseInt(ev.target.value, 10) || 0;
  $('#edEndOnAction').onchange = ev => e.endOnAction = ev.target.checked;
  $('#edFinalMode').onchange = ev => e.checkMode = ev.target.checked ? 1 : 0;
  $('#edAdvanced').onchange = ev => { ED.judgeBox = ev.target.checked; $('#edJudgeBox').hidden = !ED.judgeBox; };

  fillComboSelect();
  lmtHint();
  judgeNote();
  fillPools();
  fillConds();
}

function lmtHint() {
  const e = ED.draft;
  const el = $('#edLmtHint');
  if (e.lmtAny) { el.textContent = '→ 不限 LMT：只要 FSMId 相同就会触发'; return; }
  const raw = $('#edLmt').value.trim();
  if (!raw) { el.textContent = '→ 空 = 不限 LMT'; return; }
  const bad = raw.split(/[,;\s]+/).filter(s => s && !/^-?\d+$/.test(s));
  if (bad.length) { el.innerHTML = ''; el.append('→ 无法识别：', h('b', { text: bad.join(', ') }),
    '　只能填整数（逗号分隔）'); return; }
  el.textContent = '→ 只匹配 LMT：' + e.lmt.join(', ');
}
function parseLmt(s) {
  return s.split(/[,;\s]+/).map(t => t.trim()).filter(t => /^-?\d+$/.test(t)).map(Number);
}

function fillComboSelect() {
  const e = ED.draft;
  const sel = $('#edCombo');
  sel.innerHTML = '';
  sel.appendChild(h('option', { value: '' }, '默认组合'));
  if (e.weaponType >= 0) {
    Backend.call('combo.list', { weapon: e.weaponType }).then(r => {
      (r.combos || []).filter(c => c).forEach(c => sel.appendChild(
        h('option', { value: c, selected: c === e.combo }, c)));
    }).catch(() => {});
  }
  sel.onchange = () => e.combo = sel.value;
}

function judgeNote() {
  const j = ED.draft.judgePreset || 0;
  const notes = {
    1: '打出伤害就播【命中】的音效，没打中播【落空】。不限定武器和动作。',
    2: '在登龙命中或落空时分别播放不同的 wav。',
    3: '大居成功需要打出伤害且不掉刃，所以在窗口结束时判定。',
    4: '真蓄两段：第一段约 0.65 秒、伤害小，第二段约 1.7~2.1 秒、伤害大。判定起点设在 1200ms 卡在两段中间。',
  };
  $('#edJudgeNote').textContent = j >= 1 && j <= 4 ? notes[j]
    : (j === 5 ? '自己写条件表达式，例如 dmg>0 & dAura>=0' : '动作一匹配就播，不做判定。');
}

/* ---- 音效卡片（普通池 / 刃时池 / 判定池共用同一套控件）---- */
function soundCard(sp, onRemove, onChange) {
  const wrap = h('div', { class: 'card2' });

  const pathRow = h('div', { class: 'path' }, sp.path || '(未选文件)');
  if (sp.fixed) pathRow.appendChild(h('span', { class: 'badge', text: 'F 固定', style: 'margin-left:7px' }));
  wrap.appendChild(pathRow);

  const mk = (label, get, set, min, max, step, w) => {
    const num = h('input', { type: 'number', class: 'mini', value: String(get()), min: String(min), max: String(max) });
    const rng = h('input', { type: 'range', min: String(min), max: String(max), value: String(get()),
      style: 'width:' + (w || 90) + 'px' });
    let syncing = false;
    const push = v => {
      if (syncing) return;
      syncing = true;
      v = Math.max(min, Math.min(max, v | 0));
      set(v); num.value = String(v); rng.value = String(v);
      syncing = false; onChange && onChange();
    };
    rng.oninput = () => push(parseInt(rng.value, 10));
    num.oninput = () => push(parseInt(num.value, 10) || 0);
    return h('div', { class: 'row' }, label, rng, num, h('em', {}, step));
  };

  wrap.appendChild(mk('延时', () => sp.delay, v => sp.delay = v, 0, 5000, 'ms', 110));
  wrap.appendChild(mk('音量', () => sp.vol, v => sp.vol = v, 0, 100, '', 80));
  wrap.appendChild(mk('冷却', () => sp.cdMs, v => sp.cdMs = v, 0, 60000, 'ms', 80));

  const fixedCb = h('input', { type: 'checkbox', checked: !!sp.fixed });
  fixedCb.onchange = () => { sp.fixed = fixedCb.checked; onChange && onChange(); };
  const lockCb = h('input', { type: 'checkbox', checked: !!sp.playLock });
  lockCb.onchange = () => { sp.playLock = lockCb.checked; onChange && onChange(); };

  wrap.appendChild(h('div', { class: 'row' },
    h('label', { class: 'inline' }, fixedCb, '固定(F)：命中时总是播放这条'),
    h('label', { class: 'inline' }, lockCb, '播放期间不重复')));
  wrap.appendChild(h('div', { class: 'row' },
    h('button', { onclick: () => Backend.call('sound.preview', { path: sp.path, vol: sp.vol, delay: sp.delay })
      .catch(e => toast('试听失败：' + e.message, 'err')) }, '试听'),
    h('button', { onclick: async () => {
      const r = await Backend.call('sound.pick', {});
      if (r && r.path) { sp.path = r.path; onChange && onChange(); refreshSoundCards(); }
    } }, '换'),
    h('button', { class: 'del', onclick: () => { onRemove(); } }, '移除')));
  return wrap;
}

/* ---- 音效池 ---- */
let POOL_REBUILD = null;
function refreshSoundCards() {
  if (POOL_REBUILD) POOL_REBUILD();
}
function fillPools() {
  const box = $('#edPools');
  const e = ED.draft;
  const rebuild = () => { box.innerHTML = ''; buildPools(box, e); };
  POOL_REBUILD = rebuild;
  rebuild();
}

function buildPools(box, e) {
  const isLS = e.weaponType === 3;
  e.gauge = e.gauge || [[], [], [], []];

  const pool = (title, arr, key, canRemove) => {
    const openKey = key;
    const id = key;
    const open = ED.poolOpen[id] !== false;
    const head = h('div', { class: 'poolhead' + (open ? '' : ' closed') },
      h('span', { class: 'arrow', text: '▼' }), `${title}  ${arr.length} 条`);
    head.onclick = () => { ED.poolOpen[id] = !open; fillPools(); };
    box.appendChild(head);
    if (!open) return;

    arr.forEach((sp, i) => {
      box.appendChild(soundCard(sp, () => { arr.splice(i, 1); fillPools(); }, () => {}));
    });
    if (!arr.length) box.appendChild(h('div', { class: 'note', style: 'margin-bottom:8px' }, '(空)'));

    const inp = h('input', { type: 'text', placeholder: '路径，如 sounds/xxx.wav' });
    const add = h('button', { onclick: () => {
      const v = inp.value.trim(); if (!v) return;
      if (arr.some(s => s.path === v)) { toast('这条音效已经在了', 'err'); return; }
      arr.push({ path: v, delay: 0, vol: 100, fixed: false, cdMs: 0, playLock: false });
      inp.value = ''; fillPools();
    } }, '添加');
    const browse = h('button', { onclick: async () => {
      const r = await Backend.call('sound.pickMulti', {});
      (r.paths || []).forEach(p => {
        if (!arr.some(s => s.path === p))
          arr.push({ path: p, delay: 0, vol: 100, fixed: false, cdMs: 0, playLock: false });
      });
      fillPools();
    } }, '浏览…');
    box.appendChild(h('div', { class: 'addrow' }, inp, add, browse));
  };

  $('#edDefHead').textContent = isLS ? '默认音效（任意刃时）' : '默认音效';
  pool('默认音效', e.def, 'def', true);

  if (isLS && ED.adv) {
    box.appendChild(h('div', { class: 'sect' }, '刃时音效（太刀专用，未配置的刃色回退默认音效）'));
    ['无刃时', '白刃时', '黄刃时', '红刃时'].forEach((nm, i) => {
      pool(nm, e.gauge[i], 'g' + i, true);
    });
  }
}

/* ---- 判定条件 ---- */
function fillConds() {
  const box = $('#edConds');
  box.innerHTML = '';
  const e = ED.draft;
  e.conds = e.conds || [];
  e.conds.forEach((c, i) => {
    const expr = h('input', { type: 'text', value: c.expr || '', placeholder: '如 dmg>0 & dAura>=0',
      style: 'flex:1;min-width:220px' });
    expr.oninput = () => c.expr = expr.value;
    const atEnd = h('input', { type: 'checkbox', checked: !!c.atEnd });
    atEnd.onchange = () => c.atEnd = atEnd.checked;

    const del = h('button', { class: 'del', onclick: async () => {
      const ok = await confirmBox({ title: '删除条件', danger: true, okText: '删除',
        message: `确定删除条件「${c.expr || '(空)'}」吗？\n它的音效池（${(c.pool || []).length} 条）会一起删掉。` });
      if (!ok) return;
      e.conds.splice(i, 1); fillConds();
    } }, '删除');

    const cb = h('div', { class: 'condbox' },
      h('div', { class: 'row' }, h('span', { style: 'color:var(--muted);font-size:12px' }, '条件'), expr, del),
      h('div', { class: 'row' },
        h('label', { class: 'inline' }, atEnd, '只在窗口结束时判定（SoundEnd:）'),
        h('span', { class: 'tip' }, '变量：dmg / aura / dAura / charge / dCharge / lmt / fsm / fsmTarget / ms')));
    c.pool = c.pool || [];
    c.pool.forEach((sp, k) => cb.appendChild(
      soundCard(sp, () => { c.pool.splice(k, 1); fillConds(); }, () => {})));
    const inp = h('input', { type: 'text', placeholder: '路径，如 sounds/命中.wav' });
    cb.appendChild(h('div', { class: 'addrow' }, inp,
      h('button', { onclick: () => {
        const v = inp.value.trim(); if (!v) return;
        c.pool.push({ path: v, delay: 0, vol: 100, fixed: false, cdMs: 0, playLock: false });
        inp.value = ''; fillConds();
      } }, '添加'),
      h('button', { onclick: async () => {
        const r = await Backend.call('sound.pickMulti', {});
        (r.paths || []).forEach(p => c.pool.push(
          { path: p, delay: 0, vol: 100, fixed: false, cdMs: 0, playLock: false }));
        fillConds();
      } }, '浏览…')));
    box.appendChild(cb);
  });
  if (!e.conds.length) box.appendChild(h('div', { class: 'note' }, '还没有条件：点下面「＋ 添加条件」'));
}

async function saveEditor() {
  const e = ED.draft;
  if (!e.name) e.name = (e.fsmId >= 0 ? 'FSM ' + e.fsmId : '条目');
  if (e.weaponType < 0) e.combo = '';
  try {
    await Backend.call('entries.save', { index: ED.index, entry: e });
    $('#editor').classList.remove('open');
    ED.open = false;
    await refresh();
    toast(ED.index == null ? '已新增条目' : '已修改条目（记得保存）', 'ok');
  } catch (err) { toast('保存失败：' + err.message, 'err'); }
}

/* ===========================================================================
   模态：FSM 查询 / ID 库 / 更新
   =========================================================================== */
function openModal(id) { $(id).classList.add('open'); if (id === '#mUpd') refreshUpdate(); }
function closeModals() { $$('.modal').forEach(m => { if (m.id !== 'mConfirm') m.classList.remove('open'); }); }

async function runFsmSearch() {
  const q = $('#fsmQ').value;
  const w = parseInt($('#fsmW').value, 10);
  try {
    const r = await Backend.call('fsm.search', { q, weapon: w });
    const tb = $('#fsmBody'); tb.innerHTML = '';
    (r.rows || []).forEach(row => tb.appendChild(h('tr', {},
      h('td', {}, row.name || ''),
      h('td', {}, wInline(row.weapon)),
      h('td', { class: 'lmt' }, String(row.lmt)),
      h('td', { class: 'fsm' }, String(row.fsm)),
      h('td', { class: 'fsm' }, row.source || ''))));
    $('#fsmFoot').textContent = `共 ${r.total || 0} 条`;
  } catch (e) { toast('查询失败：' + e.message, 'err'); }
}

async function refreshIds() {
  $('#idsPaths').textContent = '基础库：fsm_db.csv（随版本更新，只追加行）\n用户库：fsm_db_user.csv（你的实测/导入都进这里）';
  try {
    const r = await Backend.call('fsm.captured', {});
    const tb = $('#idsBody'); tb.innerHTML = '';
    (r.captured || []).forEach(c => tb.appendChild(h('tr', {},
      h('td', {}, c.name || ''), h('td', {}, wInline(c.weapon)),
      h('td', { class: 'lmt' }, String(c.lmt)), h('td', { class: 'fsm' }, String(c.fsm)),
      h('td', {}, c.added ? h('span', { class: 'badge', text: '已添加' }) : ''))));
  } catch {}
}

/* ===========================================================================
   热键与聊天指令
   键值就是 Windows 虚拟键码（VK_*），和浏览器 keydown 的 keyCode 是同一套，
   所以"点一下格子、再按一个新键"就能直接改绑，不需要手填数字。
   =========================================================================== */
const VK_NAMES = {
  8:'Backspace', 9:'Tab', 13:'Enter', 16:'Shift', 17:'Ctrl', 18:'Alt', 19:'Pause',
  20:'CapsLock', 27:'Esc', 32:'空格', 33:'PageUp', 34:'PageDown', 35:'End', 36:'Home',
  37:'←', 38:'↑', 39:'→', 40:'↓', 45:'Insert', 46:'Delete',
  144:'NumLock', 145:'ScrollLock',
};
const VK_SYMS = {186:';', 187:'=', 188:',', 189:'-', 190:'.', 191:'/', 192:'`',
                 219:'[', 220:'\\', 221:']', 222:"'"};
function vkName(code) {
  if (code == null) return '未设置';
  if (code === 0) return '（关闭）';
  if (VK_NAMES[code]) return VK_NAMES[code];
  if (code >= 48 && code <= 57) return '主键盘 ' + String.fromCharCode(code);
  if (code >= 65 && code <= 90) return String.fromCharCode(code);
  if (code >= 96 && code <= 105) return '小键盘 ' + (code - 96);
  if (code >= 112 && code <= 123) return 'F' + (code - 111);
  if (VK_SYMS[code]) return VK_SYMS[code];
  return '键码 ' + code;
}

// 从 core 读写的字段名 → 界面上的用途说明
const HOTKEYS = [
  { key: 'reloadKey',   label: '重载配置' },
  { key: 'volUpKey',    label: '音量 +5' },
  { key: 'volDownKey',  label: '音量 −5' },
  { key: 'setVolKey',   label: '设为指定音量' },
  { key: 'toggleKey',   label: '总开关 开 / 关' },
  { key: 'moreKey',     label: '更多音效 开 / 关' },
  { key: 'comboKey',    label: '切换配置组合' },
];

// 指令表：内容与插件 WeaponSoundEnhance.cpp 里的 /wse 分支一一对应
const CHAT_CMDS = [
  ['/wse', '查看状态'],
  ['/wse reload', '重新读取 ini，改动立刻生效（等价工具栏「重载游戏配置」）'],
  ['/wse on  /  off', '总开关 开 / 关'],
  ['/wse more  /  one', '更多音效 开 / 只播一条'],
  ['/wse stop', '停掉正在播放的音效'],
  ['/wse vol N', '音量设为 N（0 – 100）'],
  ['/wse vol+  /  vol-', '音量 +5 / −5'],
  ['/wse combo', '切到当前武器的下一个配置组合'],
  ['/wse combo 名字', '切到指定的配置组合'],
  ['/wse combos', '列出该武器所有组合，当前那个用 [ ] 标出'],
];

let HK = {};             // 当前热键值（字段名 → VK 码）
let hkCapture = null;    // 正在等待改绑的字段名

function renderHotkeys() {
  const tb = $('#hkBody');
  tb.innerHTML = '';
  // ★ 回调参数不能叫 h —— 那会遮蔽全局的 DOM 构建函数 h()，
  //   于是 h('button',…) 变成"把对象当函数调用"，整张表一行都建不出来。
  HOTKEYS.forEach(item => {
    const btn = h('button', {
      class: 'keybtn' + (hkCapture === item.key ? ' capturing' : ''),
      title: '点这里，然后按一个新键',
      onclick: () => { hkCapture = item.key; renderHotkeys(); },
    }, hkCapture === item.key ? '按下新键…' : vkName(HK[item.key]));
    tb.appendChild(h('tr', {},
      h('td', {}, item.label),
      h('td', {}, btn),
      h('td', {}, h('button', {
        title: '把这个热键关掉',
        onclick: () => setHotkey(item.key, 0),
      }, '清除'))));
  });
}

function renderChatCmds() {
  const tb = $('#cmdBody');
  tb.innerHTML = '';
  CHAT_CMDS.forEach(([c, d]) => tb.appendChild(h('tr', {},
    h('td', {}, h('code', { text: c })),
    h('td', {}, d))));
}

async function refreshHotkeys() {
  HK = ST.hotkeys || {};
  const g = ST.global || {};
  $('#hkMod').value = String(HK.modifierKey == null ? 17 : HK.modifierKey);
  $('#hkVolVal').value = String(HK.setVolValue == null ? 50 : HK.setVolValue);
  $('#gChatCmd').checked = !!g.chatCommands;
  $('#gChatEcho').checked = !!g.chatEcho;
  hkCapture = null;
  renderHotkeys();
}

async function setHotkey(key, code) {
  const label = (HOTKEYS.find(item => item.key === key) || {}).label || key;
  hkCapture = null;
  try {
    const r = await Backend.call('hotkeys.set', { key, value: code });
    HK = r || HK;
    renderHotkeys();
    // modifierKey / setVolValue 不在 HOTKEYS 表里，名字单独给
    const shown = key === 'modifierKey' ? ({ 0: '不用组合键', 16: 'Shift', 17: 'Ctrl', 18: 'Alt' }[code] || vkName(code))
                : key === 'setVolValue' ? (code + '%')
                : vkName(code);
    toast(`「${label}」已设为 ${shown} — 记得回主界面点「保存」`, 'ok');
  } catch (e) {
    toast('改键失败：' + e.message, 'err');
    await refreshHotkeys();
  }
}

function refreshUpdate() {
  const u = ST.upd;
  $('#updCur').textContent = 'v' + (ST.version || '—');
  $('#updNew').textContent = u.tag || '—';
  $('#updNotes').innerHTML = mdToHtml(u.notes || '');
  $('#updMsg').textContent = u.err ? ('出错：' + u.err) : (u.msg || '');
  const bar = $('#updBar');
  bar.hidden = !(u.state === 1 && u.progress > 0);
  $('#updBarFill').style.width = (u.progress || 0) + '%';
  $('#updInstall').hidden = !(u.state === 3);
  $('#updRestart').hidden = !u.needRestart;
}

/* ===========================================================================
   工具栏动作
   =========================================================================== */
const ACTIONS = {
  'cfg.save':   async () => {
    const r = await Backend.call('config.save', {});
    toast('已保存：' + (r.iniPath || ''), 'ok'); await refresh();
  },
  'cfg.saveAs': async () => {
    const r = await Backend.call('config.saveAs', {});
    if (!r.cancelled) { toast('已另存为：' + r.iniPath, 'ok'); await refresh(); }
  },
  'cfg.open':   async () => {
    const r = await Backend.call('config.open', {});
    if (!r.cancelled) { toast('已打开：' + r.iniPath, 'ok'); await refresh(); }
  },
  'cfg.mergeOld': async () => {
    const r = await Backend.call('config.mergeOldIni', {});
    await refresh();
    toast(r.added ? `已合并 ${r.added} 条` : '没有可合并的新条目', 'ok');
  },
  'game.reload': async () => { await Backend.call('game.reload', {});
    toast('已请求游戏重载配置（改完要先保存）', 'ok'); },
  'game.stop':   async () => { await Backend.call('game.stop', {});
    toast('已请求停止游戏内正在播放的音效', 'ok'); },
  'game.launch': async () => {
    const r = await Backend.call('game.launch', {});
    toast(r.running ? '怪物猎人已在运行，已切到游戏窗口' : '已启动怪物猎人', 'ok');
  },
  'sound.openDir': async () => { await Backend.call('sound.openSoundsDir', {}); },
  'entry.add':  async () => { ST.selIndex = -1; renderTable(); await openEditor(null); },

  'combo.create': async () => {
    const inp = $('#comboNewName');
    const name = inp.value.trim();
    if (!name) { toast('先填组合名', 'err'); return; }
    if (ST.combos.includes(name)) { toast('这个组合名已经有了', 'err'); return; }
    await Backend.call('combo.create', { weapon: ST.weaponFilter, name });
    inp.value = '';
    await loadCombos(); renderComboPanel(); await refresh();
    toast('已新增组合：' + name, 'ok');
  },
  'combo.rename': async () => {
    const w = ST.weaponFilter, from = ST.active[w] || '';
    if (!from) { toast('「默认」组合不能改名', 'err'); return; }
    const to = await confirmBox({ title: '组合改名', input: true, okText: '改名',
      message: `把组合「${from}」改成：`, placeholder: '新组合名' });
    if (!to) return;
    await Backend.call('combo.rename', { weapon: w, from, to });
    await loadCombos(); renderComboPanel(); await refresh();
    toast('已改名为「' + to + '」', 'ok');
  },
  'combo.del': async () => {
    const w = ST.weaponFilter, name = ST.active[w] || '';
    if (!name) { toast('「默认」组合不能删除', 'err'); return; }
    const n = ST.entries.filter(e => e.weaponType === w && e.combo === name).length;
    const ok = await confirmBox({ title: '删除组合', danger: true, okText: '删除组合',
      message: `确定删除组合「${name}」吗？\n\n` +
        (n ? `它下面的 <b>${n} 个条目</b>也会一起删掉。\n` : '这个组合下没有条目。\n') +
        `\n<b>删除后不可撤销</b>。` });
    if (!ok) return;
    const r = await Backend.call('combo.remove', { weapon: w, name });
    await loadCombos(); renderComboPanel(); await refresh();
    toast(`已删除组合「${name}」${r.removedEntries ? ' 及其 ' + r.removedEntries + ' 个条目' : ''}`, 'ok');
  },
  'combo.up':   async () => { await Backend.call('combo.move', { weapon: ST.weaponFilter, name: ST.active[ST.weaponFilter] || '', dir: -1 });
                              await loadCombos(); renderComboPanel(); },
  'combo.down': async () => { await Backend.call('combo.move', { weapon: ST.weaponFilter, name: ST.active[ST.weaponFilter] || '', dir: 1 });
                              await loadCombos(); renderComboPanel(); },
  'combo.export': async () => {
    // 导出的是"某一把武器的当前组合"，所以必须选中具体武器：
    // 选着"全部条目/通用"时 core 会直接报错，这里提前给一句人话，
    // 别让用户只看到一个两秒就消失的错误提示。
    const w = ST.weaponFilter;
    if (w < 0) { toast('先在左栏选一把武器，再导出该武器的组合', 'err'); return; }
    try {
      const r = await Backend.call('combo.export', { weapon: w });
      if (r.cancelled) return;
      toast(`已导出「${ST.active[w] || '默认'}」：${r.copied} 个音效`
            + (r.missing ? `，${r.missing} 个音效文件缺失` : '') + `\n${r.zip}`, 'ok');
    } catch (e) { toast('导出失败：' + e.message, 'err'); }
  },
  'combo.import': async () => {
    const r = await Backend.call('combo.import', {});
    if (r.cancelled) return;
    await refresh();
    toast(`已导入 ${r.added} 条条目、${r.copied} 个音效`, 'ok');
  },

  'win.fsm':    async () => { openModal('#mFsm'); await runFsmSearch(); },
  'win.hot':    async () => { openModal('#mHot'); await refreshHotkeys(); },
  'win.ids':    async () => { openModal('#mIds'); await refreshIds(); },
  'win.update': async () => {
    openModal('#mUpd');
    const r = await Backend.call('update.check', { user: true });
    ST.upd = Object.assign(ST.upd, r); refreshUpdate();
  },
  'm.close': () => closeModals(),

  'ids.export': async () => { const r = await Backend.call('fsm.exportMeasured', {});
    toast(`已导出 ${r.count} 条到 ${r.path}`, 'ok'); await refreshIds(); },
  'ids.import': async () => { const r = await Backend.call('fsm.importCsv', {});
    if (!r.cancelled) { toast(`已合并 ${r.added} 条新动作ID`, 'ok'); await refreshIds(); } },
  'ids.submit': async () => { const r = await Backend.call('fsm.submit', {});
    toast('已写出 CSV 并复制到剪贴板，请在打开的页面粘贴提交', 'ok'); },
  'ids.fetch':  async () => { const r = await Backend.call('fsm.fetchLatest', {});
    toast(`已获取最新库（${r.count} 条）`, 'ok'); await refreshIds(); },

  'upd.check':   async () => { const r = await Backend.call('update.check', { user: true });
    ST.upd = Object.assign(ST.upd, r); refreshUpdate(); },
  'upd.install': async () => { await Backend.call('update.install', {});
    toast('开始下载安装…'); },
  'upd.restart': async () => { await Backend.call('update.restart', {}); },
  'upd.open':    async () => { await Backend.call('update.openLatest', {}); },

  'live.clear': () => { ST.live.history = []; renderLive(); },
  'live.retry': () => { Backend.call('live.retry', {}).catch(() => {}); toast('已请求重新连接'); },

  'ed.close':  () => { $('#editor').classList.remove('open'); ED.open = false; },
  'ed.cancel': () => { $('#editor').classList.remove('open'); ED.open = false; },
  'ed.ok':     () => saveEditor(),
  'ed.addCond': () => {
    ED.draft.conds = ED.draft.conds || [];
    ED.draft.conds.push({ expr: 'dmg>0', atEnd: false, pool: [] });
    fillConds();
  },
};

/* ===========================================================================
   刷新 / 启动
   =========================================================================== */
async function loadEntries() {
  const r = await Backend.call('entries.list', {});
  ST.entries = r.entries || [];
}

async function refresh() {
  const s = await Backend.call('state', {});
  ST.version = s.version;
  ST.themes = s.themes || [];
  ST.weapons = s.weapons || [];
  ST.anyCount = s.anyCount || 0;
  ST.entryCount = s.entryCount || 0;
  ST.judges = s.judges || [];
  ST.global = s.global || {};
  ST.hotkeys = s.hotkeys || {};
  ST.active = s.active || {};
  ST.assetBase = s.assetBase || ST.assetBase;
  ST.iconBase = s.iconBase || ST.iconBase;
  ST.logoUrl = s.logoUrl || ST.logoUrl;

  if (s.ui) {
    if (s.ui.sort && s.ui.sort.key) ST.sort = s.ui.sort;
    if (typeof s.ui.weaponFilter === 'number') ST.weaponFilter = s.ui.weaponFilter;
    if (typeof s.ui.histExpanded === 'boolean') ST.histExpanded = s.ui.histExpanded;
  }
  if (!ST.ready) { applyTheme(s.theme || s.defaultTheme || 'clean-light'); renderThemePicker(); }

  $('#logoImg').src = ST.logoUrl;
  // 版本徽章顺带显示当前主题 id，方便一眼确认主题有没有生效
  $('#verText').textContent = 'v' + (s.version || '—') + ' · ' + (ST.theme || '?');
  $('#statusText').textContent = s.iniPath
    ? ((s.isGameIni ? '✓ 已加载（游戏读取的那份）：' : '⚠ 正在编辑的不是游戏读取的那份：') + s.iniPath)
    : '未加载配置';

  // 全局选项
  const g = ST.global;
  $('#gEnabled').checked = !!g.enabled;
  $('#gMore').checked = !!g.moreSounds;
  $('#gVol').value = g.volume == null ? 50 : g.volume;
  $('#gVolNum').textContent = String(g.volume == null ? 50 : g.volume);
  paintVolBar();   // ★ 数值变了必须重画填充条，否则数字和进度对不上
  $('#gDebug').checked = !!g.debug;
  $('#gHotkeys').checked = !!g.hotkeysEnabled;

  // 条目列表和组合列表互不依赖，并行拉取省一次往返
  await Promise.all([loadEntries(), loadCombos()]);
  renderWeapons();
  renderComboPanel();
  applyFilter();
  renderLive();
  ST.ready = true;
}

// 全局设置写入（主界面选项行与热键窗口都要用，放模块级共用）
function globalSet(k, v) {
  return Backend.call('global.set', { key: k, value: v })
    .catch(e => toast('保存设置失败：' + e.message, 'err'));
}

// 把音量条（range）的填充进度和它的 value 对齐。
// ★ 这个范围条的"填充"是靠 background 渐变画出来的，不是浏览器原生画的；
//   所以每次改 value 都必须手动重画，否则填充位置会停在旧值上。
function paintVolBar() {
  const el = $('#gVol');
  const v = Math.max(0, Math.min(100, parseInt(el.value, 10) || 0));
  el.style.background =
    `linear-gradient(90deg,var(--accent) ${v}%, var(--line-2) ${v}%)`;
}

function wireGlobal() {
  const set = globalSet;
  $('#gEnabled').onchange = e => set('enabled', e.target.checked ? 1 : 0);
  $('#gMore').onchange = e => set('moreSounds', e.target.checked ? 1 : 0);
  $('#gDebug').onchange = e => set('debug', e.target.checked ? 1 : 0);
  $('#gHotkeys').onchange = e => set('hotkeysEnabled', e.target.checked ? 1 : 0);
  $('#gVol').oninput = e => {
    const v = parseInt(e.target.value, 10);
    $('#gVolNum').textContent = String(v);
    paintVolBar();
    set('volume', v);
  };
  paintVolBar();
}

function wireToolbar() {
  $$('[data-act]').forEach(el => {
    const fn = ACTIONS[el.dataset.act];
    if (!fn) return;
    el.addEventListener('click', () => { try { fn(); } catch (e) { toast(String(e.message || e), 'err'); } });
  });
  $$('#entryTable th.sortable').forEach(th => {
    th.addEventListener('click', () => {
      const k = th.dataset.sort;
      if (ST.sort.key === k) ST.sort.asc = !ST.sort.asc;
      else ST.sort = { key: k, asc: true };
      applyFilter();
      Backend.call('ui.set', { sort: ST.sort }).catch(() => {});
    });
  });
  $('#searchBox').addEventListener('input', e => { ST.search = e.target.value; applyFilter(); });
  $('#onlyActive').addEventListener('change', e => {
    ST.onlyActive = e.target.checked; applyFilter();
    Backend.call('ui.set', { onlyActive: ST.onlyActive }).catch(() => {});
  });
  $('#fsmQ').addEventListener('input', runFsmSearch);
  $('#fsmW').addEventListener('change', runFsmSearch);
  $('#mConfirm').addEventListener('click', e => { if (e.target.id === 'mConfirm') $('#cfCancel').click(); });
  $$('.modal').forEach(m => m.addEventListener('click', e => { if (e.target === m) closeModals(); }));
  document.addEventListener('keydown', e => {
    // ★ 改键模式必须最先吃掉按键：否则想按 Esc 取消反而会把窗口关掉
    if (hkCapture) {
      e.preventDefault();
      e.stopPropagation();
      if (e.keyCode === 27 || e.key === 'Escape') {
        hkCapture = null; renderHotkeys(); toast('已取消改键');
        return;
      }
      setHotkey(hkCapture, e.keyCode);
      return;
    }
    if (e.key === 'Escape') { closeModals(); if (!$('#mConfirm').classList.contains('open')) ACTIONS['ed.cancel'](); }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); ACTIONS['cfg.save'](); }
  });

  // ---- 热键窗口里的组合键 / 目标音量 / 聊天指令开关 ----
  $('#hkMod').addEventListener('change', e => setHotkey('modifierKey', parseInt(e.target.value, 10)));
  $('#hkVolVal').addEventListener('change', e => {
    const v = Math.max(0, Math.min(100, parseInt(e.target.value, 10) || 0));
    e.target.value = String(v);
    setHotkey('setVolValue', v);
  });
  $('#gChatCmd').addEventListener('change', e => {
    globalSet('chatCommands', e.target.checked ? 1 : 0);
  });
  $('#gChatEcho').addEventListener('change', e => {
    globalSet('chatEcho', e.target.checked ? 1 : 0);
  });
}

function buildFsmWeaponSelect() {
  const sel = $('#fsmW');
  sel.innerHTML = '';
  sel.appendChild(h('option', { value: '-1' }, '全部武器'));
  (ST.weapons || []).forEach(w => sel.appendChild(h('option', { value: String(w.id) }, `${w.id} ${w.name}`)));
}

Backend.onEvent(ev => {
  if (ev.event === 'live') {
    ST.live = Object.assign(ST.live, ev.data || {});
    renderLive();
  } else if (ev.event === 'update') {
    ST.upd = Object.assign(ST.upd, ev.data || {});
    refreshUpdate();
  } else if (ev.event === 'status') {
    if (ev.data && ev.data.text) $('#statusText').textContent = ev.data.text;
  }
});

(async function main() {
  // 预览用：?theme=pro-dark 可以强制某套主题（不写盘，只影响本次显示）
  const forcedTheme = new URLSearchParams(location.search).get('theme')
    || (location.hash.indexOf('#theme=') === 0 ? location.hash.slice(7) : null);

  // 有任何脚本错误都显式摆到界面上，别静默白屏
  window.addEventListener('error', ev => {
    const el = $('#statusText');
    if (el) el.textContent = '脚本错误：' + (ev.message || ev.error);
  });
  window.addEventListener('unhandledrejection', ev => {
    toast('操作失败：' + ((ev.reason && ev.reason.message) || ev.reason), 'err');
  });

  wireToolbar();
  wireGlobal();
  renderChatCmds();          // 指令表是静态的，建一次就行
  try {
    await refresh();
    // 数据就绪，撤掉启动加载屏
    const splash = $('#splash');
    if (splash) { splash.classList.add('hide'); setTimeout(() => splash.remove(), 300); }
    if (forcedTheme) applyTheme(forcedTheme);
    buildFsmWeaponSelect();
    // 预览用：#editor 直接打开第一条的编辑器（方便截图/试样式）
    if (location.hash.indexOf('editor') >= 0 && ST.visible.length) await openEditor(ST.visible[0].i);
    if (location.hash.indexOf('hot') >= 0) await ACTIONS['win.hot']();
    if (Backend.mode === 'browser')
      toast('浏览器预览模式：数据是假的，只用来调界面', 'ok');
  } catch (e) {
    const splash = $('#splash');
    if (splash) { splash.classList.add('hide'); setTimeout(() => splash.remove(), 300); }
    $('#statusText').textContent = '初始化失败：' + e.message;
    toast('初始化失败：' + e.message, 'err');
  }
})();

})();
