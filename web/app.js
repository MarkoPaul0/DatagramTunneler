(() => {
  const api = '/api/v1';
  const state = { tunnels: [], runtimes: new Map(), events: [], selectedAlias: null, socket: null, retry: null, view: 'operations', configDirty: false };
  const $ = (selector) => document.querySelector(selector);
  const tunnelGrid = $('#tunnel-grid');
  const eventList = $('#event-list');
  const toast = $('#toast');
  const connection = $('#connection-state');
  const detailPanel = $('#tunnel-detail');

  function setView(view, updateLocation = true) {
    state.view = view === 'configuration' ? 'configuration' : 'operations';
    $('#operations-view').hidden = state.view !== 'operations';
    $('#configuration-view').hidden = state.view !== 'configuration';
    document.querySelectorAll('.view-tab').forEach((tab) => {
      const active = tab.dataset.view === state.view;
      tab.classList.toggle('active', active);
      tab.setAttribute('aria-selected', String(active));
    });
    if (state.view === 'configuration') loadConfiguration();
    if (updateLocation) history.replaceState(null, '', state.view === 'configuration' ? '#configuration' : '#operations');
  }

  function notify(message, error = false) {
    toast.textContent = message;
    toast.classList.toggle('error', error);
    toast.classList.add('visible');
    window.clearTimeout(notify.timer);
    notify.timer = window.setTimeout(() => toast.classList.remove('visible'), 3300);
  }

  async function request(path, options = {}) {
    const response = await fetch(`${api}${path}`, options);
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(payload.error?.message || `Request failed (${response.status})`);
    return payload;
  }

  function runtimeFor(alias, kind = 'tunnel') {
    return state.runtimes.get(`${kind}:${alias}`) || { state: 'stopped', detail: 'Not started' };
  }

  function tunnelRole(mode) {
    return mode === 'client'
      ? { name: 'Ingress', detail: 'TCP client', className: 'ingress' }
      : { name: 'Egress', detail: 'TCP server', className: 'egress' };
  }

  function tunnelFor(alias) {
    return state.tunnels.find((tunnel) => tunnel.alias === alias);
  }

  function formatBytes(value) {
    if (!value) return '0 B';
    if (value < 1024) return `${value} B`;
    if (value < 1024 * 1024) return `${(value / 1024).toFixed(1)} KiB`;
    return `${(value / (1024 * 1024)).toFixed(1)} MiB`;
  }

  function formatRate(value) { return value ? `${formatBytes(value)}/s` : '0 B/s'; }

  function metric(value, suffix = '') { return value == null ? 'Unavailable' : `${Number(value).toFixed(2)}${suffix}`; }

  function formatTimestamp(milliseconds) {
    return new Date(milliseconds || Date.now()).toLocaleTimeString([], {
      hour: '2-digit', minute: '2-digit', second: '2-digit', fractionalSecondDigits: 3
    });
  }

  function renderTunnels() {
    if (!state.tunnels.length) {
      tunnelGrid.innerHTML = '<div class="empty-state">No named tunnels are configured.</div>';
      return;
    }
    tunnelGrid.innerHTML = state.tunnels.map((tunnel) => {
      const runtime = runtimeFor(tunnel.alias);
      const role = tunnelRole(tunnel.mode);
      const selected = state.selectedAlias === tunnel.alias ? 'selected' : '';
      return `<article class="tunnel-card ${escapeHtml(runtime.state)} ${selected}" data-tunnel-card="${escapeAttribute(tunnel.alias)}" tabindex="0" aria-label="Inspect ${escapeHtml(tunnel.alias)}">
        <div class="tunnel-top"><span class="mode-pill ${role.className}">${role.name} <small>· ${role.detail}</small></span><span class="runtime-state ${escapeHtml(runtime.state)}">${escapeHtml(runtime.state)}</span></div>
        <h3>${escapeHtml(tunnel.alias)}</h3>
        <p class="destination">UDP DESTINATION<br><b>${escapeHtml(tunnel.udp_destination)}</b></p>
        <p class="destination">${escapeHtml(runtime.detail || 'Not started')}</p>
        <button class="inspect-button" data-inspect="${escapeAttribute(tunnel.alias)}" type="button">View stats &amp; events <span>→</span></button>
        <div class="action-row" aria-label="${escapeHtml(tunnel.alias)} controls">
          <button data-tunnel="${escapeAttribute(tunnel.alias)}" data-action="start" type="button">Start</button>
          <button data-tunnel="${escapeAttribute(tunnel.alias)}" data-action="restart" type="button">Restart</button>
          <button data-tunnel="${escapeAttribute(tunnel.alias)}" data-action="stop" type="button">Stop</button>
        </div>
      </article>`;
    }).join('');
    renderTunnelDetail();
  }

  function renderProducerAliases() {
    const selected = $('#producer-alias').value;
    const clients = state.tunnels.filter((tunnel) => tunnel.mode === 'client');
    $('#producer-alias').innerHTML = clients.map((tunnel) => `<option value="${escapeAttribute(tunnel.alias)}">${escapeHtml(tunnel.alias)} · ${escapeHtml(tunnel.udp_destination)}</option>`).join('');
    if (clients.some((tunnel) => tunnel.alias === selected)) $('#producer-alias').value = selected;
  }

  function addEvent(event) {
    state.events.unshift(event);
    state.events = state.events.slice(0, 40);
    eventList.innerHTML = state.events.map((item) => {
      const time = formatTimestamp(item.timestamp_unix_milliseconds);
      const kind = item.snapshot?.kind || 'service';
      const definition = kind === 'tunnel' ? tunnelFor(item.alias) : null;
      const role = definition ? tunnelRole(definition.mode) : null;
      const label = kind === 'producer' ? 'Producer' : role?.name || 'Service';
      const detail = kind === 'producer' ? 'test signal' : role?.detail || '';
      const className = kind === 'producer' ? 'producer' : role?.className || 'service';
      return `<li class="${escapeHtml(item.severity || 'info')}"><time>${time}</time><p><span class="event-kind ${escapeHtml(className)}">${label}${detail ? `<small>${detail}</small>` : ''}</span><b>${escapeHtml(item.alias || 'service')}</b> ${escapeHtml(item.message || 'updated')}</p></li>`;
    }).join('');
  }

  function renderTunnelDetail() {
    const tunnel = tunnelFor(state.selectedAlias);
    if (!tunnel) {
      detailPanel.hidden = true;
      return;
    }
    const runtime = runtimeFor(tunnel.alias);
    const role = tunnelRole(tunnel.mode);
    const metrics = runtime.metrics || {};
    const averageSize = metrics.datagram_count ? metrics.byte_count / metrics.datagram_count : null;
    detailPanel.hidden = false;
    $('#detail-heading').textContent = tunnel.alias;
    $('#detail-summary').innerHTML = `<span class="mode-pill ${role.className}">${role.name} <small>· ${role.detail}</small></span><span class="runtime-state ${escapeHtml(runtime.state)}">${escapeHtml(runtime.state)}</span><span>${escapeHtml(tunnel.udp_destination)}</span>`;
    $('#metrics-grid').innerHTML = [
      ['Datagrams', metrics.datagram_count ?? 0],
      ['Transferred', formatBytes(metrics.byte_count ?? 0)],
      ['Average size', averageSize == null ? 'Unavailable' : `${averageSize.toFixed(1)} B`],
      ['Throughput', formatRate(metrics.throughput_bytes_per_second ?? 0)],
      ['Latency avg', metric(metrics.average_latency_milliseconds, ' ms')],
      ['Latency p99', metric(metrics.p99_latency_milliseconds, ' ms')]
    ].map(([label, value]) => `<div class="metric"><span>${label}</span><strong>${escapeHtml(value)}</strong></div>`).join('');
    const events = state.events.filter((event) => event.alias === tunnel.alias && event.snapshot?.kind === 'tunnel');
    $('#detail-events').innerHTML = events.length
      ? events.map((event) => `<li><time>${formatTimestamp(event.timestamp_unix_milliseconds)}</time><span>${escapeHtml(event.message || 'updated')}</span></li>`).join('')
      : '<li class="detail-empty">No control events recorded for this tunnel in this browser session.</li>';
    const datagrams = [...(runtime.recent_datagrams || [])].reverse();
    const action = tunnel.mode === 'client' ? 'Forwarded' : 'Published';
    $('#detail-datagrams').innerHTML = datagrams.length
      ? datagrams.map((datagram) => {
          const timestamp = formatTimestamp(datagram.timestamp_unix_milliseconds);
          const latency = datagram.latency_milliseconds == null ? 'Latency unavailable' : `${Number(datagram.latency_milliseconds).toFixed(3)} ms`;
          return `<li><time>${timestamp}</time><span><b>${action}</b> ${escapeHtml(formatBytes(datagram.bytes))}<small>${escapeHtml(latency)}</small></span></li>`;
        }).join('')
      : '<li class="detail-empty">No datagrams observed for this tunnel yet.</li>';
  }

  function selectTunnel(alias) {
    state.selectedAlias = alias;
    renderTunnels();
    detailPanel.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
  }

  function eventFromWire(message) {
    const event = message.event;
    if (!event) return;
    if (event.snapshot) state.runtimes.set(`${event.snapshot.kind}:${event.snapshot.alias}`, event.snapshot);
    if (event.kind !== 'metrics') addEvent(event);
    renderTunnels();
  }

  function setConnection(kind, text) {
    connection.className = `connection-state ${kind}`;
    connection.lastElementChild.textContent = text;
  }

  function connectEvents() {
    window.clearTimeout(state.retry);
    const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
    setConnection('', 'CONNECTING');
    const socket = new WebSocket(`${scheme}://${location.host}${api}/events`);
    state.socket = socket;
    socket.onopen = () => setConnection('connected', 'LIVE');
    socket.onmessage = (message) => { try { eventFromWire(JSON.parse(message.data)); } catch { /* Ignore malformed telemetry. */ } };
    socket.onclose = () => {
      if (state.socket === socket) {
        setConnection('disconnected', 'RECONNECTING');
        state.retry = window.setTimeout(connectEvents, 2500);
      }
    };
    socket.onerror = () => socket.close();
  }

  async function loadConfiguration() {
    if (state.configDirty) return;
    try {
      const response = await request('/config');
      $('#config-editor').value = response.toml || '';
    } catch (error) { notify(error.message, true); }
  }

  async function refresh() {
    try {
      const [tunnelResponse, runtimeResponse] = await Promise.all([
        request('/tunnels'), request('/runtimes')
      ]);
      state.tunnels = tunnelResponse.tunnels || [];
      state.runtimes = new Map((runtimeResponse.runtimes || []).map((runtime) => [`${runtime.kind}:${runtime.alias}`, runtime]));
      renderTunnels(); renderProducerAliases(); renderTunnelDetail();
    } catch (error) { notify(error.message, true); }
  }

  async function action(alias, action) {
    try {
      await request(`/tunnels/${encodeURIComponent(alias)}/${action}`, { method: 'POST' });
      notify(`${alias}: ${action} requested`);
      window.setTimeout(refresh, 120);
    } catch (error) { notify(error.message, true); }
  }

  function escapeHtml(value) { return String(value).replace(/[&<>"']/g, (character) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[character])); }
  function escapeAttribute(value) { return escapeHtml(value); }

  tunnelGrid.addEventListener('click', (event) => {
    const inspect = event.target.closest('button[data-inspect]');
    if (inspect) { selectTunnel(inspect.dataset.inspect); return; }
    const button = event.target.closest('button[data-tunnel]');
    if (button) { action(button.dataset.tunnel, button.dataset.action); return; }
    const card = event.target.closest('[data-tunnel-card]');
    if (card) selectTunnel(card.dataset.tunnelCard);
  });
  tunnelGrid.addEventListener('keydown', (event) => {
    if ((event.key === 'Enter' || event.key === ' ') && event.target.matches('[data-tunnel-card]')) {
      event.preventDefault();
      selectTunnel(event.target.dataset.tunnelCard);
    }
  });
  $('#close-detail').addEventListener('click', () => { state.selectedAlias = null; renderTunnels(); });
  document.querySelectorAll('.view-tab').forEach((tab) => tab.addEventListener('click', () => setView(tab.dataset.view)));
  $('#refresh-button').addEventListener('click', refresh);
  $('#clear-events').addEventListener('click', () => { state.events = []; eventList.innerHTML = '<li class="event-empty">Waiting for lifecycle events…</li>'; });
  $('#producer-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    const alias = $('#producer-alias').value;
    const body = { interval_milliseconds: Number($('#producer-interval').value), payload_prefix: $('#producer-prefix').value };
    if ($('#producer-count').value) body.count = Number($('#producer-count').value);
    try { await request(`/tunnels/${encodeURIComponent(alias)}/producer/start`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }); notify(`${alias}: producer started`); window.setTimeout(refresh, 120); }
    catch (error) { notify(error.message, true); }
  });
  $('#producer-stop').addEventListener('click', async () => {
    const alias = $('#producer-alias').value;
    try { await request(`/tunnels/${encodeURIComponent(alias)}/producer/stop`, { method: 'POST' }); notify(`${alias}: producer stop requested`); window.setTimeout(refresh, 120); }
    catch (error) { notify(error.message, true); }
  });
  $('#save-config').addEventListener('click', async () => {
    try { await request('/config', { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ toml: $('#config-editor').value }) }); state.configDirty = false; notify('Configuration saved'); await refresh(); }
    catch (error) { notify(error.message, true); }
  });
  $('#config-editor').addEventListener('input', () => { state.configDirty = true; });

  refresh();
  setView(location.hash === '#configuration' ? 'configuration' : 'operations', false);
  connectEvents();
  window.setInterval(() => {
    if (document.visibilityState === 'visible' && state.socket?.readyState !== WebSocket.OPEN) refresh();
  }, 2000);
})();
