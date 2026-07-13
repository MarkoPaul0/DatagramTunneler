# Practical use cases

These examples relay a multicast group from a **source network** to a
**destination network**. The tunnel is one-way: client-side UDP multicast goes
to the server over TCP, then the server republishes it on its local network.

Replace every example IP address and multicast group with values appropriate
for your network. Do not expose the TCP listener directly to an untrusted
network: DatagramTunneler does not encrypt or authenticate that connection.
Use a trusted private network or secure overlay such as WireGuard or SSH.

## Shared example topology

| Role | Address | Purpose |
| --- | --- | --- |
| Client host | `192.168.10.20` | Can join source group `239.1.2.3:5000` |
| Server host | `192.168.20.20` | Listens on TCP port `14052` |
| Destination group | `239.1.2.4:5000` | Receives the republished datagrams |

The server host must be reachable from the client host on TCP port `14052`.
The `-i` option is the local interface address on each host—not the multicast
address.

## 1. Relay multicast between two networks

Start the server on the destination network:

```sh
dgramtunneler --server \
  -i 192.168.20.20 \
  -t 14052 \
  -u 239.1.2.4:5000
```

Start the client on the source network:

```sh
dgramtunneler --client \
  -i 192.168.10.20 \
  -t 192.168.20.20:14052 \
  -u 239.1.2.3:5000
```

Send a test datagram from the source network:

```sh
printf 'hello multicast\n' | nc -u -w 1 239.1.2.3 5000
```

When the source group is defined by a named client tunnel, the built-in
producer removes the need for `nc`:

```sh
dgramtunneler producer <client-alias> --count 5
```

It sends `Dummy datagram #1` through `Dummy datagram #5` to that alias's
configured source multicast group. Add `--interval-ms 250` or
`--payload-prefix "Telemetry test"` to adjust the test stream.

On a destination-network host, join `239.1.2.4:5000` with the receiving
application or packet-capture tool. It should receive `hello multicast`.

For a lightweight receiver on a host with Python 3, save the following as
`receive_multicast.py`, then run `python3 receive_multicast.py`:

```python
import socket
import struct

group = "239.1.2.4"
port = 5000

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("", port))
sock.setsockopt(
    socket.IPPROTO_IP,
    socket.IP_ADD_MEMBERSHIP,
    struct.pack("=4s4s", socket.inet_aton(group), socket.inet_aton("0.0.0.0")),
)

while True:
    payload, sender = sock.recvfrom(65535)
    print(f"{sender}: {payload!r}")
```

## 2. Relay a telemetry feed to a remote test or monitoring network

For a long-running telemetry feed, use named tunnels so the topology can be
validated and started by alias. Create the config file on each host with
`dgramtunneler config init`, then add the applicable entry below.

On the source-network client:

```toml
[tunnels.telemetry-source]
mode = "client"
udp_interface = "192.168.10.20"
udp_group = "239.1.2.3:5000"
tcp_server = "192.168.20.20:14052"
```

On the destination-network server:

```toml
[tunnels.telemetry-destination]
mode = "server"
udp_interface = "192.168.20.20"
tcp_listen_port = 14052
udp_destination = "239.1.2.4:5000"
```

Validate both entries before starting them. Start the destination server first,
then the source client:

```sh
dgramtunneler tunnel validate telemetry-destination
dgramtunneler tunnel run telemetry-destination
```

```sh
dgramtunneler tunnel validate telemetry-source
dgramtunneler tunnel run telemetry-source
```

In another terminal on the source network, generate a finite test feed:

```sh
dgramtunneler producer telemetry-source --count 10
```

This pattern is suited to telemetry, simulation output, and observability
feeds where a remote network needs a copy of a multicast stream. TCP can add
latency under packet loss, so it is not suitable for latency-critical control
or real-time gameplay traffic.

## 3. Test one-way LAN discovery from a remote network

Some applications announce their presence on a multicast group. To see those
announcements on a separate test subnet, use the same server/client commands
from [Relay multicast between two networks](#1-relay-multicast-between-two-networks),
with the application's discovery group as `239.1.2.3:5000` and an unused
destination group on the test subnet as `239.1.2.4:5000`.

First capture or receive the destination group to confirm the announcements
arrive. Then point the test application at the destination group if it supports
configuration, or use a test environment that listens to that group.

This is appropriate for one-way discovery announcements. Protocols that expect
the discovering client to send a reply, such as request/response service
discovery, need a separate return path and may not be a good fit.

## Troubleshooting

- Confirm the server is listening before starting the client.
- Confirm that TCP port `14052` is reachable from the client to the server.
- Verify that the client interface can join the source multicast group and that
  datagrams are actually present there.
- Verify that the destination application joins the destination group, not the
  original source group.
- Use `dgramtunneler tunnel validate <alias>` for named configurations before
  running them.
