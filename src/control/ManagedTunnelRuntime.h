#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Producer.h"
#include "control/ControlService.h"
#include "control/TunnelRuntime.h"

namespace control {

// In-process supervisor for named tunnels and dummy producers. It owns worker
// threads; a later daemon/API adapter will own one instance of this class.
class ManagedTunnelRuntime final : public TunnelRuntime {
public:
    explicit ManagedTunnelRuntime(const ControlService& control_service, EventSink* event_sink = nullptr);
    ~ManagedTunnelRuntime() override;

    ManagedTunnelRuntime(const ManagedTunnelRuntime&) = delete;
    ManagedTunnelRuntime& operator=(const ManagedTunnelRuntime&) = delete;

    void start(std::string_view alias) override;
    void stop(std::string_view alias) override;
    void restart(std::string_view alias) override;
    std::vector<TunnelSnapshot> snapshots() const override;

    void startProducer(std::string_view alias, DatagramProducer::Options options);
    void stopProducer(std::string_view alias);
    void restartProducer(std::string_view alias, DatagramProducer::Options options);

private:
    struct Worker;
    using WorkerPtr = std::shared_ptr<Worker>;

    static std::string workerKey(RuntimeKind kind, std::string_view alias);
    void startTunnel(std::string alias);
    void startProducerWorker(std::string alias, DatagramProducer::Options options);
    void releaseFinishedSlot(RuntimeKind kind, std::string_view alias);
    void requestStop(RuntimeKind kind, std::string_view alias);
    void joinAndRemove(RuntimeKind kind, std::string_view alias);
    void transition(const WorkerPtr& worker, TunnelState state, std::string detail);
    void recordDatagram(const WorkerPtr& worker, std::size_t bytes);
    void recordLatency(const WorkerPtr& worker, double milliseconds);
    void publish(const TunnelSnapshot& snapshot, EventSeverity severity) const;

    const ControlService& control_service_;
    EventSink* event_sink_ = nullptr;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, WorkerPtr> workers_;
};

} // namespace control
