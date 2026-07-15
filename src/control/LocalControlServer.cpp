#include "control/LocalControlServer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <map>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "control/ControlApi.h"
#include "control/WebAssets.h"

namespace control {
namespace {

constexpr std::size_t kMaximumRequestSize = 1024U * 1024U;
constexpr std::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

struct HttpRequest {
    std::string method;
    std::string target;
    std::map<std::string, std::string, std::less<>> headers;
    std::string body;
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool sendAll(SocketHandle socket, std::string_view data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const std::size_t remaining = data.size() - sent;
        const SocketBufferLength length = static_cast<SocketBufferLength>(remaining);
        const SocketIoSize result = send(socket, data.data() + sent, length, kNoSignalFlag);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

std::optional<std::size_t> contentLength(const HttpRequest& request) {
    const auto it = request.headers.find("content-length");
    if (it == request.headers.end()) {
        return std::size_t{0};
    }
    std::size_t value = 0;
    const auto [pointer, error] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
    if (error != std::errc{} || pointer != it->second.data() + it->second.size() || value > kMaximumRequestSize) {
        return std::nullopt;
    }
    return value;
}

std::optional<HttpRequest> receiveRequest(SocketHandle socket) {
    std::string data;
    data.reserve(4096);
    std::array<char, 4096> buffer{};
    std::size_t header_end = std::string::npos;
    while (data.size() < kMaximumRequestSize) {
        const SocketIoSize received = recv(socket, buffer.data(), static_cast<SocketBufferLength>(buffer.size()), 0);
        if (received <= 0) {
            return std::nullopt;
        }
        data.append(buffer.data(), static_cast<std::size_t>(received));
        header_end = data.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            break;
        }
    }
    if (header_end == std::string::npos) {
        return std::nullopt;
    }

    HttpRequest request;
    std::istringstream headers(data.substr(0, header_end));
    std::string line;
    if (!std::getline(headers, line)) {
        return std::nullopt;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    std::istringstream request_line(line);
    std::string version;
    if (!(request_line >> request.method >> request.target >> version) || version != "HTTP/1.1") {
        return std::nullopt;
    }
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            return std::nullopt;
        }
        std::string name = lower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.erase(value.begin());
        }
        request.headers.insert_or_assign(std::move(name), std::move(value));
    }
    const std::optional<std::size_t> expected_length = contentLength(request);
    if (!expected_length.has_value()) {
        return std::nullopt;
    }
    request.body = data.substr(header_end + 4);
    while (request.body.size() < *expected_length) {
        const SocketIoSize received = recv(socket, buffer.data(), static_cast<SocketBufferLength>(buffer.size()), 0);
        if (received <= 0) {
            return std::nullopt;
        }
        request.body.append(buffer.data(), static_cast<std::size_t>(received));
    }
    if (request.body.size() != *expected_length) {
        return std::nullopt;
    }
    return request;
}

std::string jsonEscape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char raw_character : value) {
        const unsigned char character = static_cast<unsigned char>(raw_character);
        switch (character) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (character < 0x20U) {
                std::ostringstream output;
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned int>(character);
                escaped += output.str();
            } else {
                escaped += static_cast<char>(character);
            }
        }
    }
    return escaped;
}

std::optional<std::string> jsonStringProperty(std::string_view json, std::string_view name) {
    const std::string marker = "\"" + std::string(name) + "\"";
    std::size_t position = json.find(marker);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    position = json.find(':', position + marker.size());
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])) != 0) {
        ++position;
    }
    if (position >= json.size() || json[position++] != '"') {
        return std::nullopt;
    }
    std::string value;
    bool escaped = false;
    for (; position < json.size(); ++position) {
        const char character = json[position];
        if (!escaped && character == '"') {
            return value;
        }
        if (escaped) {
            switch (character) {
            case '"': value += '"'; break;
            case '\\': value += '\\'; break;
            case '/': value += '/'; break;
            case 'b': value += '\b'; break;
            case 'f': value += '\f'; break;
            case 'n': value += '\n'; break;
            case 'r': value += '\r'; break;
            case 't': value += '\t'; break;
            default: return std::nullopt;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else {
            value += character;
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t> jsonUnsignedProperty(std::string_view json, std::string_view name) {
    const std::string marker = "\"" + std::string(name) + "\"";
    std::size_t position = json.find(marker);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    position = json.find(':', position + marker.size());
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])) != 0) {
        ++position;
    }
    std::uint64_t value = 0;
    const auto [pointer, error] = std::from_chars(json.data() + position, json.data() + json.size(), value);
    if (error != std::errc{} || pointer == json.data() + position) {
        return std::nullopt;
    }
    return value;
}

