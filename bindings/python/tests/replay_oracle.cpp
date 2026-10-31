// SPDX-License-Identifier: Apache-2.0
//
// What the C++ connector says about a capture, as JSON, so that test_binding.py
// can hold the binding to the connector rather than to the C ABI it calls. Its
// field names are the binding's, and one step is one input record, with a last
// step for the flush.
//
//   motionConnectorPython_replay_oracle <source> <capture>
#include "motionConnectorMocopi/Connector.h"
#include "motionConnectorMocopi/PacketCapture.h"
#include "motionConnectorVmc/Connector.h"
#include "motionConnectorVmc/PacketCapture.h"
#include "motionConnectorVrchatOsc/Connector.h"
#include "motionConnectorVrchatOsc/PacketCapture.h"
#include "motionConnectorWebSocket/Connector.h"
#include "motionConnectorWebSocket/PacketCapture.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace {

namespace connectors = openstrata::connectors;
namespace core = connectors::core;
namespace motion = openstrata::motion;
using connectors::transport::PacketCapture;

void
Number(double value)
{
    if (std::isnan(value)) {
        std::fputs("NaN", stdout);
    } else if (std::isinf(value)) {
        std::fputs(value < 0 ? "-Infinity" : "Infinity", stdout);
    } else {
        std::printf("%.17g", value);
    }
}

void
Integer(std::uint64_t value)
{
    std::printf("%llu", static_cast<unsigned long long>(value));
}

void
Text(std::string_view value)
{
    std::putchar('"');
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            std::putchar('\\');
            std::putchar(c);
        } else if (byte < 0x20) {
            std::printf("\\u%04x", byte);
        } else {
            std::putchar(c);
        }
    }
    std::putchar('"');
}

void
Key(const char* name)
{
    std::printf("\"%s\":", name);
}

void
Null()
{
    std::fputs("null", stdout);
}

void
Vector(const pxr::GfVec3f& value)
{
    std::putchar('[');
    for (int index = 0; index < 3; ++index) {
        if (index != 0) {
            std::putchar(',');
        }
        Number(value[index]);
    }
    std::putchar(']');
}

void
Quaternion(const pxr::GfQuatf& value)
{
    std::putchar('[');
    Number(value.GetReal());
    for (int index = 0; index < 3; ++index) {
        std::putchar(',');
        Number(value.GetImaginary()[index]);
    }
    std::putchar(']');
}

template <class Value, class Write>
void
Optional(const std::optional<Value>& value, Write write)
{
    value ? write(*value) : Null();
}

void
Pose(const motion::MotionPose& pose)
{
    const motion::RootMotion& root = pose.root;
    const motion::SourceMetadata& metadata = pose.metadata;
    std::putchar('{');
    Key("source_kind");
    Integer(static_cast<std::uint64_t>(metadata.kind));
    std::fputs(",", stdout);
    Key("timestamp");
    Number(pose.timestamp);
    std::fputs(",", stdout);
    Key("root_position");
    root.hasPosition ? Vector(root.worldPosition) : Null();
    std::fputs(",", stdout);
    Key("root_orientation");
    root.hasOrientation ? Quaternion(root.worldOrientation) : Null();
    std::fputs(",", stdout);
    Key("root_linear_velocity");
    root.hasLinearVelocity ? Vector(root.linearVelocity) : Null();
    std::fputs(",", stdout);
    Key("root_angular_velocity");
    root.hasAngularVelocity ? Vector(root.angularVelocity) : Null();
    std::fputs(",", stdout);
    Key("look_at_target");
    Optional(pose.lookAtTarget, Vector);
    std::fputs(",", stdout);
    Key("local_rotations");
    std::putchar('[');
    for (std::size_t joint = 0; joint < motion::HumanJointCount; ++joint) {
        if (joint != 0) {
            std::putchar(',');
        }
        pose.validRotations.test(joint) ? Quaternion(pose.localRotations[joint]) : Null();
    }
    std::fputs("],", stdout);
    Key("confidence");
    if (pose.confidence) {
        std::putchar('[');
        for (std::size_t joint = 0; joint < motion::HumanJointCount; ++joint) {
            if (joint != 0) {
                std::putchar(',');
            }
            Number((*pose.confidence)[joint]);
        }
        std::putchar(']');
    } else {
        Null();
    }
    std::fputs(",", stdout);
    Key("left_foot_contact");
    pose.contacts ? Integer(static_cast<std::uint64_t>(pose.contacts->leftFoot)) : Null();
    std::fputs(",", stdout);
    Key("right_foot_contact");
    pose.contacts ? Integer(static_cast<std::uint64_t>(pose.contacts->rightFoot)) : Null();
    std::fputs(",", stdout);
    Key("channels");
    std::putchar('[');
    bool first = true;
    for (const auto& channel : pose.channels.entries) {
        if (!first) {
            std::putchar(',');
        }
        first = false;
        std::putchar('[');
        Text(channel.name);
        std::putchar(',');
        Number(channel.value);
        std::putchar(']');
    }
    std::fputs("],", stdout);
    Key("provider");
    Text(metadata.provider);
    std::fputs(",", stdout);
    Key("protocol");
    Text(metadata.protocol);
    std::fputs(",", stdout);
    Key("source_id");
    Text(metadata.sourceId);
    std::fputs(",", stdout);
    Key("metadata_source_timestamp");
    Optional(metadata.sourceTimestamp, Number);
    std::fputs(",", stdout);
    Key("metadata_sequence_number");
    Optional(metadata.sequenceNumber, [](std::uint64_t value) { Integer(value); });
    std::putchar('}');
}

