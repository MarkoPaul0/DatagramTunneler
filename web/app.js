(() => {
  const api = '/api/v1';
  const state = {
    tunnels: [], runtimes: new Map(), events: [], selectedAlias: null, socket: null, retry: null,
    view: 'operations', configDirty: false, configMode: 'form', configModel: []
  };
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

  function configurationFor(alias) {
    return state.configModel.find((tunnel) => tunnel.alias === alias);
  }

  function routeFor(tunnel) {
    const configuration = configurationFor(tunnel.alias) || {};
    if (tunnel.mode === 'client') {
      return {
        inputLabel: 'UDP IN',
        inputValue: configuration.udp_group || tunnel.udp_destination || 'Not configured',
        outputLabel: 'TCP OUT',
        outputValue: configuration.tcp_server || 'Not configured',
        interfaceValue: configuration.udp_interface || 'Not configured'
      };
    }
    return {
      inputLabel: 'TCP IN',
      inputValue: configuration.tcp_listen_port ? `0.0.0.0:${configuration.tcp_listen_port}` : 'Not configured',
      outputLabel: 'UDP OUT',
      outputValue: configuration.udp_destination || tunnel.udp_destination || 'replicate_client',
      interfaceValue: configuration.udp_interface || 'Not configured'
    };
  }

  function renderTunnelFlow(tunnel, runtime) {
    const route = routeFor(tunnel);
    const isActive = ['running', 'connected'].includes(runtime.state);
    const hasTraffic = isActive && Number(runtime.metrics?.datagram_count || 0) > 0;
    const traffic = hasTraffic ? ' flowing' : '';
    const stateLabel = hasTraffic ? 'recent datagrams observed' : 'waiting for datagrams';
    return `<div class="route-flow${traffic}" aria-label="${escapeAttribute(`${route.inputLabel} ${route.inputValue}, through TCP tunnel, to ${route.outputLabel} ${route.outputValue}`)}">
      <div class="route-endpoint route-input"><span>${route.inputLabel}</span><strong>${escapeHtml(route.inputValue)}</strong></div>
      <div class="route-transit" aria-hidden="true"><i></i><i></i><i></i><span>TCP TUNNEL</span></div>
      <div class="route-endpoint route-output"><span>${route.outputLabel}</span><strong>${escapeHtml(route.outputValue)}</strong></div>
      <p class="route-interface"><span>INTERFACE</span><b>${escapeHtml(route.interfaceValue)}</b><small>${stateLabel}</small></p>
    </div>`;
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
        <div class="tunnel-card-title"><h3>${escapeHtml(tunnel.alias)}</h3><p>${escapeHtml(runtime.detail || 'Not started')}</p></div>
        ${renderTunnelFlow(tunnel, runtime)}
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
      state.configModel = parseTomlConfiguration(response.toml || '');
      renderConfigForm();
    } catch (error) { notify(error.message, true); }
  }

  function tomlValue(value) {
    return `"${String(value).replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
  }

  function tomlString(value) {
    const match = String(value).trim().match(/^"((?:\\.|[^"\\])*)"$/);
    if (!match) throw new Error('Guided editor supports quoted string values only. Use TOML file mode for this configuration.');
    return match[1].replace(/\\([\\"])/g, '$1');
  }

  function parseTomlConfiguration(toml) {
    const tunnels = [];
    let tunnel = null;
    for (const rawLine of String(toml).split(/\r?\n/)) {
      const line = rawLine.trim();
      if (!line || line.startsWith('#') || line === 'version = 1') continue;
      const header = line.match(/^\[tunnels\.([A-Za-z0-9_-]+)\]$/);
      if (header) {
        tunnel = { alias: header[1], mode: '', udp_interface: '', udp_group: '', tcp_server: '', tcp_listen_port: '', udp_destination: '' };
        tunnels.push(tunnel);
        continue;
      }
      const field = line.match(/^([a-z_]+)\s*=\s*(.+)$/);
      if (!field || !tunnel) throw new Error('Guided editor supports the standard named-tunnel TOML format only. Use TOML file mode for this configuration.');
      const [ , name, rawValue ] = field;
      if (!['mode', 'udp_interface', 'udp_group', 'tcp_server', 'tcp_listen_port', 'udp_destination'].includes(name)) {
        throw new Error('Guided editor supports the standard named-tunnel TOML format only. Use TOML file mode for this configuration.');
      }
      tunnel[name] = name === 'tcp_listen_port' ? String(Number(rawValue.trim())) : tomlString(rawValue);
    }
    return tunnels;
  }

  function splitEndpoint(value, allowReplica = false) {
    const endpoint = String(value || '').trim();
    if (allowReplica && endpoint === 'replicate_client') return { address: endpoint, port: '' };
    const match = endpoint.match(/^(.+):(\d+)$/);
    return match ? { address: match[1], port: match[2] } : { address: endpoint, port: '' };
  }

  function joinEndpoint(address, port, allowReplica = false) {
    const host = String(address || '').trim();
    const service = String(port || '').trim();
    if (allowReplica && host === 'replicate_client' && !service) return host;
    return host && service ? `${host}:${service}` : host;
  }

  function collectFormTunnels() {
    return [...document.querySelectorAll('[data-config-tunnel]')].map((card) => {
      const field = (name) => card.querySelector(`[data-config-field="${name}"]`)?.value.trim() || '';
      return {
        alias: field('alias'), mode: field('mode'), udp_interface: field('udp_interface'),
        udp_group: joinEndpoint(field('udp_group_address'), field('udp_group_port')),
        tcp_server: joinEndpoint(field('tcp_server_address'), field('tcp_server_port')),
        tcp_listen_port: field('tcp_listen_port'),
        udp_destination: joinEndpoint(field('udp_destination_address'), field('udp_destination_port'), true)
      };
    });
  }

  function validateFormTunnels(tunnels) {
    const aliases = new Set();
    for (const tunnel of tunnels) {
      if (!/^[A-Za-z0-9_-]+$/.test(tunnel.alias)) throw new Error('Tunnel aliases may contain letters, numbers, hyphens, and underscores only.');
      if (aliases.has(tunnel.alias)) throw new Error(`Tunnel alias '${tunnel.alias}' is duplicated.`);
      aliases.add(tunnel.alias);
      if (!['client', 'server'].includes(tunnel.mode)) throw new Error(`Tunnel '${tunnel.alias}' must be a client or server.`);
      if (!tunnel.udp_interface) throw new Error(`Tunnel '${tunnel.alias}' requires a UDP interface.`);
      if (tunnel.mode === 'client' && (!/^.+:\d+$/.test(tunnel.udp_group) || !/^.+:\d+$/.test(tunnel.tcp_server))) {
        throw new Error(`Client tunnel '${tunnel.alias}' requires a UDP group address and port, plus a TCP server address and port.`);
      }
      if (tunnel.mode === 'server' && !/^\d+$/.test(tunnel.tcp_listen_port)) throw new Error(`Server tunnel '${tunnel.alias}' requires a TCP listen port.`);
      if (tunnel.mode === 'server' && tunnel.udp_destination && tunnel.udp_destination !== 'replicate_client' && !/^.+:\d+$/.test(tunnel.udp_destination)) {
        throw new Error(`Server tunnel '${tunnel.alias}' requires a UDP destination address and port, or replicate_client.`);
      }
    }
  }

  function serializeFormTunnels(tunnels) {
    validateFormTunnels(tunnels);
    const output = ['version = 1'];
    for (const tunnel of tunnels) {
      output.push('', `[tunnels.${tunnel.alias}]`, `mode = ${tomlValue(tunnel.mode)}`, `udp_interface = ${tomlValue(tunnel.udp_interface)}`);
      if (tunnel.mode === 'client') {
        output.push(`udp_group = ${tomlValue(tunnel.udp_group)}`, `tcp_server = ${tomlValue(tunnel.tcp_server)}`);
      } else {
        output.push(`tcp_listen_port = ${tunnel.tcp_listen_port}`);
        if (tunnel.udp_destination) output.push(`udp_destination = ${tomlValue(tunnel.udp_destination)}`);
      }
    }
    return `${output.join('\n')}\n`;
  }

  function tunnelEditor(tunnel, index) {
    const multicastGroup = splitEndpoint(tunnel.udp_group);
    const tcpServer = splitEndpoint(tunnel.tcp_server);
    const udpDestination = splitEndpoint(tunnel.udp_destination, true);
    const modeFields = tunnel.mode === 'server'
      ? `<label>TCP listen port<input data-config-field="tcp_listen_port" type="number" min="1" max="65535" value="${escapeAttribute(tunnel.tcp_listen_port)}" required></label>
         <div class="field-row">
           <label>UDP destination address<input data-config-field="udp_destination_address" value="${escapeAttribute(udpDestination.address)}" placeholder="239.1.2.4 or replicate_client"></label>
           <label>UDP port<input data-config-field="udp_destination_port" type="number" min="1" max="65535" value="${escapeAttribute(udpDestination.port)}" placeholder="5000"></label>
         </div>`
      : `<div class="field-row">
           <label>UDP multicast address<input data-config-field="udp_group_address" value="${escapeAttribute(multicastGroup.address)}" placeholder="239.1.2.3" required></label>
           <label>UDP port<input data-config-field="udp_group_port" type="number" min="1" max="65535" value="${escapeAttribute(multicastGroup.port)}" placeholder="5000" required></label>
         </div>
         <div class="field-row">
           <label>TCP server address<input data-config-field="tcp_server_address" value="${escapeAttribute(tcpServer.address)}" placeholder="192.168.1.10" required></label>
           <label>TCP port<input data-config-field="tcp_server_port" type="number" min="1" max="65535" value="${escapeAttribute(tcpServer.port)}" placeholder="14052" required></label>
         </div>`;
    return `<article class="tunnel-editor" data-config-tunnel="${index}">
      <div class="tunnel-editor-heading"><span class="panel-number">${tunnel.mode === 'server' ? 'EGRESS' : 'INGRESS'}</span><button class="text-button delete-tunnel" data-delete-tunnel="${index}" type="button">Delete</button></div>
      <div class="editor-fields editor-fields-top">
        <label>Alias<input data-config-field="alias" value="${escapeAttribute(tunnel.alias)}" required></label>
        <label>Mode<select data-config-field="mode"><option value="client"${tunnel.mode === 'client' ? ' selected' : ''}>Client / ingress</option><option value="server"${tunnel.mode === 'server' ? ' selected' : ''}>Server / egress</option></select></label>
        <label>UDP interface<input data-config-field="udp_interface" value="${escapeAttribute(tunnel.udp_interface)}" placeholder="192.168.1.20 or en0" required></label>
      </div>
      <div class="editor-fields">${modeFields}</div>
    </article>`;
  }

  function renderConfigForm() {
    const list = $('#tunnel-editor-list');
    list.innerHTML = state.configModel.length
      ? state.configModel.map(tunnelEditor).join('')
      : '<div class="empty-state">No named tunnels yet. Add a client or server route to begin.</div>';
  }

  function setConfigMode(mode) {
    const nextMode = mode === 'toml' ? 'toml' : 'form';
    if (nextMode === 'toml') {
      try {
        state.configModel = collectFormTunnels();
        $('#config-editor').value = serializeFormTunnels(state.configModel);
      } catch (error) { notify(error.message, true); return; }
    } else {
      try {
        state.configModel = parseTomlConfiguration($('#config-editor').value);
        renderConfigForm();
      } catch (error) { notify(error.message, true); return; }
    }
    state.configMode = nextMode;
    $('#config-form-view').hidden = nextMode !== 'form';
    $('#config-toml-view').hidden = nextMode !== 'toml';
    document.querySelectorAll('.config-mode-tab').forEach((tab) => {
      const active = tab.dataset.configMode === nextMode;
      tab.classList.toggle('active', active);
      tab.setAttribute('aria-selected', String(active));
    });
  }

  async function saveConfiguration(toml) {
    await request('/config', { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ toml }) });
    $('#config-editor').value = toml;
    state.configModel = parseTomlConfiguration(toml);
    state.configDirty = false;
    notify('Configuration saved');
    await refresh();
  }

  async function refresh() {
    try {
      const [tunnelResponse, runtimeResponse, configurationResponse] = await Promise.all([
        request('/tunnels'), request('/runtimes'), request('/config')
      ]);
      state.tunnels = tunnelResponse.tunnels || [];
      state.runtimes = new Map((runtimeResponse.runtimes || []).map((runtime) => [`${runtime.kind}:${runtime.alias}`, runtime]));
      if (!state.configDirty) state.configModel = parseTomlConfiguration(configurationResponse.toml || '');
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
    try { await saveConfiguration($('#config-editor').value); }
    catch (error) { notify(error.message, true); }
  });
  $('#config-editor').addEventListener('input', () => { state.configDirty = true; });
  document.querySelectorAll('.config-mode-tab').forEach((tab) => tab.addEventListener('click', () => setConfigMode(tab.dataset.configMode)));
  $('#add-tunnel').addEventListener('click', () => {
    state.configModel = collectFormTunnels();
    state.configModel.push({ alias: 'new-tunnel', mode: 'client', udp_interface: '', udp_group: '', tcp_server: '', tcp_listen_port: '', udp_destination: '' });
    state.configDirty = true;
    renderConfigForm();
  });
  $('#tunnel-editor-list').addEventListener('input', (event) => {
    if (event.target.matches('[data-config-field]')) state.configDirty = true;
  });
  $('#tunnel-editor-list').addEventListener('change', (event) => {
    if (!event.target.matches('[data-config-field="mode"]')) return;
    state.configModel = collectFormTunnels();
    const card = event.target.closest('[data-config-tunnel]');
    state.configModel[Number(card.dataset.configTunnel)].mode = event.target.value;
    state.configDirty = true;
    renderConfigForm();
  });
  $('#tunnel-editor-list').addEventListener('click', (event) => {
    const button = event.target.closest('[data-delete-tunnel]');
    if (!button) return;
    state.configModel = collectFormTunnels();
    state.configModel.splice(Number(button.dataset.deleteTunnel), 1);
    state.configDirty = true;
    renderConfigForm();
  });
  $('#save-form-config').addEventListener('click', async () => {
    try {
      state.configModel = collectFormTunnels();
      await saveConfiguration(serializeFormTunnels(state.configModel));
      renderConfigForm();
    } catch (error) { notify(error.message, true); }
  });

  refresh();
  setView(location.hash === '#configuration' ? 'configuration' : 'operations', false);
  connectEvents();
  window.setInterval(() => {
    if (document.visibilityState === 'visible' && state.socket?.readyState !== WebSocket.OPEN) refresh();
  }, 2000);
})();