const char* stateName(TunnelState state) {
    switch (state) {
    case TunnelState::Stopped: return "stopped";
    case TunnelState::Starting: return "starting";
    case TunnelState::Running: return "running";
    case TunnelState::Stopping: return "stopping";
    case TunnelState::Failed: return "failed";
    }
    return "unknown";
}

const char* kindName(RuntimeKind kind) {
    return kind == RuntimeKind::Tunnel ? "tunnel" : "producer";
}

const char* eventKindName(EventKind kind) {
    switch (kind) {
    case EventKind::Lifecycle: return "lifecycle";
    case EventKind::Log: return "log";
    case EventKind::Metrics: return "metrics";
    }
    return "unknown";
}

const char* severityName(EventSeverity severity) {
    switch (severity) {
    case EventSeverity::Info: return "info";
    case EventSeverity::Warning: return "warning";
    case EventSeverity::Error: return "error";
    }
    return "unknown";
}

std::string snapshotJson(const TunnelSnapshot& snapshot) {
    std::ostringstream output;
    const auto optionalNumber = [&output](const std::optional<double>& value) {
        if (value.has_value()) {
            output << *value;
        } else {
            output << "null";
        }
    };
    output << "{\"alias\":\"" << jsonEscape(snapshot.alias) << "\",\"kind\":\"" << kindName(snapshot.kind)
           << "\",\"state\":\"" << stateName(snapshot.state) << "\",\"detail\":\"" << jsonEscape(snapshot.detail)
           << "\",\"metrics\":{\"datagram_count\":" << snapshot.metrics.datagram_count
           << ",\"byte_count\":" << snapshot.metrics.byte_count
           << ",\"throughput_bytes_per_second\":" << snapshot.metrics.throughput_bytes_per_second
           << ",\"average_latency_milliseconds\":";
    optionalNumber(snapshot.metrics.average_latency_milliseconds);
    output << ",\"p50_latency_milliseconds\":";
    optionalNumber(snapshot.metrics.p50_latency_milliseconds);
    output << ",\"p99_latency_milliseconds\":";
    optionalNumber(snapshot.metrics.p99_latency_milliseconds);
    output << ",\"maximum_latency_milliseconds\":";
    optionalNumber(snapshot.metrics.maximum_latency_milliseconds);
    output << "}}";
    return output.str();
}

std::string eventJson(const ControlEvent& event) {
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(event.timestamp.time_since_epoch()).count();
    std::ostringstream output;
    output << "{\"api_version\":\"" << api::kVersion << "\",\"event\":{\"kind\":\"" << eventKindName(event.kind)
           << "\",\"severity\":\"" << severityName(event.severity) << "\",\"timestamp_unix_milliseconds\":" << milliseconds
           << ",\"alias\":\"" << jsonEscape(event.alias) << "\",\"message\":\"" << jsonEscape(event.message) << "\"";
    if (event.snapshot.has_value()) {
        output << ",\"snapshot\":" << snapshotJson(*event.snapshot);
    }
    output << "}}";
    return output.str();
}

std::string errorJson(std::string_view code, std::string_view message) {
    return "{\"error\":{\"code\":\"" + std::string(code) + "\",\"message\":\"" + jsonEscape(message) + "\"}}";
}

bool sendJson(SocketHandle socket, int status, std::string_view body) {
    const char* reason = status == 200 ? "OK" : status == 202 ? "Accepted" : status == 400 ? "Bad Request" :
                         status == 404 ? "Not Found" : status == 409 ? "Conflict" : "Internal Server Error";
    const std::string response = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\nContent-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + std::string(body);
    return sendAll(socket, response);
}

bool sendAsset(SocketHandle socket, std::string_view content_type, std::string_view body) {
    const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(content_type) +
        "; charset=utf-8\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'self'; connect-src 'self' ws: wss:; style-src 'self'; script-src 'self'\r\n"
        "Connection: close\r\n\r\n" + std::string(body);
    return sendAll(socket, response);
}