void
Tracker(const core::TrackerObservation& tracker)
{
    std::putchar('{');
    Key("tracker_id");
    Text(tracker.trackerId);
    std::fputs(",", stdout);
    Key("position");
    tracker.hasPosition ? Vector(tracker.position) : Null();
    std::fputs(",", stdout);
    Key("rotation");
    tracker.hasRotation ? Quaternion(tracker.rotation) : Null();
    std::fputs(",", stdout);
    Key("confidence");
    Optional(tracker.confidence, Number);
    std::putchar('}');
}

void
Frame(const core::MotionFrame& frame)
{
    std::putchar('{');
    Key("frame_number");
    Integer(frame.frameNumber);
    std::fputs(",", stdout);
    Key("receive_timestamp");
    Number(frame.timing.receiveTimestamp);
    std::fputs(",", stdout);
    Key("source_timestamp");
    Optional(frame.timing.sourceTimestamp, Number);
    std::fputs(",", stdout);
    Key("sequence");
    Optional(frame.timing.sequence, [](std::uint64_t value) { Integer(value); });
    std::fputs(",", stdout);
    Key("source_clock");
    Integer(static_cast<std::uint64_t>(frame.timing.sourceClock));
    std::fputs(",", stdout);
    Key("source_profile");
    Text(frame.sourceProfile);
    std::fputs(",", stdout);
    Key("actors");
    std::putchar('[');
    bool firstActor = true;
    for (const core::ActorFrame& actor : frame.actors) {
        if (!firstActor) {
            std::putchar(',');
        }
        firstActor = false;
        std::putchar('{');
        Key("actor_id");
        Text(actor.actor);
        std::fputs(",", stdout);
        Key("pose");
        Optional(actor.pose, Pose);
        std::fputs(",", stdout);
        Key("trackers");
        std::putchar('[');
        bool firstTracker = true;
        for (const core::TrackerObservation& tracker : actor.trackers) {
            if (!firstTracker) {
                std::putchar(',');
            }
            firstTracker = false;
            Tracker(tracker);
        }
        std::fputs("]}", stdout);
    }
    std::fputs("]}", stdout);
}

template <class Diagnostic>
void
WriteDiagnostic(const Diagnostic& diagnostic)
{
    std::putchar('{');
    Key("code");
    Text(DiagnosticCodeString(diagnostic.code));
    std::fputs(",", stdout);
    Key("severity");
    Integer(static_cast<std::uint64_t>(diagnostic.severity));
    std::fputs(",", stdout);
    Key("recoverable");
    std::fputs(diagnostic.recoverable ? "true" : "false", stdout);
    std::fputs(",", stdout);
    Key("source");
    Text(diagnostic.source);
    std::fputs(",", stdout);
    Key("subject");
    Text(diagnostic.subject);
    std::fputs(",", stdout);
    Key("detail");
    Text(diagnostic.detail);
    std::fputs(",", stdout);
    Key("timestamp");
    Optional(diagnostic.timestamp, Number);
    std::fputs(",", stdout);
    Key("sequence");
    Optional(diagnostic.sequence, [](std::uint64_t value) { Integer(value); });
    std::putchar('}');
}

