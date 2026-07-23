// ---------------------------------------------------------------------------
// Author:   David Gilinsky
// File:     web/js/app.js
// Purpose:  AirWatcher web UI (vanilla JS SPA): Status, ASIAirs (add/edit +
//           discovery + all copy/delete policies), and Log. No external deps.
// License:  GPL-3.0-or-later
// ---------------------------------------------------------------------------
'use strict';

const $ = (id) => document.getElementById(id);

function el(tag, attrs, ...kids) {
  const e = document.createElement(tag);
  for (const k in (attrs || {})) {
    if (k === 'onclick' || k === 'onchange' || k === 'oninput' || k === 'onsubmit') e[k] = attrs[k];
    else if (k === 'html') e.innerHTML = attrs[k];
    else if (attrs[k] != null) e.setAttribute(k, attrs[k]);
  }
  for (const c of kids.flat()) if (c != null) e.append(c.nodeType ? c : document.createTextNode(String(c)));
  return e;
}

// --- token (optional Bearer auth) ---
const tok = {
  get: () => localStorage.getItem('aw_token') || '',
  set: (t) => t ? localStorage.setItem('aw_token', t) : localStorage.removeItem('aw_token'),
};

async function api(method, path, body) {
  const h = { 'Content-Type': 'application/json' };
  if (tok.get()) h['Authorization'] = 'Bearer ' + tok.get();
  const r = await fetch('/api' + path, { method, headers: h, body: body ? JSON.stringify(body) : undefined });
  const txt = await r.text();
  const data = txt ? JSON.parse(txt) : null;
  if (!r.ok) throw new Error((data && data.error) || ('HTTP ' + r.status));
  return data;
}

function fmtTs(s) {
  if (!s) return '—';
  const d = new Date(s.replace(' ', 'T') + (s.endsWith('Z') ? '' : 'Z'));
  return isNaN(d) ? s : d.toLocaleString();
}

function table(cols, rows, render) {
  const t = el('table');
  t.append(el('thead', {}, el('tr', {}, ...cols.map((c) => el('th', {}, c)))));
  const tb = el('tbody');
  for (const r of rows) tb.append(el('tr', {}, ...render(r)));
  t.append(tb);
  return el('div', { class: 'scroll' }, t);
}

function msg(kind, text) { return el('div', { class: 'msg ' + kind }, text); }
function setStatus(t) { $('status-line').textContent = t; }

const VIEWS = {
  status: { label: 'Status', render: viewStatus },
  asiairs: { label: 'ASIAirs', render: viewAsiairs },
  log: { label: 'Log', render: viewLog },
};

// ---------------------------------------------------------------- Status
async function viewStatus(root) {
  root.append(el('h2', {}, 'Status'));
  let airs = [], status = [];
  try { [airs, status] = await Promise.all([api('GET', '/asiairs'), api('GET', '/status')]); }
  catch (e) { root.append(msg('err', e.message)); return; }
  const byId = {}; status.forEach((s) => (byId[s.asiair] = s));
  if (!airs.length) { root.append(el('p', { class: 'muted' }, 'No ASIAirs configured yet. Add one on the ASIAirs tab.')); return; }
  root.append(table(
    ['ASIAir', 'IP', 'Enabled', 'Total in Autorun', 'Remaining to copy', 'State', 'Last copy'],
    airs.map((a) => ({ a, s: byId[a.id] || {} })),
    ({ a, s }) => [
      el('td', {}, el('strong', {}, a.name || a.id)),
      el('td', {}, s.ip || a.host),
      el('td', {}, el('span', { class: 'pill ' + (a.enabled ? 'on' : 'off') }, a.enabled ? 'on' : 'off')),
      el('td', {}, s.total_autorun != null ? s.total_autorun : '—'),
      el('td', {}, s.remaining != null ? s.remaining : '—'),
      el('td', {}, el('span', { class: 'state-' + (s.state || 'idle') }, s.state || '—')),
      el('td', {}, fmtTs(s.last_copy_utc)),
    ]));
  root.append(el('div', { class: 'row' }, el('button', { class: 'btn ghost sm', onclick: () => render() }, 'Refresh')));
}

