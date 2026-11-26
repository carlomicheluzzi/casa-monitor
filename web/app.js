'use strict';

const $ = id => document.getElementById(id);
const number = (value, digits = 0) => new Intl.NumberFormat('it-IT', { minimumFractionDigits: digits, maximumFractionDigits: digits }).format(value);
let mode = new URLSearchParams(location.search).get('mode');
if (!['cloud', 'demo'].includes(mode)) mode = null;
let state = null, token = '', audio = null, audioEnabled = false, polling = false;
let fetchedAt = 0, failedAt = 0, failureMessage = '', audioIssue = '', lastAnnouncement = '';
let previousConfig = '', settingsConfig = null;
let detailId = null, assignmentConfig = null;
const tiles = new Map();
const reasonLabels = { waiting: 'In attesa', verifying: 'In verifica', offline: 'Offline', invalid: 'Non valido', stale: 'Dato scaduto', connection: 'Senza rete', not_installed: 'Da installare' };

function icon(name) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('aria-hidden', 'true');
  const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
  use.setAttribute('href', '#i-' + name); svg.append(use); return svg;
}

function makeTile(circuit) {
  const article = document.createElement('article'); article.className = 'tile'; article.id = 'tile-' + circuit.id;
  const top = document.createElement('div'); top.className = 'tile-top';
  const name = document.createElement('h3'); name.className = 'tile-name';
  const label = document.createElement('span'); label.textContent = circuit.label;
  name.append(icon(circuit.kind === 'light' ? 'light' : 'power'), label);
  const age = document.createElement('span'); age.className = 'tile-age'; top.append(name, age);
  const value = document.createElement('div'); value.className = 'tile-number';
  const power = document.createElement('strong'); power.textContent = '—';
  const unit = document.createElement('span'); unit.textContent = 'W'; value.append(power, unit);
  const spark = document.createElementNS('http://www.w3.org/2000/svg', 'svg'); spark.setAttribute('viewBox', '0 0 104 32'); spark.classList.add('sparkline'); spark.setAttribute('aria-hidden', 'true');
  const line = document.createElementNS('http://www.w3.org/2000/svg', 'polyline'); spark.append(line);
  const bottom = document.createElement('div'); bottom.className = 'tile-bottom';
  const source = document.createElement('span'); source.className = 'source';
  const voltage = document.createElement('span'); voltage.className = 'voltage'; bottom.append(source, voltage);
  article.append(top, value, spark, bottom);
  const open = document.createElement('button'); open.className = 'tile-open'; open.type = 'button';
  open.setAttribute('aria-label', `Apri dettaglio ${circuit.label}, ${circuit.zone}`); open.setAttribute('aria-haspopup', 'dialog');
  open.addEventListener('click', () => openDetail(circuit.id)); article.append(open);
  const arrow = document.createElement('span'); arrow.className = 'tile-arrow'; arrow.textContent = '↗'; arrow.setAttribute('aria-hidden', 'true'); name.append(arrow);
  tiles.set(circuit.id, { article, label, age, power, voltage, source, line });
  return article;
}

function buildTiles() {
  const signature = JSON.stringify(state.config.circuits);
  if (signature === previousConfig) return;
  previousConfig = signature; tiles.clear();
  $('zone1-cards').replaceChildren(); $('zone2-cards').replaceChildren();
  state.config.circuits.forEach(c => $(c.id.endsWith('1') ? 'zone1-cards' : 'zone2-cards').append(makeTile(c)));
  $('zone1-name').textContent = state.config.circuits.find(c => c.id === 'luce1').zone;
  $('zone2-name').textContent = state.config.circuits.find(c => c.id === 'luce2').zone;
}

function sparkline(line, circuit) {
  const samples = state.history.slice(-35).map(sample => sample.circuits[circuit.id]);
  if (!circuit.available || samples.length < 2) { line.removeAttribute('points'); return; }
  const values = samples.filter(v => v !== null);
  // Draw only the contiguous suffix: gaps must not look like measured history.
  const gap = samples.lastIndexOf(null), tail = samples.slice(gap + 1);
  if (tail.length < 2 || !values.length) { line.removeAttribute('points'); return; }
  const low = Math.min(...tail), high = Math.max(...tail), range = Math.max(high - low, high * .05, 2);
  line.setAttribute('points', tail.map((v, i) => `${i / (tail.length - 1) * 104},${29 - (v - low) / range * 24}`).join(' '));
}

