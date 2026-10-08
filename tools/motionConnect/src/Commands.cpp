// SPDX-License-Identifier: Apache-2.0

#include "Commands.h"

#include "motionConnectorCore/Types.h"
#include "motionConnectorMocopi/Connector.h"
#include "motionConnectorMocopi/PacketCapture.h"
#include "motionConnectorTransport/PacketCapture.h"
#include "motionConnectorVmc/Connector.h"
#include "motionConnectorVmc/PacketCapture.h"
#include "motionConnectorVrchatOsc/Connector.h"
#include "motionConnectorVrchatOsc/PacketCapture.h"
#include "motionConnectorWebSocket/Connector.h"
#include "motionConnectorWebSocket/FrameSender.h"
#include "motionConnectorWebSocket/PacketCapture.h"

#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace motionConnectTool {
namespace {

using openstrata::connectors::core::BufferMode;
using openstrata::connectors::core::ConnectorCapabilities;
using openstrata::connectors::core::ConnectorCapability;
using openstrata::connectors::core::ConnectorConfig;
using openstrata::connectors::core::ConnectorState;
using openstrata::connectors::core::MotionFrame;
using openstrata::connectors::transport::PacketCapture;
using openstrata::connectors::transport::PacketCaptureError;
using openstrata::connectors::transport::RecordedDatagram;
namespace websocket = openstrata::connectors::websocket;

struct ConnectorInfo {
    std::string_view name;
    std::string_view profile;
    std::uint16_t port;
    ConnectorCapabilities capabilities;
};

volatile std::sig_atomic_t interrupted = 0;

extern "C" void
OnInterrupt(int)
{
    interrupted = 1;
}

std::array<ConnectorInfo, 4>
GetConnectors()
{
    openstrata::connectors::vmc::VmcConnector vmc;
    openstrata::connectors::mocopi::MocopiConnector mocopi;
    openstrata::connectors::vrchatOsc::VrchatOscConnector vrchatOsc;
    websocket::WebSocketConnector ws;
    return {{{"vmc", "vmc.v1", 39539, vmc.GetCapabilities()},
             {"mocopi", "mocopi.body.v1", 12351, mocopi.GetCapabilities()},
             {"vrchat-osc", "vrchat-osc.trackers.v1", 9001, vrchatOsc.GetCapabilities()},
             {"websocket", "(the sender's)", 0, ws.GetCapabilities()}}};
}

const ConnectorInfo&
GetConnectorInfo(Source source)
{
    static const auto connectors = GetConnectors();
    return connectors[static_cast<std::size_t>(source)];
}

bool
ParseSource(std::string_view value, Source* source)
{
    if (value == "vmc") {
        *source = Source::Vmc;
        return true;
    }
    if (value == "mocopi") {
        *source = Source::Mocopi;
        return true;
    }
    if (value == "vrchat-osc") {
        *source = Source::VrchatOsc;
        return true;
    }
    if (value == "websocket") {
        *source = Source::WebSocket;
        return true;
    }
    return false;
}

bool
TakeValue(int argc, char** argv, int* index, std::string_view flag, std::string* value,
          std::string* error)
{
    if (*index + 1 >= argc) {
        *error = std::string(flag) + " requires a value";
        return false;
    }
    *value = argv[++(*index)];
    return true;
}

bool
ParseCount(std::string_view text, std::size_t* value)
{
    if (text.empty() || text.front() == '-') {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(std::string(text), &consumed);
        if (consumed != text.size() ||
            parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
            return false;
        }
        *value = static_cast<std::size_t>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool
ParsePort(std::string_view text, std::uint16_t* value)
{
    std::size_t parsed = 0;
    if (!ParseCount(text, &parsed) || parsed > 65535) {
        return false;
    }
    *value = static_cast<std::uint16_t>(parsed);
    return true;
}

bool
ParseDuration(std::string_view text, double* value)
{
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(std::string(text), &consumed);
        if (consumed != text.size() || !std::isfinite(parsed) || parsed < 0.0) {
            return false;
        }
        *value = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void
PrintCapabilities(const ConnectorCapabilities& capabilities)
{
    constexpr std::array<std::pair<ConnectorCapability, std::string_view>, 10> known = {{
        {ConnectorCapability::Body, "body"},
        {ConnectorCapability::Hands, "hands"},
        {ConnectorCapability::Face, "face"},
        {ConnectorCapability::Eyes, "eyes"},
        {ConnectorCapability::RootMotion, "root-motion"},
        {ConnectorCapability::Trackers, "trackers"},
        {ConnectorCapability::Controllers, "controllers"},
        {ConnectorCapability::SourceTimestamps, "source-timestamps"},
        {ConnectorCapability::Confidence, "confidence"},
        {ConnectorCapability::MultipleActors, "multiple-actors"},
    }};

    bool first = true;
    for (const auto& capability : known) {
        if (!capabilities.Has(capability.first)) {
            continue;
        }
        if (!first) {
            std::cout << ", ";
        }
        std::cout << capability.second;
        first = false;
    }
    std::cout << '\n';
}

void
PrintFrame(const MotionFrame& frame)
{
    std::size_t poses = 0;
    std::size_t trackers = 0;
    for (const auto& actor : frame.actors) {
        poses += actor.pose.has_value() ? 1 : 0;
        trackers += actor.trackers.size();
    }
    std::cout << "frame " << frame.frameNumber << " profile=" << frame.sourceProfile
              << " receive=" << std::fixed << std::setprecision(6) << frame.timing.receiveTimestamp
              << " actors=" << frame.actors.size() << " poses=" << poses << " trackers=" << trackers
              << '\n';
}

template <typename Connector>
void
DrainFrames(Connector& connector, std::size_t* total, std::size_t maxFrames)
{
    MotionFrame frame;
    while ((maxFrames == 0 || *total < maxFrames) && connector.Poll(frame)) {
        PrintFrame(frame);
        ++(*total);
    }
}

bool
ReadCapture(Source source, const std::string& path, PacketCapture* capture,
            PacketCaptureError* error)
{
    switch (source) {
    case Source::Vmc:
        return openstrata::connectors::vmc::ReadPacketCaptureFile(path, capture, error);
    case Source::Mocopi:
        return openstrata::connectors::mocopi::ReadPacketCaptureFile(path, capture, error);
    case Source::VrchatOsc:
        return openstrata::connectors::vrchatOsc::ReadPacketCaptureFile(path, capture, error);
    case Source::WebSocket:
        return websocket::ReadPacketCaptureFile(path, capture, error);
    }
    return false;
}

void
FlushReplay(openstrata::connectors::vmc::VmcConnector& connector, double receiveTimestamp)
{
    connector.Flush(receiveTimestamp);
}

void
FlushReplay(openstrata::connectors::mocopi::MocopiConnector&, double)
{
}

void
FlushReplay(openstrata::connectors::vrchatOsc::VrchatOscConnector& connector, double)
{
    connector.Flush();
}

void
FlushReplay(websocket::WebSocketConnector&, double)
{
}

template <typename Connector>
void
PushReplay(Connector& connector, const RecordedDatagram& datagram)
{
    connector.PushDatagram(datagram.bytes.data(), datagram.bytes.size(), datagram.receiveTime);
}

void
PushReplay(openstrata::connectors::vrchatOsc::VrchatOscConnector& connector,
           const RecordedDatagram& datagram)
{
    connector.PushDatagram(
        datagram.bytes.data(), datagram.bytes.size(), datagram.receiveTime, datagram.peer);
}

void
PushReplay(websocket::WebSocketConnector& connector, const RecordedDatagram& datagram)
{
    connector.PushMessage(std::string_view(reinterpret_cast<const char*>(datagram.bytes.data()),
                                           datagram.bytes.size()),
                          datagram.receiveTime,
                          datagram.peer);
}

std::string_view
SourceLabel(Source source)
{
    return source == Source::WebSocket ? "websocket" : GetConnectorInfo(source).profile;
}

// The same concrete connector services live input and hardware-free replay.
template <typename Action>
int
WithSource(Source source, Action action)
{
    switch (source) {
    case Source::Vmc: {
        openstrata::connectors::vmc::VmcConnector connector;
        return action(connector);
    }
    case Source::Mocopi: {
        openstrata::connectors::mocopi::MocopiConnector connector;
        return action(connector);
    }
    case Source::VrchatOsc: {
        openstrata::connectors::vrchatOsc::VrchatOscConnector connector;
        return action(connector);
    }
    case Source::WebSocket: {
        websocket::WebSocketConnector connector;
        return action(connector);
    }
    }
    return 1;
}

template <typename Connector, typename Push>
int
InspectCapture(const PacketCapture& capture, Source source, Connector& connector, Push push)
{
    std::cout << "source: " << SourceLabel(source) << '\n';
    std::size_t frames = 0;
    double lastReceiveTimestamp = 0.0;
    for (const auto& datagram : capture.datagrams) {
        lastReceiveTimestamp = datagram.receiveTime;
        push(datagram);
        DrainFrames(connector, &frames, 0);
    }
    FlushReplay(connector, lastReceiveTimestamp);
    DrainFrames(connector, &frames, 0);
    std::cout << "frames: " << frames << '\n';
    return 0;
}

int
RunInspect(const Options& options)
{
    PacketCapture capture;
    PacketCaptureError error;
    if (!ReadCapture(options.source, options.capturePath, &capture, &error)) {
        std::cerr << "motion_connect: " << options.capturePath;
        if (error.line != 0) {
            std::cerr << ":" << error.line;
        }
        std::cerr << ": " << error.message << '\n';
        return 1;
    }

    return WithSource(options.source, [&](auto& connector) {
        return InspectCapture(capture, options.source, connector, [&](const auto& datagram) {
            PushReplay(connector, datagram);
        });
    });
}

ConnectorConfig
MakeDumpConfig(const Options& options)
{
    ConnectorConfig config;
    config.bindAddress = options.bindAddress;
    config.port = options.portSpecified ? options.port : GetConnectorInfo(options.source).port;
    if (options.source != Source::WebSocket) {
        config.sourceProfile = GetConnectorInfo(options.source).profile;
    }
    config.bufferMode = BufferMode::Ordered;
    config.bufferCapacity = 64;
    return config;
}

template <typename Connector>
void
PrintListen(Connector& connector)
{
    std::cout << "listen: " << connector.GetEndpoint() << std::endl;
}

template <typename Connector>
int
RunDump(const Options& options, Connector& connector)
{
    const auto status = connector.Open(MakeDumpConfig(options));
    if (!status) {
        std::cerr << "motion_connect: " << status.message << '\n';
        return 1;
    }

    interrupted = 0;
    std::signal(SIGINT, OnInterrupt);
    std::cout << "source: " << SourceLabel(options.source) << '\n';
    PrintListen(connector);
    const auto started = std::chrono::steady_clock::now();
    std::size_t frames = 0;
    while (!interrupted && (options.maxFrames == 0 || frames < options.maxFrames)) {
        DrainFrames(connector, &frames, options.maxFrames);
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (options.durationSeconds != 0.0 && elapsed >= options.durationSeconds) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    connector.Close();
    std::cout << "frames: " << frames << '\n';
    return 0;
}

int
RunDumpBySource(const Options& options)
{
    return WithSource(options.source, [&](auto& connector) { return RunDump(options, connector); });
}

template <typename Endpoint>
void
ReportDiagnostics(Endpoint& endpoint)
{
    for (const auto& diagnostic : endpoint.GetDiagnostics()) {
        std::cerr << FormatDiagnostic(diagnostic) << '\n';
    }
    endpoint.ClearDiagnostics();
}

std::string_view
CaptureMagic(Source source)
{
    switch (source) {
    case Source::Vmc:
        return openstrata::connectors::vmc::PacketCaptureMagic;
    case Source::Mocopi:
        return openstrata::connectors::mocopi::PacketCaptureMagic;
    case Source::VrchatOsc:
        return openstrata::connectors::vrchatOsc::PacketCaptureMagic;
    case Source::WebSocket:
        return websocket::PacketCaptureMagic;
    }
    return {};
}

template <typename Connector>
int
RunRecord(const Options& options, Connector& connector)
{
    const auto status = connector.Open(MakeDumpConfig(options));
    ReportDiagnostics(connector);
    if (!status) {
        std::cerr << "motion_connect: " << status.message << '\n';
        connector.Close();
        return 1;
    }
    // Discover output errors before waiting on a live source. Keep the file
    // open for this session; the existing writer owns every on-disk byte.
    std::ofstream output(options.output, std::ios::binary);
    if (!output) {
        std::cerr << "motion_connect: could not open packet capture '" << options.output << "'\n";
        connector.Close();
        return 1;
    }
    connector.StartCapture();
    interrupted = 0;
    std::signal(SIGINT, OnInterrupt);
    std::cout << "source: " << SourceLabel(options.source) << '\n'
              << "capture: " << options.output << '\n';
    PrintListen(connector);
    const auto started = std::chrono::steady_clock::now();
    std::size_t frames = 0;
    bool failed = false;
    while (!interrupted && (options.maxFrames == 0 || frames < options.maxFrames)) {
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (options.durationSeconds != 0.0 && elapsed >= options.durationSeconds) {
            break;
        }
        const auto recordsBefore = connector.GetCapture().datagrams.size();
        MotionFrame frame;
        const bool polled = connector.Poll(frame);
        if (polled) {
            ++frames;
        }
        ReportDiagnostics(connector);
        if (connector.GetState() == ConnectorState::Error) {
            failed = true;
            break;
        }
        // A false Poll can still have consumed a datagram that has not yet
        // completed a frame. Drain that traffic immediately; on Windows even
        // a 1 ms sleep can split a tracker's position/rotation burst.
        if (!polled && connector.GetCapture().datagrams.size() == recordsBefore) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    connector.StopCapture();
    connector.Close();
    ReportDiagnostics(connector);
    const auto& capture = connector.GetCapture();
    if (!openstrata::connectors::transport::WritePacketCapture(
            CaptureMagic(options.source), output, capture) ||
        !output.flush().good()) {
        std::cerr << "motion_connect: could not write packet capture '" << options.output << "'\n";
        return 1;
    }
    output.close();
    if (output.fail()) {
        std::cerr << "motion_connect: could not close packet capture '" << options.output << "'\n";
        return 1;
    }
    std::cout << "records: " << capture.datagrams.size() << '\n'
              << "frames: " << frames << std::endl;
    return failed ? 1 : 0;
}

std::string_view
StateName(ConnectorState state)
{
    switch (state) {
    case ConnectorState::Disconnected:
        return "disconnected";
    case ConnectorState::Connecting:
        return "connecting";
    case ConnectorState::Connected:
        return "connected";
    case ConnectorState::Degraded:
        return "degraded";
    case ConnectorState::Error:
        return "error";
    }
    return "error";
}

template <typename Connector>
int
RunBridge(const Options& options, Connector& connector)
{
    const bool replay = !options.capturePath.empty();
    PacketCapture capture;
    PacketCaptureError error;
    if (replay && !ReadCapture(options.source, options.capturePath, &capture, &error)) {
        std::cerr << "motion_connect: " << options.capturePath << ':' << error.line << ": "
                  << error.message << '\n';
        return 1;
    }
    if (!replay) {
        const auto status = connector.Open(MakeDumpConfig(options));
        ReportDiagnostics(connector);
        if (!status) {
            std::cerr << "motion_connect: " << status.message << '\n';
            connector.Close();
            return 1;
        }
    }

    websocket::WebSocketConfig config;
    config.role = options.wsConnectSpecified ? websocket::WebSocketRole::Connect
                                             : websocket::WebSocketRole::Listen;
    config.address = options.wsAddress;
    config.port = options.wsPort;
    config.path = options.wsPath;
    config.allowedOrigins = options.allowedOrigins;
    websocket::WebSocketFrameSender sender;
    if (!sender.Open(config)) {
        ReportDiagnostics(sender);
        std::cerr << "motion_connect: " << sender.GetLastError() << '\n';
        connector.Close();
        return 1;
    }

    interrupted = 0;
    std::signal(SIGINT, OnInterrupt);
    std::cout << "source: " << SourceLabel(options.source) << '\n'
              << "output: ws://" << sender.GetEndpoint() << config.path << std::endl;
    const auto started = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> replayStarted;
    std::optional<ConnectorState> lastState;
    std::set<std::string> peers;
    std::size_t frames = 0;
    std::size_t refused = 0;
    std::size_t record = 0;
    bool replayFinished = false;

    const auto stopped = [&]() {
        return interrupted || (options.maxFrames != 0 && frames >= options.maxFrames) ||
               (options.durationSeconds != 0.0 &&
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() >=
                    options.durationSeconds);
    };
    const auto reportSource = [&]() {
        const auto state = connector.GetState();
        if (!lastState || *lastState != state) {
            std::cout << "state: " << StateName(state) << std::endl;
            lastState = state;
        }
        ReportDiagnostics(connector);
    };
    const auto service = [&]() {
        sender.Service();
        std::set<std::string> current;
        for (const auto& peer : sender.GetPeerStats()) {
            current.insert(peer.endpoint);
            if (!peers.contains(peer.endpoint)) {
                std::cout << "peer: + " << peer.endpoint << std::endl;
            }
        }
        for (const auto& peer : peers) {
            if (!current.contains(peer)) {
                std::cout << "peer: - " << peer << std::endl;
            }
        }
        peers = std::move(current);
        ReportDiagnostics(sender);
    };
    const auto forward = [&]() {
        MotionFrame frame;
        while (!stopped() && connector.Poll(frame)) {
            ++frames;
            if (!sender.Send(frame)) {
                ++refused;
            }
            reportSource();
            service();
        }
        reportSource();
    };

    reportSource();
    while (!stopped() && !replayFinished) {
        // Service even while upstream is quiet or in Error: peers stay connected.
        service();
        if (!replay) {
            forward();
        } else {
            if (!replayStarted && sender.GetPeerCount() != 0) {
                replayStarted = std::chrono::steady_clock::now();
            }
            if (replayStarted) {
                const double elapsed =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - *replayStarted)
                        .count();
                while (!stopped() && record < capture.datagrams.size() &&
                       capture.datagrams[record].receiveTime -
                               capture.datagrams.front().receiveTime <=
                           elapsed) {
                    PushReplay(connector, capture.datagrams[record++]);
                    forward();
                }
                if (!stopped() && record == capture.datagrams.size()) {
                    FlushReplay(connector,
                                capture.datagrams.empty() ? 0.0
                                                          : capture.datagrams.back().receiveTime);
                    forward();
                    replayFinished = true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Service queued frames before close 1001, bounded even for a stalled peer.
    const auto drainStarted = std::chrono::steady_clock::now();
    do {
        service();
        bool queued = false;
        for (const auto& peer : sender.GetPeerStats()) {
            queued = queued || peer.queuedMessages != 0 || peer.pendingBytes != 0;
        }
        if (!queued) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() - drainStarted < std::chrono::seconds(1));
    sender.Close();
    for (const auto& peer : peers) {
        std::cout << "peer: - " << peer << std::endl;
    }
    connector.Close();
    ReportDiagnostics(sender);
    reportSource();
    std::cout << "frames: " << frames << '\n' << "refused: " << refused << std::endl;
    return 0;
}

} // namespace

bool
ParseOptions(int argc, char** argv, Options* options, bool* showHelp, std::string* error)
{
    *showHelp = false;
    if (argc < 2) {
        *error = "a command is required";
        return false;
    }

    const std::string_view command = argv[1];
    if (command == "list") {
        options->command = Command::List;
    } else if (command == "dump") {
        options->command = Command::Dump;
    } else if (command == "inspect") {
        options->command = Command::Inspect;
    } else if (command == "record") {
        options->command = Command::Record;
    } else if (command == "bridge") {
        options->command = Command::Bridge;
    } else if (command == "--help" || command == "-h") {
        *showHelp = true;
        return true;
    } else {
        *error = "unknown command '" + std::string(command) + "'";
        return false;
    }

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            *showHelp = true;
            return true;
        }

        std::string value;
        if (argument == "--source") {
            if (!TakeValue(argc, argv, &index, argument, &value, error)) {
                return false;
            }
            if (!ParseSource(value, &options->source)) {
                *error = "--source expects vmc, mocopi, vrchat-osc, or websocket";
                return false;
            }
            options->sourceSpecified = true;
        } else if (argument == "--capture") {
            if (!TakeValue(argc, argv, &index, argument, &options->capturePath, error)) {
                return false;
            }
        } else if (argument == "--listen") {
            options->listenSpecified = true;
            if (!TakeValue(argc, argv, &index, argument, &options->bindAddress, error)) {
                return false;
            }
        } else if (argument == "--port") {
            if (!TakeValue(argc, argv, &index, argument, &value, error)) {
                return false;
            }
            if (!ParsePort(value, &options->port)) {
                *error = "--port expects a number between 0 and 65535";
                return false;
            }
            options->portSpecified = true;
        } else if (argument == "--max-frames") {
            if (!TakeValue(argc, argv, &index, argument, &value, error)) {
                return false;
            }
            if (!ParseCount(value, &options->maxFrames)) {
                *error = "--max-frames expects a whole number";
                return false;
            }
        } else if (argument == "--duration") {
            if (!TakeValue(argc, argv, &index, argument, &value, error)) {
                return false;
            }
            if (!ParseDuration(value, &options->durationSeconds)) {
                *error = "--duration expects a non-negative number";
                return false;
            }
        } else if (argument == "--output" || argument == "--ws-listen" ||
                   argument == "--ws-connect" || argument == "--ws-port" ||
                   argument == "--ws-path" || argument == "--allow-origin") {
            if (argument != "--output")
                options->wsOptionsSpecified = true;
            if (!TakeValue(argc, argv, &index, argument, &value, error)) {
                return false;
            }
            if (argument == "--output") {
                options->output = value;
                options->outputSpecified = true;
            } else if (argument == "--ws-listen" || argument == "--ws-connect") {
                options->wsAddress = value;
                if (argument == "--ws-listen")
                    options->wsListenSpecified = true;
                else
                    options->wsConnectSpecified = true;
            } else if (argument == "--ws-port") {
                if (!ParsePort(value, &options->wsPort)) {
                    *error = "--ws-port expects a number between 0 and 65535";
                    return false;
                }
                options->wsPortSpecified = true;
            } else if (argument == "--ws-path") {
                options->wsPath = value;
            } else {
                options->allowedOrigins.push_back(value);
            }
        } else {
            *error = "unknown option '" + std::string(argument) + "'";
            return false;
        }
    }

    if (options->command == Command::List) {
        if (argc != 2) {
            *error = "list takes no options";
            return false;
        }
    } else if (!options->sourceSpecified) {
        *error = "--source is required for dump, inspect, bridge, and record";
        return false;
    } else if (options->command == Command::Inspect && options->capturePath.empty()) {
        *error = "--capture is required for inspect";
        return false;
    } else if (options->command == Command::Dump && !options->capturePath.empty()) {
        *error = "--capture is only valid for inspect and bridge";
        return false;
    }
    if (options->command == Command::Record) {
        if (!options->outputSpecified || options->output.empty()) {
            *error = "--output is required for record";
            return false;
        }
        if (!options->capturePath.empty()) {
            *error = "--capture is only valid for inspect and bridge";
            return false;
        }
    } else if (options->command != Command::Bridge && options->outputSpecified) {
        *error = "--output is only valid for bridge and record";
        return false;
    }
    if (options->command != Command::Bridge && options->wsOptionsSpecified) {
        *error = "WebSocket output options are only valid for bridge";
        return false;
    }
    if (options->command == Command::Bridge) {
        if (!options->wsPortSpecified) {
            *error = "--ws-port is required for bridge";
            return false;
        }
        if (options->wsListenSpecified && options->wsConnectSpecified) {
            *error = "--ws-listen and --ws-connect cannot be combined";
            return false;
        }
        if (options->wsConnectSpecified && options->wsPort == 0) {
            *error = "--ws-connect requires a nonzero --ws-port";
            return false;
        }
        if (options->output != "websocket") {
            *error = "--output expects websocket";
            return false;
        }
        if (!options->capturePath.empty() && (options->listenSpecified || options->portSpecified)) {
            *error = "--capture cannot be combined with --listen or --port";
            return false;
        }
    }
    if (options->command != Command::List && options->command != Command::Inspect &&
        options->source == Source::WebSocket &&
        (options->command == Command::Record || options->capturePath.empty()) &&
        !options->portSpecified) {
        *error = "--port is required for a live websocket source";
        return false;
    }
    return true;
}

void
PrintUsage(std::ostream& output)
{
    output << "motion_connect - inspect the shared motion connector contract\n\n"
              "Usage:\n"
              "  motion_connect list\n"
              "  motion_connect dump --source SOURCE [options]\n"
              "  motion_connect inspect --source SOURCE --capture PATH\n"
              "  motion_connect bridge --source SOURCE --ws-port N [options]\n"
              "  motion_connect record --source SOURCE --output PATH [options]\n\n"
              "Sources: vmc, mocopi, vrchat-osc, websocket\n\n"
              "Source and stop options:\n"
              "  --listen ADDR          Bind address (default 127.0.0.1).\n"
              "  --port N               Listen port (required for websocket; 0 chooses).\n"
              "  --capture PATH         Inspect or bridge a capture instead of listening.\n"
              "  --max-frames N         Stop after N frames; 0 means unlimited.\n"
              "  --duration S           Stop after S seconds; 0 means unlimited.\n"
              "Record output options:\n"
              "  --output PATH          Save raw input in the source packet-capture format.\n"
              "Bridge output options:\n"
              "  --output websocket     Output format (default websocket).\n"
              "  --ws-listen ADDR       Serve peers (default 127.0.0.1).\n"
              "  --ws-connect ADDR      Connect to a peer; excludes --ws-listen.\n"
              "  --ws-port N            Required; 0 is allowed when listening.\n"
              "  --ws-path PATH         Request target (default /).\n"
              "  --allow-origin ORIGIN  Allow a browser origin; repeatable, none by default.\n"
              "  -h, --help             Show this message.\n";
}

int
Run(const Options& options)
{
    if (options.command == Command::List) {
        std::cout << "connectors:\n";
        for (const ConnectorInfo& connector : GetConnectors()) {
            std::cout << "  " << connector.name << '\n'
                      << "    profile: " << connector.profile << '\n'
                      << "    capabilities: ";
            PrintCapabilities(connector.capabilities);
        }
        return 0;
    }
    if (options.command == Command::Inspect) {
        return RunInspect(options);
    }
    if (options.command == Command::Bridge) {
        return WithSource(options.source,
                          [&](auto& connector) { return RunBridge(options, connector); });
    }
    if (options.command == Command::Record) {
        return WithSource(options.source,
                          [&](auto& connector) { return RunRecord(options, connector); });
    }
    return RunDumpBySource(options);
}

} // namespace motionConnectTool
