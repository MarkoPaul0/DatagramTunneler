#include "Network.h"

#ifdef _WIN32

#include <cstdlib>

namespace {

void cleanupNetwork() {
    WSACleanup();
}

} // namespace

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