function announce(message) {
  if (message !== lastAnnouncement) { $('announcement').textContent = message; lastAnnouncement = message; }
}

function render() {
  if (!state) return;
  const elapsed = Math.max(0, (performance.now() - fetchedAt) / 1000);
  const localLost = !!failedAt || elapsed > 8;
  const valid = state.complete && !localLost;
  const level = state.alarm.active ? 'alarm' : valid ? state.alarm.level : 'unavailable';
  document.body.dataset.level = level;
  document.title = `${mode === 'demo' ? '[Prova] ' : ''}${level === 'alarm' ? '⚠ ' : ''}${valid ? number(state.total_w / 1000, 2) + ' kW · ' : ''}Casa Energia`;
  buildTiles();
  $('mode-cloud').classList.toggle('selected', mode === 'cloud'); $('mode-cloud').setAttribute('aria-pressed', mode === 'cloud');
  $('mode-demo').classList.toggle('selected', mode === 'demo'); $('mode-demo').setAttribute('aria-pressed', mode === 'demo');
  $('demo-banner').hidden = mode !== 'demo'; $('simulation-controls').hidden = mode !== 'demo';
  document.querySelectorAll('[data-scenario]').forEach(button => { const selected = button.dataset.scenario === state.scenario; button.classList.toggle('selected', selected); button.setAttribute('aria-pressed', selected); });
  const config = state.config;
  $('total').textContent = valid ? number(state.total_w / 1000, 2) : '—';
  $('total-caption').textContent = valid ? (mode === 'demo' ? 'Quattro circuiti simulati' : 'Somma dei quattro circuiti monitorati') : 'Totale non disponibile · verifico le misure';
  $('remaining').textContent = valid ? number(Math.abs(state.remaining_w) / 1000, 2) + ' kW' : '— kW';
  $('budget-title').textContent = valid && state.remaining_w < 0 ? 'Oltre il limite' : 'Margine disponibile';
  $('meter-fill').style.width = valid ? Math.min(100, Math.max(0, state.total_w / config.limit_w * 100)) + '%' : '0%';
  $('meter').setAttribute('aria-valuemax', config.limit_w);
  if (valid) { $('meter').setAttribute('aria-valuenow', Math.max(0, Math.min(config.limit_w, state.total_w))); $('meter').setAttribute('aria-valuetext', number(state.total_w) + ' W su ' + number(config.limit_w) + ' W'); }
  else { $('meter').removeAttribute('aria-valuenow'); $('meter').setAttribute('aria-valuetext', 'Dati non disponibili'); }
  $('warning-marker').style.left = config.warning_w / config.limit_w * 100 + '%';
  $('limit-label').textContent = number(config.limit_w / 1000, 2) + ' kW';
  const status = {
    normal: ['✓', 'Consumi sotto controllo', 'Nessun superamento della soglia'],
    warning: ['!', 'Vicino al limite', 'Riduci i carichi prima di raggiungere ' + number(config.limit_w / 1000, 2) + ' kW'],
    alarm: ['!', !valid ? 'Allarme attivo · dati da verificare' : state.total_w > config.limit_w ? 'Soglia superata' : 'Allarme attivo · attendo il rientro', !valid ? 'Il rientro non è ancora confermato' : state.total_w > config.limit_w ? 'Riduci i carichi elettrici di casa' : 'Rientro a ' + number((config.limit_w - config.hysteresis_w) / 1000, 2) + ' kW o meno'],
    unavailable: ['·', 'Dati da verificare', localLost ? 'Collegamento al monitor interrotto' : state.connection.error || (state.circuits.some(c => c.reason === 'verifying') ? 'Attendo un nuovo aggiornamento dai dispositivi' : 'Servono quattro misure valide e aggiornate')],
  }[level];
  $('status-symbol').textContent = status[0]; $('status-title').textContent = status[1]; $('status-description').textContent = status[2];
  announce((mode === 'demo' ? 'Simulazione. ' : '') + status[1]);
  for (const c of state.circuits) {
    const tile = tiles.get(c.id), available = c.available && !localLost;
    tile.article.classList.toggle('unavailable', !available);
    tile.power.textContent = available ? number(c.power_w, c.power_w < 100 && c.power_w >= 0 ? 1 : 0) : '—';
    tile.article.setAttribute('aria-label', `${c.zone}, ${c.label}: ${available ? number(c.power_w, 1) + ' watt' : 'dato non disponibile'}`);
    tile.age.textContent = available ? (mode === 'demo' ? 'Simulato' : Math.floor((c.age_s || 0) + elapsed) + ' s fa') : localLost ? 'Senza rete' : reasonLabels[c.reason];
    tile.source.textContent = `…${c.device_id.slice(-6)} · CH ${c.channel}`;
    tile.source.title = `Shelly ${c.device_id}, canale ${c.channel}`;
    tile.voltage.textContent = available && c.voltage_v !== null ? number(c.voltage_v, 1) + ' V' : c.last_power_w !== null ? 'Ultimo: ' + number(c.last_power_w, 1) + ' W' : 'In attesa';
    sparkline(tile.line, { ...c, available });
  }
  for (const suffix of ['1', '2']) {
    const circuits = state.circuits.filter(c => c.id.endsWith(suffix));
    $('zone' + suffix + '-total').textContent = !localLost && circuits.every(c => c.available) ? number(circuits.reduce((sum, c) => sum + c.power_w, 0), 1) + ' W' : '— W';
  }
  $('mapping-note').textContent = config.mapping_confirmed ? 'Associazione delle zone verificata ↗' : 'Associazione delle zone da confermare ↗';
  $('mapping-note').classList.toggle('confirmed', config.mapping_confirmed);
  const connectionOkay = !localLost && (mode === 'demo' || state.connection.last_success_age_s !== null && !state.connection.error);
  $('connection-dot').className = 'connection-dot ' + (connectionOkay ? 'ok' : 'error');
  $('connection-label').textContent = mode === 'demo' ? 'Simulazione locale · nessun consumo reale' : connectionOkay ? 'Shelly Cloud · richiesta ogni 3 s' : 'Shelly Cloud · riconnessione in corso';
  $('clock').textContent = new Intl.DateTimeFormat('it-IT', { hour: '2-digit', minute: '2-digit', second: '2-digit', timeZone: 'Europe/Rome' }).format(new Date());
  const muted = Math.max(0, Math.ceil(state.alarm.muted_seconds - elapsed));
  $('mute').hidden = !state.alarm.active;
  $('mute').textContent = muted > 0 ? 'Silenziato · ' + muted + ' s' : 'Silenzia ' + (config.mute_seconds % 60 === 0 ? config.mute_seconds / 60 + ' min' : config.mute_seconds + ' s');
  $('mute').disabled = muted > 0 || localLost;
  const audioReady = audioEnabled && audio && audio.state === 'running';
  $('audio-title').textContent = audioReady ? muted > 0 ? 'Allarme silenziato temporaneamente' : 'Allarme acustico attivo' : audioEnabled ? 'Audio sospeso dal browser' : 'Allarme acustico disattivato';
  $('audio-description').textContent = audioReady ? 'Mantieni aperta questa finestra e il volume udibile' : audioEnabled ? 'Premi Riattiva audio per riprendere' : 'Attiva l’audio in questa finestra';
  $('enable-audio').textContent = audioReady ? 'Disattiva audio' : audioEnabled ? 'Riattiva audio' : 'Attiva audio';
  $('enable-audio').setAttribute('aria-pressed', !!audioReady); $('test-audio').disabled = !audioReady;
  $('notice').textContent = failureMessage || audioIssue;
  renderDetail();
}

