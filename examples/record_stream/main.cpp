// SPDX-License-Identifier: Apache-2.0
//
// record_stream: a live or replayed source, recorded as motion traces.
//
//     connector -> MotionFrame -> LiveCaptureSource -> MotionRecorder -> trace
//     ^ motion-connectors       ^ usd-motion-plugins
//
// The connector libraries end at `MotionFrame`. Everything that gives a frame a
// temporal meaning is chosen here, in the consumer, and is marked "Consumer
// policy" below. A different consumer may choose differently without touching a
// connector.
#include "motionConnectorMocopi/Connector.h"
#include "motionConnectorVmc/Connector.h"
#include "motionConnectorWebSocket/Connector.h"
#include "motionConnectorWebSocket/PacketCapture.h"

#include "motionRecording/CaptureTrace.h"
#include "motionRecording/LiveCaptureSource.h"
#include "motionRecording/MotionRecorder.h"

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace core = openstrata::connectors::core;
namespace mocopi = openstrata::connectors::mocopi;
namespace motion = openstrata::motion;
namespace transport = openstrata::connectors::transport;
namespace vmc = openstrata::connectors::vmc;
namespace websocket = openstrata::connectors::websocket;

volatile std::sig_atomic_t interrupted = 0;

extern "C" void
OnInterrupt(int)
{
    interrupted = 1;
}

// One actor's path from connector poses to finished takes.
class ActorRecording {
public:
    explicit ActorRecording(std::size_t ordinal) : _ordinal(ordinal) {}

    void Push(const motion::MotionPose& pose)
    {
        ++_pushed;
        _RouteProvenance(pose.metadata);

        // Consumer policy: a take ends where the source's time goes back, the one
        // discontinuity the intake cannot admit. One trace is one clock, so the
        // new epoch starts a new take instead of being spliced onto the old one.
        // A restart whose clock happens to advance continues the take; the
        // connector's diagnostic still reports it.
        if (_newest && pose.timestamp < *_newest) {
            _CloseTake();
            _intake.Reset();
            _newest.reset();
        }
        if (!_intake.Push(pose)) {
            return; // Counted by the intake: empty, or a repeated timestamp.
        }
        _newest = pose.timestamp;

        // Consumer policy: evaluate on the source's clock, once for each accepted
        // pose, so a replay records the same trace as the session it came from.
        // A runtime ticking on its own clock calls `AlignClock` here instead.
        _recorder.Record(_intake.Sample(pose.timestamp));
    }

    void Finish() { _CloseTake(); }

    std::size_t GetOrdinal() const noexcept { return _ordinal; }
    std::uint64_t GetPushed() const noexcept { return _pushed; }
    const std::vector<motion::MotionClip>& GetTakes() const noexcept { return _takes; }
    const motion::LiveCaptureStats& GetIntakeStats() const noexcept { return _intake.GetStats(); }
    const motion::RecordReport& GetRecordReport() const noexcept { return _recorder.GetReport(); }

private:
    void _RouteProvenance(motion::SourceMetadata metadata)
    {
        // The stamp and the counter are each sample's, and the intake keeps them
        // per pose. Only the stream's identity is forwarded, and only on a change.
        metadata.sourceTimestamp.reset();
        metadata.sequenceNumber.reset();
        if (!_provenance || metadata != *_provenance) {
            _intake.SetSourceMetadata(metadata);
            _provenance = std::move(metadata);
        }
    }

    void _CloseTake()
    {
        motion::MotionClip clip = _recorder.Take();
        if (clip.samples.empty()) {
            return;
        }
        // The rate the trace's reader would otherwise measure, not a configured
        // one: this example does not resample, so the source's cadence is the rate.
        const double span = clip.endTime - clip.startTime;
        const std::size_t intervals = clip.samples.size() - 1;
        clip.nominalFrameRate =
            (span > 0.0 && intervals > 0) ? static_cast<double>(intervals) / span : 30.0;
        _takes.push_back(std::move(clip));
    }

    std::size_t _ordinal;
    std::uint64_t _pushed = 0;
    // Consumer policy: the intake's defaults, which hold a missing joint at its
    // last value and derive a missing root velocity. Both are counted in its stats.
    motion::LiveCaptureSource _intake;
    motion::MotionRecorder _recorder;
    std::optional<motion::SourceMetadata> _provenance;
    std::optional<double> _newest;
    std::vector<motion::MotionClip> _takes;
};

struct Session {
    std::map<core::ActorId, ActorRecording> actors;
    std::size_t frames = 0;
    std::size_t observationOnlyActors = 0;
};

void
Route(const core::MotionFrame& frame, Session* session)
{
    ++session->frames;
    for (const core::ActorFrame& actor : frame.actors) {
        // A tracker observation is not a pose. Solving one is a separate,
        // downstream decision, so there is nothing to record until that exists.
        if (!actor.pose) {
            ++session->observationOnlyActors;
            continue;
        }
        const std::size_t ordinal = session->actors.size();
        session->actors.try_emplace(actor.actor, ordinal).first->second.Push(*actor.pose);
    }
}

