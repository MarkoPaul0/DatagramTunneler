#include "DatagramTunneler.h"

#include <assert.h>
#include <cstdint>
#include <utility>

#include "Log.h"
#include "Network.h"
#include "Protocol.h"

namespace {

constexpr int kNoFlags = 0;
constexpr int kHeartbeatPeriodSeconds = 5;

Socket createSocket(int type) {
    Socket socket_handle(socket(AF_INET, type, kNoFlags));
    if (!socket_handle.valid()) {
        DEATH("Could not create socket! Error %d", lastSocketError());
    }
    return socket_handle;
}

} // namespace


// This method is local to this cpp file and sends the entire buffer over TCP.
static bool sendTCPData(SocketHandle tcp_socket, const void* data, size_t datalen, int flags) {
    size_t len_to_send = datalen;
    const char* p = reinterpret_cast<const char*>(data);
    do {
        SocketIoSize len_sent = 0;
        if((len_sent = send(tcp_socket, p, static_cast<SocketBufferLength>(len_to_send), flags)) < 0) {
            return false;
        }
        if (len_sent == 0) {
            DEATH("Unable to send data via TCP. The server might not be able to keep up!"); //although this is not technically an error, I do not want to keep going in that case
        }
        assert(len_to_send >= static_cast<size_t>(len_sent));
        len_to_send -= static_cast<size_t>(len_sent);
        p += len_sent;
    } while (len_to_send != 0);
    return true;
}


bool DatagramTunneler::Config::isComplete() const {
    if (udp_iface_ip_.empty() ||
        //tcp_iface_ip_.empty() || //TODO
        tcp_srv_port_ == 0) {
        return false;
    }
    if (is_client_) {
        if (tcp_srv_ip_.empty() || udp_dst_ip_.empty() || udp_dst_port_ == 0) {
            return false;
        }
    } else if (!use_clt_grp_ && (udp_dst_ip_.empty() || udp_dst_port_ == 0)) {
        return false;
    }
    return true;
}


// ---------------------------- DatagramTunneler Implementation--------------------------------
DatagramTunneler::DatagramTunneler(Config cfg) : cfg_(std::move(cfg)) {
    if (cfg_.is_client_)
        setupClient(cfg_);
    else
        setupServer(cfg_);
}


void DatagramTunneler::run() {
    if (cfg_.is_client_)
        runClient();
    else
        runServer();
}