template <class Connector>
void
Step(Connector& connector)
{
    std::putchar('{');
    Key("frames");
    std::putchar('[');
    core::MotionFrame frame;
    bool first = true;
    while (connector.Poll(frame)) {
        if (!first) {
            std::putchar(',');
        }
        first = false;
        Frame(frame);
    }
    std::fputs("],", stdout);
    Key("diagnostics");
    std::putchar('[');
    first = true;
    for (const auto& diagnostic : connector.GetDiagnostics()) {
        if (!first) {
            std::putchar(',');
        }
        first = false;
        WriteDiagnostic(diagnostic);
    }
    connector.ClearDiagnostics();
    std::fputs("]}", stdout);
}

template <class Connector, class Push, class Flush>
int
Replay(const PacketCapture& capture, Push push, Flush flush)
{
    Connector connector;
    std::putchar('{');
    Key("steps");
    std::putchar('[');
    double last = 0.0;
    for (const auto& record : capture.datagrams) {
        last = record.receiveTime;
        push(connector, record);
        Step(connector);
        std::putchar(',');
    }
    flush(connector, last);
    Step(connector);
    std::fputs("],", stdout);
    const core::FrameBufferStats stats = connector.GetBufferStats();
    Key("pushed_frames");
    Integer(stats.pushedFrames);
    std::fputs(",", stdout);
    Key("polled_frames");
    Integer(stats.polledFrames);
    std::fputs(",", stdout);
    Key("dropped_frames");
    Integer(stats.droppedFrames);
    std::puts("}");
    return 0;
}

PacketCapture
Read(bool (*reader)(const std::string&, PacketCapture*, connectors::transport::PacketCaptureError*),
     const std::string& path, bool& ok)
{
    PacketCapture capture;
    connectors::transport::PacketCaptureError error;
    ok = reader(path, &capture, &error);
    if (!ok) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.message.c_str());
    }
    return capture;
}

} // namespace

int
main(int argc, char** argv)
{
    namespace vmc = connectors::vmc;
    namespace mocopi = connectors::mocopi;
    namespace vrchat = connectors::vrchatOsc;
    namespace ws = connectors::websocket;
    if (argc != 3) {
        std::fputs("usage: motionConnectorPython_replay_oracle <source> <capture>\n", stderr);
        return 2;
    }
    const std::string_view source = argv[1];
    const std::string path = argv[2];
    bool ok = false;
    if (source == "vmc") {
        const PacketCapture capture = Read(&vmc::ReadPacketCaptureFile, path, ok);
        return ok ? Replay<vmc::VmcConnector>(
                        capture,
                        [](vmc::VmcConnector& c, const auto& d) {
                            c.PushDatagram(d.bytes.data(), d.bytes.size(), d.receiveTime);
                        },
                        [](vmc::VmcConnector& c, double t) { c.Flush(t); })
                  : 1;
    }
    if (source == "mocopi") {
        const PacketCapture capture = Read(&mocopi::ReadPacketCaptureFile, path, ok);
        return ok ? Replay<mocopi::MocopiConnector>(
                        capture,
                        [](mocopi::MocopiConnector& c, const auto& d) {
                            c.PushDatagram(d.bytes.data(), d.bytes.size(), d.receiveTime);
                        },
                        [](mocopi::MocopiConnector&, double) {})
                  : 1;
    }
    if (source == "vrchat-osc") {
        const PacketCapture capture = Read(&vrchat::ReadPacketCaptureFile, path, ok);
        return ok ? Replay<vrchat::VrchatOscConnector>(
                        capture,
                        [](vrchat::VrchatOscConnector& c, const auto& d) {
                            c.PushDatagram(d.bytes.data(), d.bytes.size(), d.receiveTime, d.peer);
                        },
                        [](vrchat::VrchatOscConnector& c, double) { c.Flush(); })
                  : 1;
    }
    if (source == "websocket") {
        const PacketCapture capture = Read(&ws::ReadPacketCaptureFile, path, ok);
        return ok ? Replay<ws::WebSocketConnector>(
                        capture,
                        [](ws::WebSocketConnector& c, const auto& d) {
                            c.PushMessage(std::string_view(reinterpret_cast<const char*>(d.bytes.data()),
                                                           d.bytes.size()),
                                          d.receiveTime,
                                          d.peer);
                        },
                        [](ws::WebSocketConnector&, double) {})
                  : 1;
    }
    std::fprintf(stderr, "unknown source %s\n", argv[1]);
    return 2;
}