template <typename Connector>
void
Drain(Connector& connector, Session* session, std::size_t maxFrames = 0)
{
    core::MotionFrame frame;
    while ((maxFrames == 0 || session->frames < maxFrames) && connector.Poll(frame)) {
        Route(frame, session);
    }
    // Diagnostics are reported beside frames, never inside them.
    for (const auto& diagnostic : connector.GetDiagnostics()) {
        std::cerr << FormatDiagnostic(diagnostic) << '\n';
    }
    connector.ClearDiagnostics();
}

template <typename Connector>
bool
ReplayCapture(const std::string& path, Connector& connector, Session* session)
{
    transport::PacketCapture capture;
    transport::PacketCaptureError error;
    bool read = false;
    if constexpr (std::is_same_v<Connector, vmc::VmcConnector>) {
        read = vmc::ReadPacketCaptureFile(path, &capture, &error);
    } else if constexpr (std::is_same_v<Connector, mocopi::MocopiConnector>) {
        read = mocopi::ReadPacketCaptureFile(path, &capture, &error);
    } else {
        read = websocket::ReadPacketCaptureFile(path, &capture, &error);
    }
    if (!read) {
        std::cerr << "record_stream: " << path << ':' << error.line << ": " << error.message
                  << '\n';
        return false;
    }

    // The same connector and the same Poll as a live session, fed from the capture.
    double lastReceiveTime = 0.0;
    for (const transport::RecordedDatagram& datagram : capture.datagrams) {
        lastReceiveTime = datagram.receiveTime;
        if constexpr (requires { connector.PushMessage(std::string_view{}, 0.0, {}); }) {
            connector.PushMessage(
                std::string_view(reinterpret_cast<const char*>(datagram.bytes.data()),
                                 datagram.bytes.size()),
                datagram.receiveTime,
                datagram.peer);
        } else {
            connector.PushDatagram(
                datagram.bytes.data(), datagram.bytes.size(), datagram.receiveTime);
        }
        Drain(connector, session);
    }
    if constexpr (requires { connector.Flush(0.0); }) {
        connector.Flush(lastReceiveTime);
    }
    Drain(connector, session);
    return true;
}

struct Options {
    std::string source;
    std::string output;
    std::string capture;
    std::string listen = "127.0.0.1";
    std::optional<std::uint16_t> port;
    std::size_t maxFrames = 0;
    double duration = 0.0;
    bool liveOption = false;
};

template <typename Connector>
bool
RecordLive(const Options& options, std::uint16_t defaultPort, std::string_view profile,
           Connector& connector, Session* session)
{
    core::ConnectorConfig config;
    config.bindAddress = options.listen;
    config.port = options.port.value_or(defaultPort);
    config.sourceProfile = std::string(profile);
    config.bufferMode = core::BufferMode::Ordered;
    config.bufferCapacity = 64;
    const core::Status status = connector.Open(config);
    if (!status) {
        std::cerr << "record_stream: " << status.message << '\n';
        return false;
    }
    std::cout << "listen: " << connector.GetEndpoint() << std::endl;

    std::signal(SIGINT, OnInterrupt);
    const auto started = std::chrono::steady_clock::now();
    while (!interrupted && (options.maxFrames == 0 || session->frames < options.maxFrames)) {
        Drain(connector, session, options.maxFrames);
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
        if (options.duration != 0.0 && elapsed.count() >= options.duration) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    connector.Close();
    return true;
}

// An actor id is whatever the sender said, so a report spells it safely, and a
// file name never uses it.
std::string
Printable(std::string_view text)
{
    std::string printable;
    for (const char c : text) {
        printable.push_back(static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? '?' : c);
    }
    return printable;
}

bool
WriteTakes(const Session& session, const std::filesystem::path& directory)
{
    bool written = true;
    for (const auto& [actor, recording] : session.actors) {
        const motion::LiveCaptureStats& intake = recording.GetIntakeStats();
        const motion::RecordReport& report = recording.GetRecordReport();
        std::cout << "actor " << recording.GetOrdinal() << ": id=\"" << Printable(actor)
                  << "\" takes=" << recording.GetTakes().size()
                  << " accepted=" << intake.framesAccepted
                  << " refused=" << recording.GetPushed() - intake.framesAccepted
                  << " jointsHeld=" << intake.jointsHeld
                  << " velocitiesDerived=" << intake.rootVelocitiesDerived
                  << " sampled=" << report.sampled << " unavailable=" << report.unavailable << '\n';

        std::size_t take = 0;
        for (const motion::MotionClip& clip : recording.GetTakes()) {
            const std::filesystem::path path =
                directory / ("actor-" + std::to_string(recording.GetOrdinal()) + ".take-" +
                             std::to_string(++take) + ".trace");
            // The writer refuses before its first byte when a sender's value cannot
            // be spelled in the format, so a refused trace leaves no partial file.
            if (!motion::WriteCaptureTraceFile(path.string(), clip)) {
                std::cerr << "record_stream: could not write " << path.string() << '\n';
                written = false;
                continue;
            }
            std::cout << "trace: " << path.string() << " frames=" << clip.samples.size() << '\n';
        }
    }
    return written;
}

template <typename Connector>
int
Run(const Options& options, std::uint16_t defaultPort, std::string_view profile)
{
    // Before the source opens: an operator should not perform a take only to
    // learn at the end that it cannot be saved.
    const std::filesystem::path directory(options.output);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error || !std::filesystem::is_empty(directory, error) || error) {
        std::cerr << "record_stream: --output must name an empty or new directory: "
                  << options.output << '\n';
        return 1;
    }

    Connector connector;
    Session session;
    std::cout << "source: " << options.source << '\n';
    const bool ran = options.capture.empty()
                         ? RecordLive(options, defaultPort, profile, connector, &session)
                         : ReplayCapture(options.capture, connector, &session);
    if (!ran) {
        return 1;
    }

    for (auto& [actor, recording] : session.actors) {
        recording.Finish();
    }
    const core::FrameBufferStats buffer = connector.GetBufferStats();
    std::cout << "frames: " << session.frames << '\n'
              << "dropped: " << buffer.droppedFrames << '\n'
              << "observation-only actors: " << session.observationOnlyActors << '\n';
    const bool written = WriteTakes(session, directory);
    if (session.actors.empty()) {
        std::cerr << "record_stream: the source delivered no pose, so nothing was recorded\n";
        return 1;
    }
    return written ? 0 : 1;
}

