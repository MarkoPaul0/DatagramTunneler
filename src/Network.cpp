#include "Network.h"

#ifdef _WIN32

#include <cstdlib>
#include <iphlpapi.h>
#include <vector>

namespace {

void cleanupNetwork() {
    WSACleanup();
}

std::string utf8FromWide(const wchar_t* value) {
    if (value == nullptr) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    static_cast<void>(WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length, nullptr, nullptr));
    result.pop_back();
    return result;
}

} // namespace

#else

#include <ifaddrs.h>

#endif


bool initializeNetwork(int* error_code) {
#ifdef _WIN32
    WSADATA data;
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        *error_code = result;
        return false;
    }
    if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
        WSACleanup();
        *error_code = WSAVERNOTSUPPORTED;
        return false;
    }
    std::atexit(cleanupNetwork);
#else
    (void)error_code;
#endif
    return true;
}

std::optional<std::string> resolveInterfaceIpv4(std::string_view interface_reference) {
    in_addr address {};
    const std::string reference(interface_reference);
    if (inet_pton(AF_INET, reference.c_str(), &address) == 1) {
        return reference;
    }
#ifdef _WIN32
    ULONG size = 0;
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW) {
        return std::nullopt;
    }
    std::vector<unsigned char> storage(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &size) != NO_ERROR) {
        return std::nullopt;
    }
    for (auto* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
        if (reference != adapter->AdapterName && reference != utf8FromWide(adapter->FriendlyName)) {
            continue;
        }
        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            const auto* socket_address = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            if (socket_address == nullptr || socket_address->sin_family != AF_INET) {
                continue;
            }
            char text[INET_ADDRSTRLEN] {};
            if (inet_ntop(AF_INET, &socket_address->sin_addr, text, sizeof(text)) != nullptr) {
                return std::string(text);
            }
        }
    }
#else
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) {
        return std::nullopt;
    }
    for (const ifaddrs* item = interfaces; item != nullptr; item = item->ifa_next) {
        if (item->ifa_name == nullptr || item->ifa_addr == nullptr || reference != item->ifa_name || item->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        const auto* socket_address = reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
        char text[INET_ADDRSTRLEN] {};
        if (inet_ntop(AF_INET, &socket_address->sin_addr, text, sizeof(text)) != nullptr) {
            freeifaddrs(interfaces);
            return std::string(text);
        }
    }
    freeifaddrs(interfaces);
#endif
    return std::nullopt;
}