function openDetail(id) {
  if (!state) return;
  detailId = id; renderDetail(); $('detail-dialog').showModal();
}

function renderDetail() {
  if (!detailId || !state) return;
  const circuit = state.circuits.find(c => c.id === detailId);
  if (!circuit) return;
  const localLost = !!failedAt || (performance.now() - fetchedAt) / 1000 > 8;
  const valid = circuit.available && !localLost;
  $('detail-global-alarm').hidden = !state.alarm.active;
  const alarmText = mode === 'demo' ? 'Allarme simulato attivo' : 'Allarme casa attivo';
  if ($('detail-alarm-text').textContent !== alarmText) $('detail-alarm-text').textContent = alarmText;
  const muteRemaining = Math.max(0, Math.ceil(state.alarm.muted_seconds - (performance.now() - fetchedAt) / 1000));
  $('detail-mute').textContent = muteRemaining ? 'Silenziato · ' + muteRemaining + ' s' : 'Silenzia allarme';
  $('detail-mute').disabled = muteRemaining > 0 || localLost;
  $('detail-title').textContent = circuit.label + ' · ' + circuit.zone;
  $('detail-kicker').textContent = mode === 'demo' ? 'SIMULAZIONE · DETTAGLIO CIRCUITO' : 'DETTAGLIO CIRCUITO';
  $('detail-source').textContent = `Shelly EM …${circuit.device_id.slice(-6)} · canale ${circuit.channel}`;
  $('detail-status').textContent = valid ? mode === 'demo' ? 'Valori simulati · esempio di suddivisione' : 'Misura aggiornata' : localLost ? 'Collegamento al monitor interrotto' : reasonLabels[circuit.reason];
  $('detail-status').classList.toggle('not-current', !valid);
  $('detail-power').textContent = valid ? number(circuit.power_w, 1) + ' W' : '— W';
  $('detail-energy').textContent = valid && circuit.energy_wh != null ? number(circuit.energy_wh / 1000, 3) + ' kWh' : '— kWh';
  $('detail-energy-note').textContent = mode === 'demo' ? 'La simulazione non inventa un contatore di energia cumulata.' : 'Contatore del misuratore dalla sua installazione o dall’ultimo azzeramento; non è il consumo di oggi.';
  const samples = state.history.slice(-100).map(s => s.circuits[detailId]);
  const tail = samples.slice(samples.lastIndexOf(null) + 1);
  if (valid && tail.length >= 2) {
    const low = Math.min(...tail), high = Math.max(...tail), range = Math.max(high - low, high * .05, 2);
    $('detail-trend-line').setAttribute('points', tail.map((v, i) => `${i / (tail.length - 1) * 560},${65 - (v - low) / range * 58}`).join(' '));
    $('detail-range').textContent = `${number(low, 1)} – ${number(high, 1)} W`;
    $('detail-trend-note').textContent = `${tail.length} campioni consecutivi della sessione${mode === 'demo' ? ' · simulati' : ''}. Le interruzioni dei dati non vengono collegate nel grafico.`;
  } else {
    $('detail-trend-line').removeAttribute('points'); $('detail-range').textContent = '';
    $('detail-trend-note').textContent = valid ? 'In attesa di almeno due campioni aggiornati.' : 'Andamento sospeso: i dati non sono aggiornati.';
  }
  $('detail-appliance-section').hidden = circuit.kind !== 'power';
  const associated = (state.appliances || []).filter(a => a.parent_id === detailId);
  const pending = (state.appliance_config || []).filter(a => !a.parent_id || !a.device_id).length;
  $('detail-appliance-note').textContent = mode === 'demo' ? 'Associazione dimostrativa. Le zone reali dei bagni sono ancora da confermare.' : associated.length ? `${associated.length} apparecchi associati a questa tile.` : `Nessun misuratore di apparecchio associato a questa zona. ${pending} apparecchi da configurare: lavatrice, asciugatrice e i due boiler.`;
  $('detail-appliances').replaceChildren();
  for (const a of associated) {
    const row = document.createElement('div'); row.className = 'appliance-reading';
    const name = document.createElement('div'); const title = document.createElement('strong'); title.textContent = a.label;
    const caption = document.createElement('span'); caption.textContent = a.location; name.append(title, caption);
    const reading = document.createElement('div'); reading.className = 'appliance-value';
    const power = document.createElement('strong'); power.textContent = a.available && !localLost ? number(a.power_w, 1) + ' W' : '— W';
    const energy = document.createElement('span'); energy.textContent = a.available && !localLost && a.energy_wh != null ? number(a.energy_wh / 1000, 3) + ' kWh cumulati' : a.simulated ? 'Simulato · energia non disponibile' : !a.available ? reasonLabels[a.reason] : 'Energia non disponibile';
    reading.append(power, energy); row.append(name, reading); $('detail-appliances').append(row);
  }
}

