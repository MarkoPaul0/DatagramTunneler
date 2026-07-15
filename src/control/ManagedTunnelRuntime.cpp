#include "control/ManagedTunnelRuntime.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

#include "DatagramTunneler.h"

namespace control {

struct ManagedTunnelRuntime::Worker {
    std::string key;
    TunnelSnapshot snapshot;
    std::jthread thread;
};

ManagedTunnelRuntime::ManagedTunnelRuntime(const ControlService& control_service, EventSink* event_sink)
    : control_service_(control_service), event_sink_(event_sink) {}

ManagedTunnelRuntime::~ManagedTunnelRuntime() {
    std::vector<WorkerPtr> workers;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [key, worker] : workers_) {
            (void)key;
            workers.push_back(worker);
        }
    }
    for (const WorkerPtr& worker : workers) {
        worker->thread.request_stop();
    }
    for (const WorkerPtr& worker : workers) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }
}

std::string ManagedTunnelRuntime::workerKey(RuntimeKind kind, std::string_view alias) {
    const char* const prefix = kind == RuntimeKind::Tunnel ? "tunnel:" : "producer:";
    return std::string(prefix) + std::string(alias);
}

void ManagedTunnelRuntime::start(std::string_view alias) {
    startTunnel(std::string(alias));
}

void ManagedTunnelRuntime::startTunnel(std::string alias) {
    const NamedTunnel definition = control_service_.tunnel(alias);
    const std::string key = workerKey(RuntimeKind::Tunnel, alias);
    releaseFinishedSlot(RuntimeKind::Tunnel, alias);

    const WorkerPtr worker = std::make_shared<Worker>();
    worker->key = key;
    worker->snapshot = {alias, RuntimeKind::Tunnel, TunnelState::Starting, {}, "Starting"};
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        workers_.emplace(key, worker);
    }
    publish(worker->snapshot, EventSeverity::Info);

    try {
        worker->thread = std::jthread([this, worker, config = definition.config](std::stop_token stop_token) {
            transition(worker, TunnelState::Running, "Running");
            try {
                DatagramTunneler tunneler(config);
                tunneler.run(stop_token);
                transition(worker, TunnelState::Stopped,
                           stop_token.stop_requested() ? "Stopped by request" : "Tunnel finished");
            } catch (const std::exception& error) {
                transition(worker, TunnelState::Failed, error.what());
            }
        });
    } catch (...) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            workers_.erase(key);
        }
        throw;
    }
}

void ManagedTunnelRuntime::startProducer(std::string_view alias, DatagramProducer::Options options) {
    startProducerWorker(std::string(alias), std::move(options));
}

void ManagedTunnelRuntime::startProducerWorker(std::string alias, DatagramProducer::Options options) {
    const NamedTunnel definition = control_service_.tunnel(alias);
    if (!definition.config.is_client_) {
        throw std::runtime_error("producer alias '" + alias + "' must name a client tunnel");
    }
    const std::string key = workerKey(RuntimeKind::Producer, alias);
    releaseFinishedSlot(RuntimeKind::Producer, alias);

    const WorkerPtr worker = std::make_shared<Worker>();
    worker->key = key;
    worker->snapshot = {alias, RuntimeKind::Producer, TunnelState::Starting, {}, "Starting"};
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        workers_.emplace(key, worker);
    }
    publish(worker->snapshot, EventSeverity::Info);

    try {
        worker->thread = std::jthread([this, worker, config = definition.config, options = std::move(options)](
                                          std::stop_token stop_token) {
            transition(worker, TunnelState::Running, "Running");
            try {
                DatagramProducer producer(config, options);
                producer.run(stop_token);
                transition(worker, TunnelState::Stopped,
                           stop_token.stop_requested() ? "Stopped by request" : "Producer finished");
            } catch (const std::exception& error) {
                transition(worker, TunnelState::Failed, error.what());
            }
        });
    } catch (...) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            workers_.erase(key);
        }
        throw;
    }
}