// Compact SHA-1 implementation used only for the standard WebSocket handshake.
std::array<std::uint8_t, 20> sha1(std::string_view input) {
    std::vector<std::uint8_t> bytes(input.begin(), input.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
    bytes.push_back(0x80U);
    while ((bytes.size() % 64U) != 56U) bytes.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<std::uint8_t>(bit_length >> shift));
    std::uint32_t h0 = 0x67452301U, h1 = 0xEFCDAB89U, h2 = 0x98BADCFEU, h3 = 0x10325476U, h4 = 0xC3D2E1F0U;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64U) {
        std::array<std::uint32_t, 80> words{};
        for (std::size_t index = 0; index < 16U; ++index) {
            words[index] = (static_cast<std::uint32_t>(bytes[offset + index * 4U]) << 24U) |
                           (static_cast<std::uint32_t>(bytes[offset + index * 4U + 1U]) << 16U) |
                           (static_cast<std::uint32_t>(bytes[offset + index * 4U + 2U]) << 8U) |
                           static_cast<std::uint32_t>(bytes[offset + index * 4U + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const std::uint32_t value = words[index - 3U] ^ words[index - 8U] ^ words[index - 14U] ^ words[index - 16U];
            words[index] = (value << 1U) | (value >> 31U);
        }
        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t function = index < 20U ? ((b & c) | (~b & d)) : index < 40U ? (b ^ c ^ d) :
                                           index < 60U ? ((b & c) | (b & d) | (c & d)) : (b ^ c ^ d);
            const std::uint32_t constant = index < 20U ? 0x5A827999U : index < 40U ? 0x6ED9EBA1U :
                                           index < 60U ? 0x8F1BBCDCU : 0xCA62C1D6U;
            const std::uint32_t rotated = (a << 5U) | (a >> 27U);
            const std::uint32_t temp = rotated + function + e + constant + words[index];
            e = d; d = c; c = (b << 30U) | (b >> 2U); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    std::array<std::uint8_t, 20> digest{};
    const std::array<std::uint32_t, 5> hashes{h0, h1, h2, h3, h4};
    for (std::size_t index = 0; index < hashes.size(); ++index) {
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            digest[index * 4U + byte] = static_cast<std::uint8_t>(hashes[index] >> (24U - static_cast<std::uint32_t>(byte * 8U)));
        }
    }
    return digest;
}

std::string base64(const std::array<std::uint8_t, 20>& input) {
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    for (std::size_t index = 0; index < input.size(); index += 3U) {
        const std::uint32_t value = static_cast<std::uint32_t>(input[index]) << 16U |
            (index + 1U < input.size() ? static_cast<std::uint32_t>(input[index + 1U]) << 8U : 0U) |
            (index + 2U < input.size() ? static_cast<std::uint32_t>(input[index + 2U]) : 0U);
        output += alphabet[(value >> 18U) & 0x3FU];
        output += alphabet[(value >> 12U) & 0x3FU];
        output += index + 1U < input.size() ? alphabet[(value >> 6U) & 0x3FU] : '=';
        output += index + 2U < input.size() ? alphabet[value & 0x3FU] : '=';
    }
    return output;
}

bool sendWebSocketText(SocketHandle socket, std::string_view text) {
    if (text.size() > 65535U) return false;
    std::string frame;
    frame.reserve(text.size() + 4U);
    frame += static_cast<char>(0x81);
    if (text.size() < 126U) {
        frame += static_cast<char>(text.size());
    } else {
        frame += static_cast<char>(126);
        frame += static_cast<char>((text.size() >> 8U) & 0xFFU);
        frame += static_cast<char>(text.size() & 0xFFU);
    }
    frame.append(text);
    return sendAll(socket, frame);
}

std::vector<std::string> splitPath(std::string_view path) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const std::size_t end = path.find('/', begin);
        const std::string_view part = path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);
        if (!part.empty()) parts.emplace_back(part);
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return parts;
}

} // namespace

struct LocalControlServer::WebSocketClient {
    explicit WebSocketClient(Socket&& socket) : socket(std::move(socket)) {}
    Socket socket;
    std::mutex mutex;
};

LocalControlServer::LocalControlServer(std::filesystem::path configuration_path, LocalControlServerOptions options)
    : options_(options), control_service_(std::move(configuration_path)), runtime_(control_service_, this) {}

LocalControlServer::~LocalControlServer() {
    requestStop();
}

void LocalControlServer::requestStop() {
    stop_requested_ = true;
    listener_.reset();
}