function openAssignments() {
  if (!state || !state.appliance_config) return;
  assignmentConfig = structuredClone(state.appliance_config); $('assignments-error').textContent = '';
  const mainSources = new Set(state.config.circuits.map(c => c.device_id + '|' + `emeters[${c.channel}]`));
  const options = new Map();
  for (const device of state.devices) for (const source of device.sources || []) {
    const key = device.id + '|' + source.source;
    if (!mainSources.has(key)) options.set(key, `${device.model} …${device.id.slice(-6)} · ${source.source}`);
  }
  for (const a of assignmentConfig) if (a.device_id && a.source) options.set(a.device_id + '|' + a.source, `…${a.device_id.slice(-6)} · ${a.source}`);
  $('assignment-fields').replaceChildren();
  for (const a of assignmentConfig) {
    const row = document.createElement('fieldset'); row.className = 'assignment-row';
    const legend = document.createElement('legend'); legend.textContent = a.label + ' · ' + a.location; row.append(legend);
    const grid = document.createElement('div'); grid.className = 'assignment-grid';
    const zoneLabel = document.createElement('label'); zoneLabel.textContent = 'Zona Forza'; const zone = document.createElement('select'); zone.name = a.id + '-parent';
    const noZone = document.createElement('option'); noZone.value = ''; noZone.textContent = 'Da associare'; zone.append(noZone);
    for (const c of state.config.circuits.filter(c => c.kind === 'power')) { const option = document.createElement('option'); option.value = c.id; option.textContent = c.label + ' · ' + c.zone; zone.append(option); }
    zone.value = a.parent_id || ''; zoneLabel.append(zone);
    const sourceLabel = document.createElement('label'); sourceLabel.textContent = 'Misuratore dedicato'; const source = document.createElement('select'); source.name = a.id + '-meter';
    const noMeter = document.createElement('option'); noMeter.value = ''; noMeter.textContent = 'Da installare / non associato'; source.append(noMeter);
    for (const [value, text] of options) { const option = document.createElement('option'); option.value = value; option.textContent = text; source.append(option); }
    source.value = a.device_id ? a.device_id + '|' + a.source : ''; sourceLabel.append(source);
    grid.append(zoneLabel, sourceLabel); row.append(grid); $('assignment-fields').append(row);
  }
  $('assignments-dialog').showModal();
}