// ---------------------------------------------------------------- ASIAirs
async function viewAsiairs(root) {
  root.append(el('h2', {}, 'ASIAirs'));
  root.append(el('div', { class: 'row' },
    el('button', { class: 'btn', onclick: () => showForm(root, null, false) }, '+ Add ASIAir'),
    el('button', { class: 'btn ghost', onclick: () => showDiscover(root) }, 'Scan subnet…'),
    el('span', { class: 'spacer' }),
    el('button', { class: 'btn ghost sm', onclick: () => render() }, 'Refresh')));
  const holder = el('div', { id: 'aw-form-holder' });
  root.append(holder);

  let airs = [];
  try { airs = await api('GET', '/asiairs'); } catch (e) { root.append(msg('err', e.message)); return; }
  if (!airs.length) { root.append(el('p', { class: 'muted' }, 'None yet — add one, or scan your subnet to discover an ASIAir.')); return; }
  root.append(table(
    ['Name', 'Host', 'Share', 'Copy', 'Delete', 'Enabled', ''],
    airs,
    (a) => [
      el('td', {}, el('strong', {}, a.name || a.id), el('div', { class: 'muted' }, a.id)),
      el('td', {}, a.host),
      el('td', {}, a.smb_share || 'EMMC Images'),
      el('td', {}, copyDesc(a)),
      el('td', {}, deleteDesc(a)),
      el('td', {}, el('span', { class: 'pill ' + (a.enabled ? 'on' : 'off') }, a.enabled ? 'on' : 'off')),
      el('td', {}, el('div', { class: 'row', style: 'margin:0;gap:.3rem' },
        el('button', { class: 'btn ghost sm', onclick: () => toggle(a) }, a.enabled ? 'Disable' : 'Enable'),
        el('button', { class: 'btn ghost sm', onclick: () => test(a, root) }, 'Test'),
        el('button', { class: 'btn ghost sm', onclick: () => showForm(root, a, true) }, 'Edit'),
        el('button', { class: 'btn danger sm', onclick: () => del(a) }, 'Delete'))),
    ]));
}

function copyDesc(a) {
  if (a.copy_mode === 'batch') return `batches of ${a.copy_n}`;
  if (a.copy_mode === 'after_frames') return `after ${a.copy_n} frames`;
  if (a.copy_mode === 'time_of_day') return `at ${a.copy_time || '??:??'}`;
  return 'immediately';
}
function deleteDesc(a) {
  if (!a.delete_after_copy || a.delete_via === 'none') return el('span', { class: 'muted' }, 'off');
  let when = 'immediately';
  if (a.delete_mode === 'after_frames') when = `after ${a.delete_n}`;
  else if (a.delete_mode === 'time_of_day') when = `at ${a.delete_time || '??:??'}`;
  return `${when} via ${a.delete_via}`;
}

async function toggle(a) { try { await api('POST', `/asiairs/${a.id}/${a.enabled ? 'disable' : 'enable'}`); render(); } catch (e) { alert(e.message); } }
async function del(a) { if (confirm(`Delete ASIAir "${a.id}"? (copied-file records are cleared; archived frames are kept)`)) { try { await api('DELETE', '/asiairs/' + a.id); render(); } catch (e) { alert(e.message); } } }
async function test(a, root) {
  const h = $('aw-form-holder'); h.innerHTML = '';
  h.append(msg('ok', `Testing ${a.host} / ${a.smb_share || 'EMMC Images'}…`));
  try {
    const r = await api('POST', `/asiairs/${a.id}/test`);
    h.innerHTML = '';
    h.append(msg(r.reachable ? 'ok' : 'err',
      r.reachable ? `Reachable — ${r.autorun_files} FITS in Autorun on ${r.host}/${r.share}`
                  : `Unreachable: ${r.error || 'no response'} (${r.host}/${r.share})`));
  } catch (e) { h.innerHTML = ''; h.append(msg('err', e.message)); }
}

function field(label, input) { return el('div', {}, el('label', {}, label), input); }

