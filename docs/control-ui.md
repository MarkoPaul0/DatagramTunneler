# Local control dashboard

The local control dashboard is the supported browser interface for operating
named DatagramTunneler tunnels on the current machine. It is served with the
same localhost-only service as the integration API.

```sh
dgramtunneler control serve --port 8765
open http://127.0.0.1:8765
```

## What it provides

- A tunnel deck showing each configured client and server route, its direction,
  and its current state.
- Start, stop, and restart controls for named tunnels.
- A dummy-producer form for any client tunnel.
- A selected-tunnel view with current throughput and latency statistics, recent
  control events, and the last ten datagram observations.
- A guided editor for adding, editing, and deleting client and server tunnels,
  alongside a direct TOML editor for file-oriented changes.

The `udp_interface` field accepts either a local IPv4 address or an interface
name, such as `en0`, `eth0`, or `Ethernet`.

Datagram observations contain a timestamp, byte count, and latency when a
server can calculate it. Payload contents are never stored or displayed.

## Local-only security boundary

The dashboard binds to `127.0.0.1` and has no authentication. Treat anyone
with access to your local machine as able to operate its tunnels. Do not expose
the dashboard through a reverse proxy, SSH port-forward, or network-facing
listener.

For programmatic integrations, use the [OpenAPI definition](openapi.yaml) and
the [control API reference](control-api.md).