$('close-detail').addEventListener('click', () => $('detail-dialog').close());
$('detail-mute').addEventListener('click', async () => {
  try { await post('/api/mute', { mode }); await poll(); }
  catch (error) { failureMessage = error.message; render(); }
});
$('open-assignments').addEventListener('click', openAssignments); $('settings-appliances').addEventListener('click', openAssignments);
$('close-assignments').addEventListener('click', () => $('assignments-dialog').close()); $('cancel-assignments').addEventListener('click', () => $('assignments-dialog').close());
$('assignments-form').addEventListener('submit', async event => {
  event.preventDefault(); const form = new FormData(event.target), appliances = structuredClone(assignmentConfig), used = new Set();
  for (const a of appliances) {
    a.parent_id = form.get(a.id + '-parent') || null;
    const source = form.get(a.id + '-meter'); [a.device_id, a.source] = source ? source.split('|') : [null, null];
    if (source && !a.parent_id) { $('assignments-error').textContent = 'Scegli la zona Forza per ogni misuratore associato.'; return; }
    if (source && used.has(source)) { $('assignments-error').textContent = 'Ogni apparecchio richiede un canale di misura distinto.'; return; }
    if (source) used.add(source);
  }
  $('save-assignments').disabled = true;
  try { await post('/api/appliances', { appliances }); $('assignments-dialog').close(); await poll(); }
  catch (error) { $('assignments-error').textContent = error.message; }
  finally { $('save-assignments').disabled = false; }
});

async function poll() {
  if (polling) return;
  polling = true;
  const requestedMode = mode;
  try {
    const response = await fetch('/api/state' + (requestedMode ? '?mode=' + requestedMode : ''), { signal: AbortSignal.timeout(6000), cache: 'no-store' });
    if (!response.ok) throw new Error('Il monitor non risponde. Riprovo automaticamente.');
    const next = await response.json();
    if (requestedMode !== mode) return;
    state = next; token = next.session_token; mode = next.mode;
    fetchedAt = performance.now(); failedAt = 0; failureMessage = '';
    render();
  } catch (error) {
    failedAt = performance.now() || 1;
    failureMessage = 'Collegamento al monitor interrotto. Riprovo automaticamente.';
    if (state) render(); else { $('status-title').textContent = 'Monitor non raggiungibile'; $('status-description').textContent = failureMessage; document.body.dataset.level = 'unavailable'; }
  } finally { polling = false; }
}

