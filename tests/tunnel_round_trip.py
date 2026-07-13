#!/usr/bin/env python3
"""Exercise the client-to-server multicast tunnel on the loopback interface."""

import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


LOOPBACK = "127.0.0.1"
INPUT_GROUP = "239.1.2.3"
OUTPUT_GROUP = "239.1.2.4"
PAYLOAD = b"DatagramTunneler integration test"


def free_port(sock_type):
    with socket.socket(socket.AF_INET, sock_type) as sock:
        sock.bind((LOOPBACK, 0))
        return sock.getsockname()[1]


def multicast_receiver(group, port):
    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    receiver.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    receiver.bind(("", port))
    membership = socket.inet_aton(group) + socket.inet_aton(LOOPBACK)
    receiver.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
    receiver.settimeout(0.2)
    return receiver


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)


def run_replicate_client_loopback(executable):
    tcp_port = free_port(socket.SOCK_STREAM)
    multicast_port = free_port(socket.SOCK_DGRAM)
    server = None
    client = None
    config_path = None

    try:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".toml", delete=False) as config:
            config_path = Path(config.name)
            config.write("""version = 1

[tunnels.client]
mode = "client"
udp_interface = "127.0.0.1"
udp_group = "239.1.2.3:{multicast_port}"
tcp_server = "127.0.0.1:{tcp_port}"

[tunnels.server]
mode = "server"
udp_interface = "127.0.0.1"
tcp_listen_port = {tcp_port}
udp_destination = "replicate_client"
""".format(multicast_port=multicast_port, tcp_port=tcp_port))

        server = subprocess.Popen(
            [executable, "tunnel", "run", "server", "--config", str(config_path)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.25)
        if server.poll() is not None:
            raise RuntimeError("replica server exited before accepting a client")
        client = subprocess.Popen(
            [executable, "tunnel", "run", "client", "--config", str(config_path)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.25)
        producer = subprocess.run(
            [executable, "producer", "client", "--config", str(config_path), "--count", "1"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=5,
            check=False,
        )
        if producer.returncode != 0:
            raise RuntimeError("replica producer failed")

        # Without disabled server multicast loopback, the client re-tunnels the
        # server's publication until the server exhausts its send buffer.
        time.sleep(1)
        if server.poll() is not None or client.poll() is not None:
            raise RuntimeError("replica tunnel entered a multicast feedback loop")
    finally:
        if client is not None:
            stop(client)
        if server is not None:
            stop(server)
        if config_path is not None:
            config_path.unlink(missing_ok=True)


def main():
    if len(sys.argv) not in (2, 3):
        raise RuntimeError("expected the dgramtunneler executable path and optional replica mode")

    executable = sys.argv[1]
    if len(sys.argv) == 3:
        if sys.argv[2] != "--replicate-client":
            raise RuntimeError("unknown test mode")
        run_replicate_client_loopback(executable)
        return 0
    tcp_port = free_port(socket.SOCK_STREAM)
    input_port = free_port(socket.SOCK_DGRAM)
    output_port = free_port(socket.SOCK_DGRAM)
    receiver = multicast_receiver(OUTPUT_GROUP, output_port)
    server = None
    client = None

    try:
        server = subprocess.Popen([
            executable, "--server", "-i", LOOPBACK, "-t", str(tcp_port),
            "-u", "{}:{}".format(OUTPUT_GROUP, output_port),
        ])
        # The client exits immediately when a TCP connection is refused, so give
        # the server process time to bind and begin listening before launching it.
        time.sleep(0.25)
        if server.poll() is not None:
            raise RuntimeError("server exited before accepting a client")
        client = subprocess.Popen([
            executable, "--client", "-i", LOOPBACK,
            "-t", "{}:{}".format(LOOPBACK, tcp_port),
            "-u", "{}:{}".format(INPUT_GROUP, input_port),
        ])

        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sender.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(LOOPBACK))
        sender.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)

        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if server.poll() is not None or client.poll() is not None:
                raise RuntimeError("tunnel process exited before forwarding a datagram")
            sender.sendto(PAYLOAD, (INPUT_GROUP, input_port))
            try:
                payload, _ = receiver.recvfrom(2048)
                if payload == PAYLOAD:
                    return 0
            except socket.timeout:
                pass
            time.sleep(0.1)

        raise RuntimeError("timed out waiting for the tunneled multicast datagram")
    finally:
        receiver.close()
        if client is not None:
            stop(client)
        if server is not None:
            stop(server)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print("tunnel round-trip test failed: {}".format(error), file=sys.stderr)
        sys.exit(1)
