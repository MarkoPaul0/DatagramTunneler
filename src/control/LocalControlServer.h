#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include "Network.h"
#include "control/ControlService.h"
#include "control/ControlTypes.h"
#include "control/ManagedTunnelRuntime.h"

namespace control {

struct LocalControlServerOptions {
    std::uint16_t port = 8765;
};

// A deliberately local-only control endpoint. It never binds a non-loopback
// address and has no authentication; remote access is a later, separate task.
class LocalControlServer final : public EventSink {
public:
    LocalControlServer(std::filesystem::path configuration_path, LocalControlServerOptions options = {});
    ~LocalControlServer() override;

    LocalControlServer(const LocalControlServer&) = delete;
    LocalControlServer& operator=(const LocalControlServer&) = delete;

    void run();
    void requestStop();
    void publish(const ControlEvent& event) override;

private:
    struct WebSocketClient;

    void serveConnection(Socket socket);
    void broadcastEvent(const ControlEvent& event);
    void broadcastMetricSnapshots();

    LocalControlServerOptions options_;
    ControlService control_service_;
    ManagedTunnelRuntime runtime_;
    std::atomic_bool stop_requested_ = false;
    Socket listener_;
    std::mutex clients_mutex_;
    std::vector<std::shared_ptr<WebSocketClient>> clients_;
};

} // namespace control