async function post(path, data) {
  const response = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Monitor-Token': token }, body: JSON.stringify(data), signal: AbortSignal.timeout(6000) });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || 'Operazione non riuscita.');
  return result;
}

async function switchMode(next) {
  if (next === mode) return;
  mode = next; state = null; failedAt = 0; lastAnnouncement = '';
  document.body.dataset.level = 'unavailable';
  $('total').textContent = '—'; $('status-title').textContent = 'Cambio modalità…';
  const url = new URL(location.href); url.searchParams.set('mode', mode); history.replaceState({}, '', url);
  // Let a request for the previous mode finish before requesting the next one.
  if (!polling) await poll();
}

function playTone(test = false) {
  if (!audioEnabled || !audio || audio.state !== 'running') return;
  if (!test && (!state || !state.alarm.active || Math.max(0, state.alarm.muted_seconds - (performance.now() - fetchedAt) / 1000) > 0)) return;
  const start = audio.currentTime;
  for (let i = 0; i < 2; i++) {
    const oscillator = audio.createOscillator(), gain = audio.createGain(), at = start + i * .28;
    oscillator.type = 'sine'; oscillator.frequency.value = test ? 660 : 880;
    gain.gain.setValueAtTime(0, at); gain.gain.linearRampToValueAtTime(.16, at + .015);
    gain.gain.setValueAtTime(.16, at + .12); gain.gain.linearRampToValueAtTime(0, at + .18);
    oscillator.connect(gain); gain.connect(audio.destination); oscillator.start(at); oscillator.stop(at + .2);
    oscillator.onended = () => { oscillator.disconnect(); gain.disconnect(); };
  }
}

$('enable-audio').addEventListener('click', async () => {
  try {
    if (audioEnabled && audio?.state === 'running') { audioEnabled = false; render(); return; }
    const Audio = window.AudioContext || window.webkitAudioContext;
    if (!Audio) throw new Error();
    if (!audio) audio = new Audio();
    await audio.resume(); audioEnabled = audio.state === 'running'; audioIssue = audioEnabled ? '' : 'Il browser non ha attivato l’audio. Premi di nuovo Attiva audio.';
    if (audioEnabled) playTone(true);
  } catch { audioIssue = 'Audio non disponibile in questo browser. L’avviso visivo resta attivo.'; }
  render();
});
$('test-audio').addEventListener('click', () => playTone(true));
$('mute').addEventListener('click', async () => { try { await post('/api/mute', { mode }); await poll(); } catch (error) { failureMessage = error.message; render(); } });
$('mode-cloud').addEventListener('click', () => switchMode('cloud'));
$('mode-demo').addEventListener('click', () => switchMode('demo'));
$('exit-demo').addEventListener('click', () => switchMode('cloud'));
document.querySelectorAll('[data-scenario]').forEach(button => button.addEventListener('click', async () => {
  try { await post('/api/demo', { scenario: button.dataset.scenario }); await poll(); }
  catch (error) { failureMessage = error.message; render(); }
}));
$('fullscreen').addEventListener('click', async () => {
  try { if (document.fullscreenElement) await document.exitFullscreen(); else await document.documentElement.requestFullscreen(); }
  catch { audioIssue = 'Schermo intero non disponibile in questo browser.'; render(); }
});

