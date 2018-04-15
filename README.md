# DatagramTunneler
Simple client/server program forwarding UDP datagrams through a TCP connection. The client joins a multicast group and forwards the received datagrams to the server, which multicasts them on its subnet.

* Designed with simplicity in mind, not low latency.
* Take "*cross-platform*" with a grain of salt. Only tested on OSX and Ubuntu 16.04 so far. It will for sure not work on Windows.