//------------------------------------------------------------------------------------------------
// CLIENT SIDE METHODS
//------------------------------------------------------------------------------------------------
void DatagramTunneler::setupClient(const Config& cfg) {
    // Creating UDP socket
    udp_socket_ = createSocket(SOCK_DGRAM);

    // Setting timeout on UDP socket so as to send data to server at least every HEARTBEAT_PERIOD_SEC seconds
    if (!setSocketReceiveTimeout(udp_socket_.get(), kHeartbeatPeriodSeconds)) {
        DEATH("Could not set receive timeout on UDP socket! Error %d", lastSocketError());
    }

    // Binding UDP socket to configured port
    sockaddr_in bind_addr {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(cfg.udp_dst_port_);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(udp_socket_.get(), reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        DEATH("Could not bind UDP socket to port %u! Error %d", cfg.udp_dst_port_, lastSocketError());
    }

    // Creating TCP socket
    tcp_socket_ = createSocket(SOCK_STREAM);

    //OSX only
#ifdef __APPLE__
    // If the TCP server crashes, the client will just exit abruptly because the client socket sends
    // a sigpipe signal when invoking send() instead of returning an error
    // on other distributions, we use MSG_NOSIGNAL when invoking send()
    int set = 1;
    if (setSocketOption(tcp_socket_.get(), SOL_SOCKET, SO_NOSIGPIPE, set) < 0) {
        DEATH("Could not prevent TCP socket to send SIGPIPE on disconnect! Error %d", lastSocketError());
    }
#endif
}


void DatagramTunneler::runClient() {
    // Connect to TCP server
    sockaddr_in server_addr {};
    //TODO: set a tcp interface ip!
    server_addr.sin_addr.s_addr = inet_addr(cfg_.tcp_srv_ip_.c_str());
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(cfg_.tcp_srv_port_);
    if (connect(tcp_socket_.get(), reinterpret_cast<struct sockaddr *>(&server_addr), sizeof(server_addr)) < 0) {
        DEATH("Unable to connect to server %s:%u. Error %d!", cfg_.tcp_srv_ip_.c_str(), cfg_.tcp_srv_port_, lastSocketError());
    }
    INFO("[DatagramTunneler][CLIENT-MODE] connected to TCP remote %s:%u and listening to multicast %s:%u",
    cfg_.tcp_srv_ip_.c_str(), cfg_.tcp_srv_port_, cfg_.udp_dst_ip_.c_str(), cfg_.udp_dst_port_);

    // Join multicast group
    ip_mreq udp_group {};
    udp_group.imr_multiaddr.s_addr = inet_addr(cfg_.udp_dst_ip_.c_str());
    udp_group.imr_interface.s_addr = inet_addr(cfg_.udp_iface_ip_.c_str());
    if(setSocketOption(udp_socket_.get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, udp_group) < 0) {
        DEATH("Could not join multicast group %s:%u. Error %d", cfg_.udp_dst_ip_.c_str(), cfg_.udp_dst_port_, lastSocketError());
    }
    INFO("Joined multicast group %s:%u.", cfg_.udp_dst_ip_.c_str(), cfg_.udp_dst_port_);

    // Setting up the DTEP header in the TunnelPacket struct
    TunnelPacket tunnel_pkt {};
    inet_pton(AF_INET, cfg_.udp_dst_ip_.c_str(), &tunnel_pkt.udp_dst_ip_);
    tunnel_pkt.udp_dst_port_ = cfg_.udp_dst_port_;
    SocketIoSize len_read = 0;
    // Running loop
    while (true) {
        // Read datagram from udp socket
        if((len_read = recv(udp_socket_.get(), reinterpret_cast<char*>(tunnel_pkt.databuf_.data()), static_cast<SocketBufferLength>(kMaxDatagramLength), kReceiveTruncationFlag)) < 0) {
            //TODO: handle errors and edge cases such as jumbo frames
            const int error_code = lastSocketError();
            if (isDatagramTooLargeError(error_code)) {
                WARN("Discarding jumbo datagram.");
                continue;
            }
            if (!isReceiveTimeout(error_code)) {
                DEATH("Unable to read data from UDP socket %d", error_code);
            }
            tunnel_pkt.type_ = TunnelPacketType::Heartbeat;
            INFO("Sending a heartbeat to server.");
        } else {
            assert(len_read <= static_cast<SocketIoSize>(kMaxDatagramLength));
            if (len_read > static_cast<SocketIoSize>(kMaxDatagramLength)) { //this is possible because we are using MSG_TRUNC flag
                WARN("Discarding jumbo datagram of %zu bytes!", static_cast<size_t>(len_read));
                continue;
            }
            tunnel_pkt.type_ = TunnelPacketType::Datagram;
            INFO("Tunneling a %zu byte datagram to server.", static_cast<size_t>(len_read));
            tunnel_pkt.datalen_ = static_cast<uint16_t>(len_read);
        }

        // Send the encapsulated datagram to the server over the TCP connection
        if(!sendTCPData(tcp_socket_.get(), &tunnel_pkt, tunnel_pkt.size(), kNoSignalFlag)) {
            DEATH("Unable to send data to server! Error %d", lastSocketError());
        }
    }
}
//------------------------------------------------------------------------------------------------


//------------------------------------------------------------------------------------------------
// SERVER SIDE METHODS
//------------------------------------------------------------------------------------------------
void DatagramTunneler::setupServer(const Config& cfg) {
    // Creating UDP socket
    udp_socket_ = createSocket(SOCK_DGRAM);

    // Setting up the UDP publishing interface
    in_addr iface;
    iface.s_addr = inet_addr(cfg_.udp_iface_ip_.c_str());
    if(setSocketOption(udp_socket_.get(), IPPROTO_IP, IP_MULTICAST_IF, iface) < 0) {
        DEATH("Could not set UDP publisher interface to %s! Error %d", cfg_.udp_iface_ip_.c_str(), lastSocketError());
    }

    // Creating TCP socket
    tcp_socket_ = createSocket(SOCK_STREAM);

    // Binding the TCP socket
    sockaddr_in tcp_iface {};
    tcp_iface.sin_family = AF_INET;
    tcp_iface.sin_port = htons (cfg_.tcp_srv_port_);
    tcp_iface.sin_addr.s_addr = htonl(INADDR_ANY); //TODO: review that
    if(bind(tcp_socket_.get(), reinterpret_cast<sockaddr*>(&tcp_iface), sizeof(tcp_iface)) < 0) {
        DEATH("Could not bind TCP socket to port %u! Error %d", cfg.tcp_srv_port_, lastSocketError());
    }
}


void DatagramTunneler::runServer() {
    INFO("[DatagramTunneler][SERVER MODE] listening for client connection on port %u...", cfg_.tcp_srv_port_);
    //Listening for client connection and accepting connection
    if (listen(tcp_socket_.get(), 1) < 0) {
        DEATH("Unable to listen on TCP port %u, error %d!", cfg_.tcp_srv_port_, lastSocketError());
    }
    sockaddr remote;
    SocketAddressLength sosize  = sizeof(remote);
    Socket client_socket(accept(tcp_socket_.get(), &remote, &sosize));
    if (!client_socket.valid()) {
        DEATH("Unable to accept incoming TCP connection, accept() error %d!", lastSocketError());
    }
    INFO("Accepted incoming connection from a remote host. Waiting for forwarded datagrams...");

    // Setting up TCP timeout HEARTBEAT_PERIOD_SEC + 1 second
    if (!setSocketReceiveTimeout(client_socket.get(), kHeartbeatPeriodSeconds + 1)) {
        DEATH("Could not set receive timeout on TCP socket! Error %d", lastSocketError());
    }

    sockaddr_in pub_group {};
    if (!cfg_.use_clt_grp_) { //if not reusing the multicast joined by the client then using the one set in the configuration to publish data
        pub_group.sin_family = AF_INET;
        pub_group.sin_port = htons(cfg_.udp_dst_port_);
        pub_group.sin_addr.s_addr = inet_addr(cfg_.udp_dst_ip_.c_str());
    }

    TunnelPacket tunnel_pkt {};
    char* p = reinterpret_cast<char*>(&tunnel_pkt); //a write pointer
    SocketIoSize len_read = 0;
    size_t len_to_read = 1;
    while(true) {
        assert(len_to_read != 0);
        // Reading data sent from client
        if ((len_read = recv(client_socket.get(), p, static_cast<SocketBufferLength>(len_to_read), kNoSignalFlag)) < 0) {
            if (isReceiveTimeout(lastSocketError())) {
                INFO("Client has been silent for too long. Terminating");
                return;
            } else {
                DEATH("Unable to read data from TCP socket, recv() error %d!", lastSocketError());
            }
        }
        if (len_read == 0) {
            INFO("Client terminated!"); //TODO: review conditions under which this could happen
            return;
        }
        if (tunnel_pkt.type_ == TunnelPacketType::Heartbeat) {
            INFO("Received heartbeat from client.");
            continue;
        }
        p += len_read;
        assert(p > reinterpret_cast<char*>(&tunnel_pkt));
        const size_t data_len = static_cast<size_t>(p - reinterpret_cast<char*>(&tunnel_pkt));
        if (data_len < kTunnelPacketHeaderLength) {
            len_to_read = kTunnelPacketHeaderLength - data_len;
            continue; //read enough bytes to get the whole DTEP header
        }
        if (data_len < tunnel_pkt.size()) {
            len_to_read = tunnel_pkt.size() - data_len;
            continue; //we need the whole DTEP packet before publishing it
        }
        assert(data_len == tunnel_pkt.size()); //we only read enough byte to at most get the whole DTEP (+8 for DTEP header)
        p = reinterpret_cast<char*>(&tunnel_pkt); //we have read a full packet, we now reset the write pointer to the beginning of tunnel_pkt
        len_to_read = 1;

        if (cfg_.use_clt_grp_) { //potential for feedbackloop if both client and server are in the same subnet
//however if that were the case, there would be no need to forward the datagrams
            pub_group = {};
            pub_group.sin_family = AF_INET;
            pub_group.sin_port = htons(tunnel_pkt.udp_dst_port_);
            pub_group.sin_addr.s_addr = tunnel_pkt.udp_dst_ip_;
        }

        // Multicasting the datagrams received from the client
        if(sendto(udp_socket_.get(), reinterpret_cast<const char*>(tunnel_pkt.databuf_.data()), tunnel_pkt.datalen_, kNoSignalFlag, reinterpret_cast<struct sockaddr*>(&pub_group), sizeof(pub_group)) < 0) {
            DEATH("Unable to publish multicast data, sendto() error %d!", lastSocketError());
        }

        //Getting group on which the datagram was published on client side
        char clt_grp_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &tunnel_pkt.udp_dst_ip_, clt_grp_ip, INET_ADDRSTRLEN);

        //Getting group on which the server is publishing the forwared datagrams
        char pub_grp_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &pub_group.sin_addr, pub_grp_ip, INET_ADDRSTRLEN);
        INFO("Published to %s:%u a %u byte datagram tunneled by client. Client side group was %s:%u",
        pub_grp_ip, ntohs(pub_group.sin_port), tunnel_pkt.datalen_, clt_grp_ip, tunnel_pkt.udp_dst_port_);
    }
}
