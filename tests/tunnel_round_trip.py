#!/usr/bin/env python3
"""Exercise the client-to-server multicast tunnel on the loopback interface."""

import socket
import subprocess
import sys
import time


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


def main():
    if len(sys.argv) != 2:
        raise RuntimeError("expected the dgramtunneler executable path")

    executable = sys.argv[1]
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