void ManagedTunnelRuntime::releaseFinishedSlot(RuntimeKind kind, std::string_view alias) {
    const std::string key = workerKey(kind, alias);
    WorkerPtr worker;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = workers_.find(key);
        if (iterator == workers_.end()) {
            return;
        }
        worker = iterator->second;
        if (worker->snapshot.state == TunnelState::Starting || worker->snapshot.state == TunnelState::Running ||
            worker->snapshot.state == TunnelState::Stopping) {
            throw std::runtime_error("runtime '" + std::string(alias) + "' is already active");
        }
        workers_.erase(iterator);
    }
    if (worker->thread.joinable()) {
        worker->thread.join();
    }
}

void ManagedTunnelRuntime::stop(std::string_view alias) {
    requestStop(RuntimeKind::Tunnel, alias);
}

void ManagedTunnelRuntime::stopProducer(std::string_view alias) {
    requestStop(RuntimeKind::Producer, alias);
}

void ManagedTunnelRuntime::requestStop(RuntimeKind kind, std::string_view alias) {
    const std::string key = workerKey(kind, alias);
    TunnelSnapshot snapshot;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = workers_.find(key);
        if (iterator == workers_.end()) {
            throw std::runtime_error("runtime '" + std::string(alias) + "' is not managed");
        }
        WorkerPtr& worker = iterator->second;
        if (worker->snapshot.state == TunnelState::Stopped || worker->snapshot.state == TunnelState::Failed) {
            return;
        }
        worker->snapshot.state = TunnelState::Stopping;
        worker->snapshot.detail = "Stopping";
        worker->thread.request_stop();
        snapshot = worker->snapshot;
    }
    publish(snapshot, EventSeverity::Info);
}

void ManagedTunnelRuntime::restart(std::string_view alias) {
    stop(alias);
    joinAndRemove(RuntimeKind::Tunnel, alias);
    start(alias);
}

void ManagedTunnelRuntime::restartProducer(std::string_view alias, DatagramProducer::Options options) {
    stopProducer(alias);
    joinAndRemove(RuntimeKind::Producer, alias);
    startProducer(alias, std::move(options));
}

void ManagedTunnelRuntime::joinAndRemove(RuntimeKind kind, std::string_view alias) {
    const std::string key = workerKey(kind, alias);
    WorkerPtr worker;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = workers_.find(key);
        if (iterator == workers_.end()) {
            throw std::runtime_error("runtime '" + std::string(alias) + "' is not managed");
        }
        worker = iterator->second;
        worker->thread.request_stop();
    }
    if (worker->thread.joinable()) {
        worker->thread.join();
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = workers_.find(key);
        if (iterator != workers_.end() && iterator->second == worker) {
            workers_.erase(iterator);
        }
    }
}

std::vector<TunnelSnapshot> ManagedTunnelRuntime::snapshots() const {
    std::vector<TunnelSnapshot> result;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        result.reserve(workers_.size());
        for (const auto& [key, worker] : workers_) {
            (void)key;
            result.push_back(worker->snapshot);
        }
    }
    std::sort(result.begin(), result.end(), [](const TunnelSnapshot& left, const TunnelSnapshot& right) {
        return left.alias < right.alias;
    });
    return result;
}

void ManagedTunnelRuntime::transition(const WorkerPtr& worker, TunnelState state, std::string detail) {
    TunnelSnapshot snapshot;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        worker->snapshot.state = state;
        worker->snapshot.detail = std::move(detail);
        snapshot = worker->snapshot;
    }
    publish(snapshot, state == TunnelState::Failed ? EventSeverity::Error : EventSeverity::Info);
}

void ManagedTunnelRuntime::publish(const TunnelSnapshot& snapshot, EventSeverity severity) const {
    if (event_sink_ == nullptr) {
        return;
    }
    ControlEvent event;
    event.kind = EventKind::Lifecycle;
    event.severity = severity;
    event.alias = snapshot.alias;
    event.message = snapshot.detail;
    event.snapshot = snapshot;
    try {
        event_sink_->publish(event);
    } catch (...) {
        // A presentation adapter must not terminate tunnel workers.
    }
}

} // namespace control
