#include "control/ManagedTunnelRuntime.h"

#include <algorithm>
#include <chrono>
#include <deque>
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
    std::chrono::steady_clock::time_point started_at = std::chrono::steady_clock::now();
    std::deque<double> latency_samples;
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
    const TunnelState initial_state = definition.config.is_client_ ? TunnelState::Connecting : TunnelState::Starting;
    const std::string initial_detail = definition.config.is_client_
        ? "Connecting to " + definition.config.tcp_srv_ip_ + ":" + std::to_string(definition.config.tcp_srv_port_)
        : "Starting";
    worker->snapshot = {alias, RuntimeKind::Tunnel, initial_state, {}, {}, initial_detail};
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        workers_.emplace(key, worker);
    }
    publish(worker->snapshot, EventSeverity::Info);

    try {
        worker->thread = std::jthread([this, worker, config = definition.config](std::stop_token stop_token) {
            worker->started_at = std::chrono::steady_clock::now();
            if (!config.is_client_) {
                transition(worker, TunnelState::Running, "Running");
            }
            try {
                DatagramTunneler::RuntimeObserver observer;
                observer.on_datagram = [this, worker](const DatagramTunneler::DatagramObservation& observation) {
                    recordDatagram(worker, observation);
                };
                observer.on_client_connection_state = [this, worker, config](DatagramTunneler::ClientConnectionState state) {
                    if (state == DatagramTunneler::ClientConnectionState::Connected) {
                        transition(worker, TunnelState::Connected,
                                   "Connected to " + config.tcp_srv_ip_ + ":" + std::to_string(config.tcp_srv_port_));
                    }
                };
                DatagramTunneler tunneler(config, std::move(observer));
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
    worker->snapshot = {alias, RuntimeKind::Producer, TunnelState::Starting, {}, {}, "Starting"};
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
        if (worker->snapshot.state == TunnelState::Starting || worker->snapshot.state == TunnelState::Connecting ||
            worker->snapshot.state == TunnelState::Connected || worker->snapshot.state == TunnelState::Running ||
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

void ManagedTunnelRuntime::recordDatagram(const WorkerPtr& worker, const DatagramTunneler::DatagramObservation& observation) {
    const std::lock_guard<std::mutex> lock(mutex_);
    ++worker->snapshot.metrics.datagram_count;
    worker->snapshot.metrics.byte_count += observation.bytes;
    const double elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - worker->started_at).count();
    if (elapsed_seconds > 0.0) {
        worker->snapshot.metrics.throughput_bytes_per_second =
            static_cast<double>(worker->snapshot.metrics.byte_count) / elapsed_seconds;
    }
    worker->snapshot.recent_datagrams.push_back({std::chrono::system_clock::now(), observation.bytes, observation.latency_milliseconds});
    if (worker->snapshot.recent_datagrams.size() > 10U) {
        worker->snapshot.recent_datagrams.erase(worker->snapshot.recent_datagrams.begin());
    }
    if (!observation.latency_milliseconds.has_value()) {
        return;
    }
    worker->latency_samples.push_back(*observation.latency_milliseconds);
    if (worker->latency_samples.size() > 1024U) {
        worker->latency_samples.pop_front();
    }
    std::vector<double> samples(worker->latency_samples.begin(), worker->latency_samples.end());
    std::sort(samples.begin(), samples.end());
    double total = 0.0;
    for (const double sample : samples) {
        total += sample;
    }
    const auto percentile = [&samples](double value) {
        return samples[static_cast<std::size_t>(value * static_cast<double>(samples.size() - 1U))];
    };
    worker->snapshot.metrics.average_latency_milliseconds = total / static_cast<double>(samples.size());
    worker->snapshot.metrics.p50_latency_milliseconds = percentile(0.50);
    worker->snapshot.metrics.p99_latency_milliseconds = percentile(0.99);
    worker->snapshot.metrics.maximum_latency_milliseconds = samples.back();
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