function showForm(root, a, editing) {
  const h = $('aw-form-holder'); h.innerHTML = '';
  if (editing === undefined) editing = !!a;  // callers pass it explicitly; discovery "Use" pre-fills a NEW form
  a = a || { copy_mode: 'immediate', delete_via: 'none', delete_mode: 'immediate', smb_share: 'EMMC Images' };
  const inp = (name, val, attrs) => el('input', Object.assign({ name, value: val != null ? val : '' }, attrs || {}));
  const sel = (name, val, opts) => {
    const s = el('select', { name });
    for (const [v, t] of opts) s.append(el('option', Object.assign({ value: v }, v === val ? { selected: 'selected' } : {}), t));
    return s;
  };
  const chk = (name, on) => el('input', Object.assign({ name, type: 'checkbox' }, on ? { checked: 'checked' } : {}));

  const idInput = inp('id', a.id, editing ? { disabled: 'disabled' } : { placeholder: 'e.g. air-backyard' });
  const form = el('div', { class: 'panel' },
    el('h3', {}, editing ? `Edit ${a.id}` : 'Add ASIAir'),
    el('div', { class: 'grid' },
      field('ID (unique)', idInput),
      field('Name', inp('name', a.name, { placeholder: 'Backyard ASIAir' })),
      field('Host / IP', inp('host', a.host, { placeholder: '172.22.4.117' })),
      field('SMB share', inp('smb_share', a.smb_share, { placeholder: 'EMMC Images' }))),
    el('h3', {}, 'Copy policy'),
    el('div', { class: 'grid' },
      field('When to copy', sel('copy_mode', a.copy_mode, [['immediate', 'Immediately'], ['batch', 'In batches of N'], ['after_frames', 'After N frames'], ['time_of_day', 'At a time of day']])),
      field('N (batch size / frame threshold)', inp('copy_n', a.copy_n, { type: 'number', min: '0' })),
      field('Time of day (HH:MM local)', inp('copy_time', a.copy_time, { placeholder: '02:00' }))),
    el('h3', {}, 'Delete policy'),
    el('div', { class: 'grid' },
      field('Delete after copy?', el('div', { class: 'row', style: 'margin:.3rem 0' }, chk('delete_after_copy', a.delete_after_copy), el('span', { class: 'muted' }, 'remove frames from the ASIAir once copied'))),
      field('Via', sel('delete_via', a.delete_via, [['none', 'Off'], ['smb', 'SMB (works on your firmware)'], ['ssh', 'SSH (needs a rooted ASIAir)']])),
      field('When to delete', sel('delete_mode', a.delete_mode, [['immediate', 'Immediately'], ['after_frames', 'After N copied'], ['time_of_day', 'At a time of day']])),
      field('N (copied-frame threshold)', inp('delete_n', a.delete_n, { type: 'number', min: '0' })),
      field('Time of day (HH:MM local)', inp('delete_time', a.delete_time, { placeholder: '12:00' })),
      field('SSH user (if via SSH)', inp('ssh_user', a.ssh_user, { placeholder: 'root' }))),
    el('h3', {}, 'Other'),
    el('div', { class: 'grid' },
      field('Timezone (for time-of-day)', inp('timezone', a.timezone, { placeholder: 'America/Phoenix' })),
      field('Notes', inp('notes', a.notes, {}))),
    el('div', { class: 'row' },
      el('button', { class: 'btn', onclick: () => submitForm(form, editing) }, editing ? 'Save' : 'Add'),
      el('button', { class: 'btn ghost', onclick: () => (h.innerHTML = '') }, 'Cancel'),
      el('span', { id: 'form-msg' })));
  h.append(form);
  if (!editing) idInput.focus();
}

async function submitForm(form, editing) {
  const g = (n) => form.querySelector(`[name=${n}]`);
  const body = {
    name: g('name').value.trim(), host: g('host').value.trim(), smb_share: g('smb_share').value.trim(),
    copy_mode: g('copy_mode').value, copy_n: parseInt(g('copy_n').value || '0', 10) || 0, copy_time: g('copy_time').value.trim(),
    delete_after_copy: g('delete_after_copy').checked, delete_via: g('delete_via').value,
    delete_mode: g('delete_mode').value, delete_n: parseInt(g('delete_n').value || '0', 10) || 0,
    delete_time: g('delete_time').value.trim(), ssh_user: g('ssh_user').value.trim(),
    timezone: g('timezone').value.trim(), notes: g('notes').value.trim(),
  };
  const id = g('id').value.trim();
  const mm = $('form-msg');
  try {
    if (editing) await api('PATCH', '/asiairs/' + id, body);
    else { body.id = id; await api('POST', '/asiairs', body); }
    render();
  } catch (e) { mm.replaceChildren(msg('err', e.message)); }
}