void
PrintUsage(std::ostream& output)
{
    output << "usage: record_stream --source vmc|mocopi|websocket --output DIR\n"
              "                     (--capture FILE | [--listen ADDRESS] [--port N]\n"
              "                      [--duration SECONDS] [--max-frames N])\n"
              "--output is a new or empty directory; each actor's take becomes\n"
              "actor-<ordinal>.take-<number>.trace there. A live session runs until its\n"
              "limit or Ctrl-C. websocket has no default port.\n";
}

bool
ParseOptions(int argc, char** argv, Options* options, std::string* error)
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view flag = argv[index];
        if (index + 1 >= argc) {
            *error = std::string(flag) + " requires a value";
            return false;
        }
        const std::string value = argv[++index];
        try {
            std::size_t consumed = 0;
            if (flag == "--source") {
                options->source = value;
            } else if (flag == "--output") {
                options->output = value;
            } else if (flag == "--capture") {
                options->capture = value;
            } else if (flag == "--listen") {
                options->listen = value;
                options->liveOption = true;
            } else if (flag == "--port") {
                const unsigned long port = std::stoul(value, &consumed);
                if (consumed != value.size() || value.front() == '-' || port > 65535) {
                    *error = "--port takes 0..65535";
                    return false;
                }
                options->port = static_cast<std::uint16_t>(port);
                options->liveOption = true;
            } else if (flag == "--max-frames") {
                const unsigned long long frames = std::stoull(value, &consumed);
                if (consumed != value.size() || value.front() == '-') {
                    *error = "--max-frames takes a count";
                    return false;
                }
                options->maxFrames = static_cast<std::size_t>(frames);
                options->liveOption = true;
            } else if (flag == "--duration") {
                options->duration = std::stod(value, &consumed);
                if (consumed != value.size() || !std::isfinite(options->duration) ||
                    options->duration < 0.0) {
                    *error = "--duration takes finite, non-negative seconds";
                    return false;
                }
                options->liveOption = true;
            } else {
                *error = "unknown option " + std::string(flag);
                return false;
            }
        } catch (const std::exception&) {
            *error = std::string(flag) + " cannot read " + value;
            return false;
        }
    }
    if (options->source != "vmc" && options->source != "mocopi" && options->source != "websocket") {
        *error = "--source takes vmc, mocopi or websocket";
        return false;
    }
    if (options->output.empty()) {
        *error = "--output is required";
        return false;
    }
    if (!options->capture.empty() && options->liveOption) {
        *error = "--capture replays a file, so it takes no live option";
        return false;
    }
    if (options->capture.empty() && options->source == "websocket" && !options->port) {
        *error = "a live websocket source requires --port";
        return false;
    }
    return true;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        PrintUsage(std::cout);
        return 0;
    }
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, &options, &error)) {
        std::cerr << "record_stream: " << error << '\n';
        PrintUsage(std::cerr);
        return 2;
    }
    if (options.source == "vmc") {
        return Run<vmc::VmcConnector>(options, 39539, "vmc.v1");
    }
    if (options.source == "mocopi") {
        return Run<mocopi::MocopiConnector>(options, 12351, "mocopi.body.v1");
    }
    return Run<websocket::WebSocketConnector>(options, 0, "");
}
