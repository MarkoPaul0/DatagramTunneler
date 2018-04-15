# DatagramTunneler
![Author](https://img.shields.io/badge/author-MarkoPaul0-red.svg?style=flat-square)
[![License: GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue.svg?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.en.html)
![GitHub last commit](https://img.shields.io/github/last-commit/MarkoPaul0/DatagramTunneler.svg?style=flat-square&maxAge=300)
<!--
![GitHub (pre-)release](https://img.shields.io/github/release/MarkoPaul0/WireBait/all.svg?style=flat-square)
![GitHub (pre-)release](https://img.shields.io/github/commits-since/MarkoPaul0/WireBait/latest.svg?style=flat-square)-->

Simple client/server program forwarding UDP datagrams through a TCP connection. The client joins a multicast group and forwards the received datagrams to the server, which multicasts them on its subnet.

* Designed with simplicity in mind, not low latency.
* Take "*cross-platform*" with a grain of salt. Only tested on OSX and Ubuntu 16.04 so far. (obviously not Windows compatible)
