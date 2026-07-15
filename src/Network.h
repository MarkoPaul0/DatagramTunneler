#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

// Windows headers define ERROR as a macro. The application uses ERROR for logging.
#ifdef ERROR
#undef ERROR
#endif

using SocketHandle = SOCKET;
using SocketIoSize = int;
using SocketAddressLength = int;
using SocketBufferLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
constexpr int kReceiveTruncationFlag = 0;
constexpr int kNoSignalFlag = 0;

#else

#include <cerrno>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

using SocketHandle = int;
using SocketIoSize = ssize_t;
using SocketAddressLength = socklen_t;
using SocketBufferLength = std::size_t;
constexpr SocketHandle kInvalidSocket = -1;
constexpr int kReceiveTruncationFlag = MSG_TRUNC;
#ifdef MSG_NOSIGNAL
constexpr int kNoSignalFlag = MSG_NOSIGNAL;
#else
constexpr int kNoSignalFlag = 0;
#endif

#endif

bool initializeNetwork(int* error_code);

// Accept either an IPv4 address or the name of a local network interface and
// return the IPv4 address needed by IPv4 multicast socket options.
std::optional<std::string> resolveInterfaceIpv4(std::string_view interface_reference);

inline void closeSocket(SocketHandle socket) {
    if (socket == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

class Socket {
public:
    Socket() = default;
    explicit Socket(SocketHandle socket) : socket_(socket) {}
    ~Socket() { reset(); }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept : socket_(other.release()) {}

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    SocketHandle get() const { return socket_; }
    bool valid() const { return socket_ != kInvalidSocket; }

    SocketHandle release() {
        const SocketHandle socket = socket_;
        socket_ = kInvalidSocket;
        return socket;
    }

    void reset(SocketHandle socket = kInvalidSocket) {
        closeSocket(socket_);
        socket_ = socket;
    }

private:
    SocketHandle socket_ = kInvalidSocket;
};

inline bool isInvalidSocket(SocketHandle socket) {
    return socket == kInvalidSocket;
}

inline int lastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

inline bool isReceiveTimeout(int error_code) {
#ifdef _WIN32
    return error_code == WSAETIMEDOUT || error_code == WSAEWOULDBLOCK;
#else
    return error_code == EAGAIN || error_code == EWOULDBLOCK;
#endif
}

inline bool isDatagramTooLargeError(int error_code) {
#ifdef _WIN32
    return error_code == WSAEMSGSIZE;
#else
    (void)error_code;
    return false;
#endif
}

template <typename T>
int setSocketOption(SocketHandle socket, int level, int option, const T& value) {
#ifdef _WIN32
    return setsockopt(socket, level, option, reinterpret_cast<const char*>(&value), static_cast<int>(sizeof(value)));
#else
    return setsockopt(socket, level, option, &value, sizeof(value));
#endif
}

inline bool setSocketReceiveTimeout(SocketHandle socket, int seconds) {
#ifdef _WIN32
    const DWORD timeout_ms = static_cast<DWORD>(seconds * 1000);
    return setSocketOption(socket, SOL_SOCKET, SO_RCVTIMEO, timeout_ms) == 0;
#else
    const timeval timeout = {seconds, 0};
    return setSocketOption(socket, SOL_SOCKET, SO_RCVTIMEO, timeout) == 0;
#endif
}
