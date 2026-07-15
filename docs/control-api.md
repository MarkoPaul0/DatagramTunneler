# Local control API

This document defines the API implemented by the local control server. It is not a remote-management feature.

Start it with the same named-tunnel configuration used by the CLI:

```sh
dgramtunneler control serve --port 8765
```

## Transport and security boundary

- Bind only to `127.0.0.1` by default; IPv6 loopback support may be added alongside it.
- Do not accept a non-loopback bind address in this API version.
- A future packaged UI will use the same local origin.
- Use JSON request and response bodies, with UTF-8 encoding.
- Prefix every endpoint and WebSocket message with API version `v1`.
- Remote exposure, authentication, origin policy, and CSRF controls are deliberately deferred to Task 33.

## REST resources

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/v1/health` | Read service readiness and API version. |
| `GET` | `/api/v1/tunnels` | List saved tunnel aliases and equivalent direct commands. |
| `GET` | `/api/v1/tunnels/{alias}` | Read one saved tunnel definition. |
| `POST` | `/api/v1/tunnels/{alias}/start` | Start a named tunnel. |
| `POST` | `/api/v1/tunnels/{alias}/stop` | Request graceful stop for a named tunnel. |
| `POST` | `/api/v1/tunnels/{alias}/restart` | Stop, wait for, and restart a named tunnel. |
| `POST` | `/api/v1/tunnels/{alias}/producer/start` | Start a dummy producer using a client tunnel. |
| `POST` | `/api/v1/tunnels/{alias}/producer/stop` | Request graceful stop for that producer. |
| `POST` | `/api/v1/tunnels/{alias}/producer/restart` | Restart a producer with supplied options. |
| `GET` | `/api/v1/runtimes` | List live tunnel and producer snapshots. |
| `GET` | `/api/v1/config` | Read the active TOML configuration. |
| `PUT` | `/api/v1/config` | Validate and replace the active TOML configuration. |

State-changing calls return `202 Accepted` with `{ "accepted": true }`. `POST` action bodies are empty unless stated otherwise.

### Producer request

`POST /api/v1/tunnels/{alias}/producer/start` accepts:

```json
{
  "interval_milliseconds": 1000,
  "count": 10,
  "payload_prefix": "Dummy datagram"
}
```

Omit `count` to produce until stopped. `interval_milliseconds` must be positive.

### Example runtime response

```json
{
  "alias": "example-client",
  "kind": "tunnel",
  "state": "running",
  "detail": "Running",
  "metrics": {
    "datagram_count": 18424,
    "byte_count": 342091,
    "throughput_bytes_per_second": 43827.1,
    "average_latency_milliseconds": 0.21,
    "p50_latency_milliseconds": 0.20,
    "p99_latency_milliseconds": 0.62,
    "maximum_latency_milliseconds": 0.71
  }
}
```

Metrics that are not available, including latency for a producer or unsynchronised peer clocks, are `null`.

## WebSocket events

`GET /api/v1/events` upgrades to a WebSocket. The server emits event messages only; the client does not issue lifecycle commands over the socket.

```json
{
  "api_version": "v1",
  "event": {
    "kind": "lifecycle",
    "severity": "info",
    "timestamp_unix_milliseconds": 1784125687123,
    "alias": "example-client",
    "message": "Running",
    "snapshot": { "alias": "example-client", "kind": "tunnel", "state": "running" }
  }
}
```

Event kinds are `lifecycle`, `log`, and `metrics`. A new connection receives a complete runtime snapshot before subsequent events, so the UI can recover after reconnecting.

## Errors

Every non-success response uses this shape:

```json
{ "error": { "code": "not_found", "message": "Tunnel 'missing-link' does not exist" } }
```

| HTTP status | Error code | Meaning |
| --- | --- | --- |
| `400` | `invalid_request` | Malformed JSON or unsupported field/value. |
| `404` | `not_found` | Alias or route does not exist. |
| `409` | `conflict` | Requested runtime is already active or cannot transition. |
| `422` | `validation_failed` | TOML or producer options are syntactically valid but invalid for DatagramTunneler. |
| `500` | `internal` | Unexpected server failure; no internal details are exposed. |

## Compatibility

`v1` is additive within a major application release: optional response fields may be added, but existing fields retain their meaning. Breaking API changes require a new path version such as `/api/v2`.