async function showDiscover(root) {
  const h = $('aw-form-holder'); h.innerHTML = '';
  const cidr = el('input', { name: 'cidr', value: '172.22.4.0/24', placeholder: 'a.b.c.d/prefix', style: 'max-width:220px' });
  const out = el('div');
  const scan = async () => {
    out.replaceChildren(msg('ok', 'Scanning ' + cidr.value + ' for ASIAirs (port 4400)…'));
    try {
      const found = await api('GET', '/discover?cidr=' + encodeURIComponent(cidr.value));
      out.innerHTML = '';
      if (!found.length) { out.append(el('p', { class: 'muted' }, 'No ASIAirs found on ' + cidr.value + '.')); return; }
      out.append(table(['IP', 'SMB', 'Name', ''], found, (d) => [
        el('td', {}, d.ip),
        el('td', {}, d.smb_open ? 'yes' : 'no'),
        el('td', {}, d.name || '—'),
        el('td', {}, el('button', { class: 'btn sm', onclick: () => useCandidate(root, d) }, 'Use')),
      ]));
    } catch (e) { out.innerHTML = ''; out.append(msg('err', e.message)); }
  };
  h.append(el('div', { class: 'panel' },
    el('h3', {}, 'Discover ASIAirs'),
    el('div', { class: 'row' }, cidr, el('button', { class: 'btn', onclick: scan }, 'Scan'),
      el('button', { class: 'btn ghost', onclick: () => (h.innerHTML = '') }, 'Close')),
    out));
}

function useCandidate(root, d) {
  const base = d.name && /asiair/i.test(d.name) ? d.name.split('.')[0] : ('air-' + d.ip.replace(/\./g, '-'));
  showForm(root, { id: base, name: d.name || ('ASIAir ' + d.ip), host: d.ip, smb_share: 'EMMC Images', copy_mode: 'immediate', delete_via: 'none', delete_mode: 'immediate' }, false);
}

// ---------------------------------------------------------------- Log
async function viewLog(root) {
  root.append(el('h2', {}, 'Activity log'));
  let rows = [];
  try { rows = await api('GET', '/log?limit=300'); } catch (e) { root.append(msg('err', e.message)); return; }
  if (!rows.length) { root.append(el('p', { class: 'muted' }, 'No activity yet.')); return; }
  root.append(table(['Time', 'ASIAir', 'Action', 'File', 'Status', 'Detail'], rows, (r) => [
    el('td', {}, fmtTs(r.ts_utc)),
    el('td', {}, r.asiair),
    el('td', {}, r.action),
    el('td', { class: 'muted' }, r.file || '—'),
    el('td', {}, el('span', { class: r.status === 'error' ? 'state-error' : (r.status === 'ok' ? 'state-idle' : '') }, r.status || '')),
    el('td', { class: 'muted' }, r.detail || ''),
  ]));
  root.append(el('div', { class: 'row' }, el('button', { class: 'btn ghost sm', onclick: () => render() }, 'Refresh')));
}

// ---------------------------------------------------------------- shell
function currentView() { const h = (location.hash || '').replace('#', ''); return VIEWS[h] ? h : 'status'; }

function refreshNav() {
  const nav = $('nav'); nav.innerHTML = '';
  const cur = currentView();
  for (const k in VIEWS) {
    nav.append(el('button', { class: k === cur ? 'active' : '', onclick: () => { location.hash = k; } }, VIEWS[k].label));
  }
}

async function render() {
  refreshNav();
  const root = $('view'); root.innerHTML = '';
  try { await VIEWS[currentView()].render(root); } catch (e) { root.append(msg('err', e.message)); }
}

async function boot() {
  let health;
  try { health = await api('GET', '/health'); }
  catch (e) { $('view').replaceChildren(msg('err', 'Cannot reach AirWatcher API: ' + e.message)); return; }
  if (health.auth && !tok.get()) {
    const t = prompt('AirWatcher requires an access token:');
    if (t) tok.set(t);
  }
  setStatus('AirWatcher ' + health.version + (health.auth ? ' · token set' : ''));
  window.addEventListener('hashchange', render);
  render();
}

boot();
