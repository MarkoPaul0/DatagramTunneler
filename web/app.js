(() => {
  const api = '/api/v1';
  const state = { tunnels: [], runtimes: new Map(), events: [], socket: null, retry: null };
  const $ = (selector) => document.querySelector(selector);
  const tunnelGrid = $('#tunnel-grid');
  const eventList = $('#event-list');
  const toast = $('#toast');
  const connection = $('#connection-state');

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

  function renderTunnels() {
    if (!state.tunnels.length) {
      tunnelGrid.innerHTML = '<div class="empty-state">No named tunnels are configured.</div>';
      return;
    }
    tunnelGrid.innerHTML = state.tunnels.map((tunnel) => {
      const runtime = runtimeFor(tunnel.alias);
      return `<article class="tunnel-card">
        <div class="tunnel-top"><span class="mode-pill">${escapeHtml(tunnel.mode)}</span><span class="runtime-state ${escapeHtml(runtime.state)}">${escapeHtml(runtime.state)}</span></div>
        <h3>${escapeHtml(tunnel.alias)}</h3>
        <p class="destination">UDP DESTINATION<br><b>${escapeHtml(tunnel.udp_destination)}</b></p>
        <p class="destination">${escapeHtml(runtime.detail || 'Not started')}</p>
        <div class="action-row" aria-label="${escapeHtml(tunnel.alias)} controls">
          <button data-tunnel="${escapeAttribute(tunnel.alias)}" data-action="start" type="button">Start</button>
          <button data-tunnel="${escapeAttribute(tunnel.alias)}" data-action="restart" type="button">Restart</button>
          <button data-tunnel="${escapeAttribute(tunnel.alias)}" data-action="stop" type="button">Stop</button>
        </div>
      </article>`;
    }).join('');
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
      const time = new Date(item.timestamp_unix_milliseconds || Date.now()).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
      return `<li class="${escapeHtml(item.severity || 'info')}"><time>${time}</time><p><b>${escapeHtml(item.alias || 'service')}</b> ${escapeHtml(item.message || 'updated')}</p></li>`;
    }).join('');
  }

  function eventFromWire(message) {
    const event = message.event;
    if (!event) return;
    if (event.snapshot) state.runtimes.set(`${event.snapshot.kind}:${event.snapshot.alias}`, event.snapshot);
    addEvent(event);
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

  async function refresh() {
    try {
      const [tunnelResponse, runtimeResponse, configResponse] = await Promise.all([
        request('/tunnels'), request('/runtimes'), request('/config')
      ]);
      state.tunnels = tunnelResponse.tunnels || [];
      state.runtimes = new Map((runtimeResponse.runtimes || []).map((runtime) => [`${runtime.kind}:${runtime.alias}`, runtime]));
      $('#config-editor').value = configResponse.toml || '';
      renderTunnels(); renderProducerAliases();
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
    const button = event.target.closest('button[data-tunnel]');
    if (button) action(button.dataset.tunnel, button.dataset.action);
  });
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
    try { await request('/config', { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ toml: $('#config-editor').value }) }); notify('Configuration saved'); await refresh(); }
    catch (error) { notify(error.message, true); }
  });

  refresh();
  connectEvents();
  window.setInterval(() => { if (document.visibilityState === 'visible') refresh(); }, 2000);
})();