void LocalControlServer::run() {
    Socket socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!socket.valid()) throw std::runtime_error("could not create local control socket (error " + std::to_string(lastSocketError()) + ")");
    const int reuse = 1;
    static_cast<void>(setSocketOption(socket.get(), SOL_SOCKET, SO_REUSEADDR, reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(options_.port);
    if (bind(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        throw std::runtime_error("could not bind local control service to 127.0.0.1:" + std::to_string(options_.port) + " (error " + std::to_string(lastSocketError()) + ")");
    }
    if (listen(socket.get(), SOMAXCONN) != 0 || !setSocketReceiveTimeout(socket.get(), 1)) {
        throw std::runtime_error("could not listen for local control requests (error " + std::to_string(lastSocketError()) + ")");
    }
    listener_ = std::move(socket);
    while (!stop_requested_) {
        sockaddr_in remote{};
        SocketAddressLength length = sizeof(remote);
        Socket client(accept(listener_.get(), reinterpret_cast<sockaddr*>(&remote), &length));
        if (!client.valid()) {
            if (stop_requested_ || isReceiveTimeout(lastSocketError())) continue;
            continue;
        }
#ifdef __APPLE__
        const int disable_sigpipe = 1;
        static_cast<void>(setSocketOption(client.get(), SOL_SOCKET, SO_NOSIGPIPE, disable_sigpipe));
#endif
        serveConnection(std::move(client));
    }
}

void LocalControlServer::serveConnection(Socket socket) {
    const std::optional<HttpRequest> request = receiveRequest(socket.get());
    if (!request.has_value()) {
        static_cast<void>(sendJson(socket.get(), 400, errorJson("invalid_request", "could not parse HTTP request")));
        return;
    }
    const std::string path = request->target.substr(0, request->target.find('?'));
    try {
        if (request->method == "GET" && (path == "/" || path == "/index.html")) {
            static_cast<void>(sendAsset(socket.get(), "text/html", web_assets::kIndexHtml));
            return;
        }
        if (request->method == "GET" && path == "/assets/styles.css") {
            static_cast<void>(sendAsset(socket.get(), "text/css", web_assets::kStylesCss));
            return;
        }
        if (request->method == "GET" && path == "/assets/app.js") {
            static_cast<void>(sendAsset(socket.get(), "application/javascript", web_assets::kAppJs));
            return;
        }
        if (request->method == "GET" && path == api::kHealthPath) {
            static_cast<void>(sendJson(socket.get(), 200, "{\"service\":\"dgramtunneler\",\"api_version\":\"v1\",\"ready\":true}"));
            return;
        }
        if (request->method == "GET" && path == api::kTunnelsPath) {
            std::ostringstream response;
            response << "{\"tunnels\":[";
            const auto tunnels = control_service_.listTunnels();
            for (std::size_t index = 0; index < tunnels.size(); ++index) {
                if (index != 0U) response << ',';
                const auto& tunnel = tunnels[index];
                response << "{\"alias\":\"" << jsonEscape(tunnel.alias) << "\",\"mode\":\"" << jsonEscape(tunnel.mode)
                         << "\",\"udp_destination\":\"" << jsonEscape(tunnel.udp_destination)
                         << "\",\"equivalent_direct_command\":\"" << jsonEscape(tunnel.equivalent_direct_command) << "\"}";
            }
            response << "]}";
            static_cast<void>(sendJson(socket.get(), 200, response.str()));
            return;
        }
        if (request->method == "GET" && path == api::kRuntimesPath) {
            std::ostringstream response;
            response << "{\"runtimes\":[";
            const auto snapshots = runtime_.snapshots();
            for (std::size_t index = 0; index < snapshots.size(); ++index) {
                if (index != 0U) response << ',';
                response << snapshotJson(snapshots[index]);
            }
            response << "]}";
            static_cast<void>(sendJson(socket.get(), 200, response.str()));
            return;
        }
        if (request->method == "GET" && path == api::kConfigPath) {
            static_cast<void>(sendJson(socket.get(), 200, "{\"toml\":\"" + jsonEscape(control_service_.configurationToml()) + "\"}"));
            return;
        }
        if (request->method == "PUT" && path == api::kConfigPath) {
            const auto toml = jsonStringProperty(request->body, "toml");
            if (!toml.has_value()) throw std::runtime_error("request must provide a JSON string property named 'toml'");
            control_service_.replaceConfiguration(*toml);
            static_cast<void>(sendJson(socket.get(), 200, "{\"updated\":true}"));
            return;
        }
        const auto upgrade = request->headers.find("upgrade");
        if (request->method == "GET" && path == api::kEventsPath && upgrade != request->headers.end() &&
            lower(upgrade->second) == "websocket") {
            const auto key = request->headers.find("sec-websocket-key");
            if (key == request->headers.end()) throw std::runtime_error("WebSocket request is missing Sec-WebSocket-Key");
            const std::string accept_key = base64(sha1(key->second + std::string(kWebSocketGuid)));
            const std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept_key + "\r\n\r\n";
            if (!sendAll(socket.get(), response)) return;
            auto client = std::make_shared<WebSocketClient>(std::move(socket));
            const auto snapshots = runtime_.snapshots();
            for (const auto& snapshot : snapshots) {
                ControlEvent event;
                event.kind = EventKind::Lifecycle;
                event.alias = snapshot.alias;
                event.message = "current runtime snapshot";
                event.snapshot = snapshot;
                if (!sendWebSocketText(client->socket.get(), eventJson(event))) return;
            }
            const std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.push_back(std::move(client));
            return;
        }

        const auto parts = splitPath(path);
        if (parts.size() >= 4U && parts[0] == "api" && parts[1] == "v1" && parts[2] == "tunnels") {
            const std::string& alias = parts[3];
            if (request->method == "GET" && parts.size() == 4U) {
                const NamedTunnel tunnel = control_service_.tunnel(alias);
                const auto summaries = control_service_.listTunnels();
                const auto summary = std::find_if(summaries.begin(), summaries.end(), [&](const TunnelSummary& item) { return item.alias == alias; });
                std::ostringstream response;
                response << "{\"alias\":\"" << jsonEscape(tunnel.alias) << "\",\"mode\":\""
                         << (tunnel.config.is_client_ ? "client" : "server") << "\"";
                if (summary != summaries.end()) response << ",\"udp_destination\":\"" << jsonEscape(summary->udp_destination) << "\"";
                response << "}";
                static_cast<void>(sendJson(socket.get(), 200, response.str()));
                return;
            }
            if (request->method == "POST" && parts.size() == 5U) {
                if (parts[4] == "start") runtime_.start(alias);
                else if (parts[4] == "stop") runtime_.stop(alias);
                else if (parts[4] == "restart") runtime_.restart(alias);
                else throw std::runtime_error("unknown tunnel action");
                static_cast<void>(sendJson(socket.get(), 202, "{\"accepted\":true}"));
                return;
            }
            if (request->method == "POST" && parts.size() == 6U && parts[4] == "producer") {
                DatagramProducer::Options options;
                if (parts[5] == "start" || parts[5] == "restart") {
                    if (const auto interval = jsonUnsignedProperty(request->body, "interval_milliseconds"); interval.has_value()) {
                        if (*interval == 0U || *interval > std::numeric_limits<unsigned int>::max()) {
                            throw std::runtime_error("interval_milliseconds must be between 1 and 4294967295");
                        }
                        options.interval_ms = static_cast<unsigned int>(*interval);
                    }
                    if (const auto count = jsonUnsignedProperty(request->body, "count"); count.has_value()) {
                        if (*count == 0U || *count > std::numeric_limits<std::size_t>::max()) {
                            throw std::runtime_error("count must be a positive integer");
                        }
                        options.count = static_cast<std::size_t>(*count);
                    }
                    if (const auto prefix = jsonStringProperty(request->body, "payload_prefix"); prefix.has_value()) options.payload_prefix = *prefix;
                }
                if (parts[5] == "start") runtime_.startProducer(alias, options);
                else if (parts[5] == "stop") runtime_.stopProducer(alias);
                else if (parts[5] == "restart") runtime_.restartProducer(alias, options);
                else throw std::runtime_error("unknown producer action");
                static_cast<void>(sendJson(socket.get(), 202, "{\"accepted\":true}"));
                return;
            }
        }
        static_cast<void>(sendJson(socket.get(), 404, errorJson("not_found", "unknown control API endpoint")));
    } catch (const std::exception& error) {
        static_cast<void>(sendJson(socket.get(), 400, errorJson("invalid_request", error.what())));
    }
}

void LocalControlServer::publish(const ControlEvent& event) {
    broadcastEvent(event);
}

void LocalControlServer::broadcastEvent(const ControlEvent& event) {
    const std::string message = eventJson(event);
    std::vector<std::shared_ptr<WebSocketClient>> clients;
    {
        const std::lock_guard<std::mutex> lock(clients_mutex_);
        clients = clients_;
    }
    std::vector<std::shared_ptr<WebSocketClient>> failed;
    for (const auto& client : clients) {
        const std::lock_guard<std::mutex> lock(client->mutex);
        if (!sendWebSocketText(client->socket.get(), message)) failed.push_back(client);
    }
    if (!failed.empty()) {
        const std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [&](const auto& client) {
            return std::find(failed.begin(), failed.end(), client) != failed.end();
        }), clients_.end());
    }
}

} // namespace control