function openSettings() {
  if (!state) return;
  settingsConfig = structuredClone(state.config);
  for (const [id, key] of [['limit', 'limit_w'], ['warning', 'warning_w'], ['hysteresis', 'hysteresis_w'], ['stale', 'stale_after_s'], ['mute', 'mute_seconds']]) $('setting-' + id).value = settingsConfig[key];
  $('setting-confirmed').checked = settingsConfig.mapping_confirmed;
  $('settings-error').textContent = '';
  const options = new Map();
  for (const device of state.devices) for (const channel of device.channels) options.set(device.id + ':' + channel, `…${device.id.slice(-6)} · canale ${channel}`);
  for (const c of settingsConfig.circuits) options.set(c.device_id + ':' + c.channel, `…${c.device_id.slice(-6)} · canale ${c.channel}`);
  $('mapping-fields').replaceChildren();
  for (const c of settingsConfig.circuits) {
    const row = document.createElement('div'); row.className = 'mapping-row'; row.dataset.id = c.id;
    for (const [field, title] of [['zone', 'Zona'], ['label', 'Nome tile']]) {
      const label = document.createElement('label'); label.textContent = title;
      const input = document.createElement('input'); input.name = c.id + '-' + field; input.value = c[field]; input.required = true; input.maxLength = 32; label.append(input); row.append(label);
    }
    const label = document.createElement('label'); label.textContent = 'Misuratore e canale';
    const select = document.createElement('select'); select.name = c.id + '-source';
    for (const [value, text] of options) { const option = document.createElement('option'); option.value = value; option.textContent = text; select.append(option); }
    select.value = c.device_id + ':' + c.channel; label.append(select); row.append(label); $('mapping-fields').append(row);
  }
  $('devices-list').replaceChildren();
  for (const device of state.devices) { const li = document.createElement('li'); li.textContent = `${device.model} · …${device.id.slice(-6)} · ${device.online ? 'online' : 'offline'}`; $('devices-list').append(li); }
  if (!state.devices.length) { const li = document.createElement('li'); li.textContent = 'In attesa del primo inventario cloud.'; $('devices-list').append(li); }
  explainReset(); $('settings-dialog').showModal();
}

function explainReset() {
  const reset = Number($('setting-limit').value) - Number($('setting-hysteresis').value);
  $('reset-explanation').textContent = `L’allarme rientra a ${number(reset)} W o meno. I dati mancanti non annullano un allarme già attivo.`;
}
$('setting-limit').addEventListener('input', explainReset); $('setting-hysteresis').addEventListener('input', explainReset);
$('open-settings').addEventListener('click', openSettings); $('mapping-note').addEventListener('click', openSettings);
$('close-settings').addEventListener('click', () => $('settings-dialog').close()); $('cancel-settings').addEventListener('click', () => $('settings-dialog').close());
$('settings-form').addEventListener('submit', async event => {
  event.preventDefault(); const config = structuredClone(settingsConfig), form = new FormData(event.target);
  for (const [id, key] of [['limit', 'limit_w'], ['warning', 'warning_w'], ['hysteresis', 'hysteresis_w'], ['stale', 'stale_after_s'], ['mute', 'mute_seconds']]) config[key] = Number($('setting-' + id).value);
  config.mapping_confirmed = $('setting-confirmed').checked;
  for (const c of config.circuits) { c.zone = form.get(c.id + '-zone').trim(); c.label = form.get(c.id + '-label').trim(); const [device, channel] = form.get(c.id + '-source').split(':'); c.device_id = device; c.channel = Number(channel); }
  if (config.warning_w >= config.limit_w) { $('settings-error').textContent = 'Il preavviso deve essere inferiore alla soglia di allarme.'; return; }
  if (new Set(config.circuits.map(c => c.device_id + ':' + c.channel)).size !== 4) { $('settings-error').textContent = 'Scegli quattro canali diversi: un canale non può essere contato due volte.'; return; }
  // A zone is shared by its two tiles.
  if (config.circuits.find(c => c.id === 'luce1').zone !== config.circuits.find(c => c.id === 'forza1').zone || config.circuits.find(c => c.id === 'luce2').zone !== config.circuits.find(c => c.id === 'forza2').zone) { $('settings-error').textContent = 'Usa lo stesso nome di zona per le sue due tile.'; return; }
  $('save-settings').disabled = true;
  try { await post('/api/config', config); $('settings-dialog').close(); await poll(); }
  catch (error) { $('settings-error').textContent = error.message; }
  finally { $('save-settings').disabled = false; }
});

window.addEventListener('online', poll);
document.addEventListener('visibilitychange', () => { if (!document.hidden) poll(); });
setInterval(poll, 1000);
setInterval(() => { render(); }, 1000);
setInterval(() => playTone(false), 3000);
poll();
