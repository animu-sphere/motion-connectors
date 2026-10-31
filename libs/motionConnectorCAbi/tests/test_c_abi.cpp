// SPDX-License-Identifier: Apache-2.0
//
// The ABI against the C++ connector it wraps: the same capture replayed through
// both must read as the same values, field by field, frame by frame, diagnostic
// by diagnostic. Then what is the boundary's own: refusals, struct sizes,
// lifetimes, strings and the UDP path. Only `_loopback` binds a socket.
//
// argv[1] is the corpus root (libs/); with "--loopback" it runs that path alone.
#include "motionConnectorCAbi/MotionConnector.h"

#include "motionConnectorMocopi/Connector.h"
#include "motionConnectorMocopi/PacketCapture.h"
#include "motionConnectorVmc/Connector.h"
#include "motionConnectorVmc/PacketCapture.h"
#include "motionConnectorVrchatOsc/Connector.h"
#include "motionConnectorVrchatOsc/PacketCapture.h"
#include "motionConnectorWebSocket/Connector.h"
#include "motionConnectorWebSocket/PacketCapture.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace connectors = openstrata::connectors;
namespace core = connectors::core;
namespace motion = openstrata::motion;
using connectors::transport::PacketCapture;

std::string_view
Text(motion_string_view_t view)
{
    assert(view.data != nullptr);
    assert(view.data[view.size] == '\0');
    return {view.data, static_cast<std::size_t>(view.size)};
}

void
CheckVector(const float (&actual)[3], const pxr::GfVec3f& expected)
{
    for (int index = 0; index < 3; ++index) {
        assert(actual[index] == expected[index]);
    }
}

void
CheckQuaternion(const float (&actual)[4], const pxr::GfQuatf& expected)
{
    assert(actual[0] == expected.GetReal());
    for (int index = 0; index < 3; ++index) {
        assert(actual[index + 1] == expected.GetImaginary()[index]);
    }
}

template <std::size_t Size>
void
CheckZero(const float (&actual)[Size])
{
    for (std::size_t index = 0; index < Size; ++index) {
        assert(actual[index] == 0.0f);
    }
}

void
CheckPose(const motion_frame_t* frame, std::uint64_t actorIndex, const motion::MotionPose& expected)
{
    MOTION_INIT(motion_pose_view_t, pose);
    assert(motion_frame_get_pose(frame, actorIndex, &pose) == MOTION_STATUS_OK);
    assert(pose.timestamp == expected.timestamp);
    assert(pose.source_kind == static_cast<std::uint32_t>(expected.metadata.kind));

    const motion::RootMotion& root = expected.root;
    assert((pose.has_root_position != 0) == root.hasPosition);
    assert((pose.has_root_orientation != 0) == root.hasOrientation);
    assert((pose.has_root_linear_velocity != 0) == root.hasLinearVelocity);
    assert((pose.has_root_angular_velocity != 0) == root.hasAngularVelocity);
    root.hasPosition ? CheckVector(pose.root_world_position, root.worldPosition)
                     : CheckZero(pose.root_world_position);
    root.hasOrientation ? CheckQuaternion(pose.root_world_orientation, root.worldOrientation)
                        : CheckZero(pose.root_world_orientation);
    root.hasLinearVelocity ? CheckVector(pose.root_linear_velocity, root.linearVelocity)
                           : CheckZero(pose.root_linear_velocity);
    root.hasAngularVelocity ? CheckVector(pose.root_angular_velocity, root.angularVelocity)
                            : CheckZero(pose.root_angular_velocity);

    assert(pose.joint_count == motion::HumanJointCount);
    for (std::size_t joint = 0; joint < motion::HumanJointCount; ++joint) {
        const bool valid = expected.validRotations.test(joint);
        assert((pose.valid_rotations[joint] != 0) == valid);
        // An absent joint is absent, never identity (CONNECTOR_CONTRACT §3).
        valid ? CheckQuaternion(pose.local_rotations[joint], expected.localRotations[joint])
              : CheckZero(pose.local_rotations[joint]);
    }
    assert((pose.confidence != nullptr) == expected.confidence.has_value());
    if (expected.confidence) {
        for (std::size_t joint = 0; joint < motion::HumanJointCount; ++joint) {
            assert(pose.confidence[joint] == (*expected.confidence)[joint]);
        }
    }

    assert((pose.has_look_at_target != 0) == expected.lookAtTarget.has_value());
    expected.lookAtTarget ? CheckVector(pose.look_at_target, *expected.lookAtTarget)
                          : CheckZero(pose.look_at_target);
    assert((pose.has_contacts != 0) == expected.contacts.has_value());
    if (expected.contacts) {
        assert(pose.left_foot_contact == static_cast<std::uint8_t>(expected.contacts->leftFoot));
        assert(pose.right_foot_contact == static_cast<std::uint8_t>(expected.contacts->rightFoot));
    }

    const motion::SourceMetadata& metadata = expected.metadata;
    assert(Text(pose.provider) == metadata.provider);
    assert(Text(pose.protocol) == metadata.protocol);
    assert(Text(pose.source_id) == metadata.sourceId);
    assert((pose.has_metadata_source_timestamp != 0) == metadata.sourceTimestamp.has_value());
    assert(pose.metadata_source_timestamp == metadata.sourceTimestamp.value_or(0.0));
    assert((pose.has_metadata_sequence_number != 0) == metadata.sequenceNumber.has_value());
    assert(pose.metadata_sequence_number == metadata.sequenceNumber.value_or(0));

    const auto& channels = expected.channels.entries;
    for (std::size_t index = 0; index < channels.size(); ++index) {
        MOTION_INIT(motion_channel_view_t, channel);
        assert(motion_frame_get_channel(frame, actorIndex, index, &channel) == MOTION_STATUS_OK);
        assert(Text(channel.name) == channels[index].name);
        assert(channel.value == channels[index].value);
    }
    MOTION_INIT(motion_channel_view_t, past);
    assert(motion_frame_get_channel(frame, actorIndex, channels.size(), &past) ==
           MOTION_STATUS_OUT_OF_RANGE);
}

void
CheckFrame(const motion_frame_t* frame, const core::MotionFrame& expected)
{
    MOTION_INIT(motion_frame_info_t, info);
    assert(motion_frame_get_info(frame, &info) == MOTION_STATUS_OK);
    assert(info.struct_size == sizeof(motion_frame_info_t));
    assert(info.frame_number == expected.frameNumber);
    assert(Text(info.source_profile) == expected.sourceProfile);
    assert(info.receive_timestamp == expected.timing.receiveTimestamp);
    assert((info.has_source_timestamp != 0) == expected.timing.sourceTimestamp.has_value());
    assert(info.source_timestamp == expected.timing.sourceTimestamp.value_or(0.0));
    assert((info.has_sequence != 0) == expected.timing.sequence.has_value());
    assert(info.sequence == expected.timing.sequence.value_or(0));
    assert(info.source_clock == static_cast<std::uint32_t>(expected.timing.sourceClock));
    assert(info.actor_count == expected.actors.size());

    for (std::size_t a = 0; a < expected.actors.size(); ++a) {
        const core::ActorFrame& actor = expected.actors[a];
        MOTION_INIT(motion_actor_view_t, view);
        assert(motion_frame_get_actor(frame, a, &view) == MOTION_STATUS_OK);
        assert(Text(view.actor_id) == actor.actor);
        assert(view.tracker_count == actor.trackers.size());
        assert((view.has_pose != 0) == actor.pose.has_value());
        if (actor.pose) {
            assert(view.channel_count == actor.pose->channels.entries.size());
            CheckPose(frame, a, *actor.pose);
        } else {
            MOTION_INIT(motion_pose_view_t, none);
            assert(motion_frame_get_pose(frame, a, &none) == MOTION_STATUS_NOT_AVAILABLE);
            assert(view.channel_count == 0);
        }
        for (std::size_t t = 0; t < actor.trackers.size(); ++t) {
            const core::TrackerObservation& tracker = actor.trackers[t];
            MOTION_INIT(motion_tracker_view_t, observed);
            assert(motion_frame_get_tracker(frame, a, t, &observed) == MOTION_STATUS_OK);
            assert(Text(observed.tracker_id) == tracker.trackerId);
            assert((observed.has_position != 0) == tracker.hasPosition);
            assert((observed.has_rotation != 0) == tracker.hasRotation);
            tracker.hasPosition ? CheckVector(observed.position, tracker.position)
                                : CheckZero(observed.position);
            tracker.hasRotation ? CheckQuaternion(observed.rotation, tracker.rotation)
                                : CheckZero(observed.rotation);
            assert((observed.has_confidence != 0) == tracker.confidence.has_value());
            assert(observed.confidence == tracker.confidence.value_or(0.0f));
        }
        MOTION_INIT(motion_tracker_view_t, past);
        assert(motion_frame_get_tracker(frame, a, actor.trackers.size(), &past) ==
               MOTION_STATUS_OUT_OF_RANGE);
    }
    MOTION_INIT(motion_actor_view_t, past);
    assert(motion_frame_get_actor(frame, expected.actors.size(), &past) ==
           MOTION_STATUS_OUT_OF_RANGE);
}

// The diagnostic the ABI delivers next must be `expected`, with its code named
// by the connector's own table. Replayed text here is already UTF-8.
template <class Diagnostic>
void
CheckDiagnostic(motion_connector_t* connector, const Diagnostic& expected)
{
    motion_diagnostic_t* diagnostic = nullptr;
    assert(motion_connector_poll_diagnostic(connector, &diagnostic) == MOTION_STATUS_OK);
    MOTION_INIT(motion_diagnostic_view_t, view);
    assert(motion_diagnostic_get_view(diagnostic, &view) == MOTION_STATUS_OK);
    assert(Text(view.code) == DiagnosticCodeString(expected.code));
    assert(view.severity == static_cast<std::uint32_t>(expected.severity));
    assert((view.recoverable != 0) == expected.recoverable);
    assert(Text(view.source) == expected.source);
    assert(Text(view.subject) == expected.subject);
    assert(Text(view.detail) == expected.detail);
    assert((view.has_timestamp != 0) == expected.timestamp.has_value());
    assert(view.timestamp == expected.timestamp.value_or(0.0));
    assert((view.has_sequence != 0) == expected.sequence.has_value());
    assert(view.sequence == expected.sequence.value_or(0));
    motion_diagnostic_release(diagnostic);
}

struct ReplayCounts {
    std::size_t frames = 0;
    std::size_t diagnostics = 0;
};

// Drains both after one input, comparing as it goes.
template <class Connector>
void
DrainAndCompare(Connector& direct, motion_connector_t* abi, ReplayCounts& counts)
{
    core::MotionFrame expected;
    while (direct.Poll(expected)) {
        motion_frame_t* frame = nullptr;
        assert(motion_connector_poll(abi, &frame) == MOTION_STATUS_OK);
        CheckFrame(frame, expected);
        motion_frame_release(frame);
        ++counts.frames;
    }
    motion_frame_t* none = reinterpret_cast<motion_frame_t*>(&counts);
    assert(motion_connector_poll(abi, &none) == MOTION_STATUS_EMPTY);
    assert(none == nullptr);

    for (const auto& diagnostic : direct.GetDiagnostics()) {
        CheckDiagnostic(abi, diagnostic);
        ++counts.diagnostics;
    }
    direct.ClearDiagnostics();
    motion_diagnostic_t* noDiagnostic = nullptr;
    assert(motion_connector_poll_diagnostic(abi, &noDiagnostic) == MOTION_STATUS_EMPTY);
    assert(noDiagnostic == nullptr);
}

template <class Connector, class PushDirect, class FlushDirect>
ReplayCounts
Replay(const char* source, const PacketCapture& capture, PushDirect pushDirect,
       FlushDirect flushDirect)
{
    Connector direct;
    motion_connector_t* abi = nullptr;
    assert(motion_connector_create(source, &abi) == MOTION_STATUS_OK);
    ReplayCounts counts;
    double last = 0.0;
    for (const auto& datagram : capture.datagrams) {
        last = datagram.receiveTime;
        pushDirect(direct, datagram);
        assert(motion_connector_push_input(abi,
                                           datagram.bytes.data(),
                                           datagram.bytes.size(),
                                           datagram.receiveTime,
                                           datagram.peer.c_str()) == MOTION_STATUS_OK);
        DrainAndCompare(direct, abi, counts);
    }
    flushDirect(direct, last);
    assert(motion_connector_flush_input(abi, last) == MOTION_STATUS_OK);
    DrainAndCompare(direct, abi, counts);

    const core::FrameBufferStats expected = direct.GetBufferStats();
    MOTION_INIT(motion_connector_stats_t, stats);
    assert(motion_connector_get_stats(abi, &stats) == MOTION_STATUS_OK);
    assert(stats.pushed_frames == expected.pushedFrames);
    assert(stats.polled_frames == expected.polledFrames);
    assert(stats.dropped_frames == expected.droppedFrames);
    assert(stats.refused_frames == 0);
    motion_connector_destroy(abi);
    return counts;
}

PacketCapture
Read(bool (*reader)(const std::string&, PacketCapture*, connectors::transport::PacketCaptureError*),
     const std::string& path)
{
    PacketCapture capture;
    connectors::transport::PacketCaptureError error;
    if (!reader(path, &capture, &error)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.message.c_str());
        assert(false);
    }
    return capture;
}

void
TestReplayParity(const std::string& libs)
{
    namespace vmc = connectors::vmc;
    namespace mocopi = connectors::mocopi;
    namespace vrchat = connectors::vrchatOsc;
    namespace ws = connectors::websocket;
    const auto pushVmc = [](vmc::VmcConnector& c, const auto& d) {
        c.PushDatagram(d.bytes.data(), d.bytes.size(), d.receiveTime);
    };
    const auto flushVmc = [](vmc::VmcConnector& c, double t) { c.Flush(t); };
    const auto pushMocopi = [](mocopi::MocopiConnector& c, const auto& d) {
        c.PushDatagram(d.bytes.data(), d.bytes.size(), d.receiveTime);
    };
    const auto flushMocopi = [](mocopi::MocopiConnector&, double) {};
    const auto pushVrchat = [](vrchat::VrchatOscConnector& c, const auto& d) {
        c.PushDatagram(d.bytes.data(), d.bytes.size(), d.receiveTime, d.peer);
    };
    const auto flushVrchat = [](vrchat::VrchatOscConnector& c, double) { c.Flush(); };
    const auto pushWs = [](ws::WebSocketConnector& c, const auto& d) {
        c.PushMessage(
            std::string_view(reinterpret_cast<const char*>(d.bytes.data()), d.bytes.size()),
            d.receiveTime,
            d.peer);
    };
    const auto flushWs = [](ws::WebSocketConnector&, double) {};

    // A capture with frames and a capture with refusals, for each source.
    ReplayCounts total;
    const auto add = [&](ReplayCounts counts) {
        total.frames += counts.frames;
        total.diagnostics += counts.diagnostics;
        return counts;
    };
    const std::string vmcDir = libs + "/motionConnectorVmc/tests/corpus/";
    assert(add(Replay<vmc::VmcConnector>(
                   "vmc",
                   Read(&vmc::ReadPacketCaptureFile, vmcDir + "mixed-traffic-30hz.vmcpackets"),
                   pushVmc,
                   flushVmc))
               .frames > 0);
    const std::string mocopiDir = libs + "/motionConnectorMocopi/tests/corpus/";
    assert(
        add(Replay<mocopi::MocopiConnector>("mocopi",
                                            Read(&mocopi::ReadPacketCaptureFile,
                                                 mocopiDir + "neutral-standing-60hz.mocopipackets"),
                                            pushMocopi,
                                            flushMocopi))
            .frames > 0);
    assert(add(Replay<mocopi::MocopiConnector>("mocopi",
                                               Read(&mocopi::ReadPacketCaptureFile,
                                                    mocopiDir + "malformed-packets.mocopipackets"),
                                               pushMocopi,
                                               flushMocopi))
               .diagnostics > 0);
    const std::string vrchatDir = libs + "/motionConnectorVrchatOsc/tests/corpus/generated/";
    assert(
        add(Replay<vrchat::VrchatOscConnector>(
                "vrchat-osc",
                Read(&vrchat::ReadPacketCaptureFile, vrchatDir + "eight-trackers.vrchatoscpackets"),
                pushVrchat,
                flushVrchat))
            .frames > 0);
    assert(add(Replay<vrchat::VrchatOscConnector>(
                   "vrchat-osc",
                   Read(&vrchat::ReadPacketCaptureFile,
                        vrchatDir + "malformed-packets.vrchatoscpackets"),
                   pushVrchat,
                   flushVrchat))
               .diagnostics > 0);
    const std::string wsDir = libs + "/motionConnectorWebSocket/tests/corpus/messages/";
    assert(add(Replay<ws::WebSocketConnector>(
                   "websocket",
                   Read(&ws::ReadPacketCaptureFile, wsDir + "steady-60hz.websocketpackets"),
                   pushWs,
                   flushWs))
               .frames > 0);
    assert(add(Replay<ws::WebSocketConnector>("websocket",
                                              Read(&ws::ReadPacketCaptureFile,
                                                   wsDir + "trackers-any-profile.websocketpackets"),
                                              pushWs,
                                              flushWs))
               .frames > 0);
    assert(add(Replay<ws::WebSocketConnector>(
                   "websocket",
                   Read(&ws::ReadPacketCaptureFile, wsDir + "codec-refusals.websocketpackets"),
                   pushWs,
                   flushWs))
               .diagnostics > 0);
    std::printf(
        "replay parity: %zu frame(s), %zu diagnostic(s)\n", total.frames, total.diagnostics);
}

// A single OSC message: address, type tags, arguments, each padded to 4 bytes.
void
AppendOscString(std::vector<std::uint8_t>& out, std::string_view text)
{
    out.insert(out.end(), text.begin(), text.end());
    out.push_back(0);
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
}

void
AppendOscFloat(std::vector<std::uint8_t>& out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(bits >> shift));
    }
}

std::vector<std::uint8_t>
VmcBlendMessage(std::string_view name, float value)
{
    std::vector<std::uint8_t> out;
    AppendOscString(out, "/VMC/Ext/Blend/Val");
    AppendOscString(out, ",sf");
    AppendOscString(out, name);
    AppendOscFloat(out, value);
    return out;
}

std::vector<std::uint8_t>
VmcTimeMessage(float seconds)
{
    std::vector<std::uint8_t> out;
    AppendOscString(out, "/VMC/Ext/T");
    AppendOscString(out, ",f");
    AppendOscFloat(out, seconds);
    return out;
}

std::vector<std::uint8_t>
VmcBoneMessage()
{
    std::vector<std::uint8_t> out;
    AppendOscString(out, "/VMC/Ext/Bone/Pos");
    AppendOscString(out, ",sfffffff");
    AppendOscString(out, "Hips");
    for (const float value : {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}) {
        AppendOscFloat(out, value);
    }
    return out;
}

void
PushBytes(motion_connector_t* connector, const std::vector<std::uint8_t>& bytes, double time)
{
    assert(motion_connector_push_input(connector, bytes.data(), bytes.size(), time, nullptr) ==
           MOTION_STATUS_OK);
}

// C_ABI §7: a frame whose identity is not UTF-8 is refused, whole and counted,
// and a diagnostic's text is repaired so the report survives.
void
TestUtf8Boundary()
{
    motion_connector_t* connector = nullptr;
    assert(motion_connector_create("vmc", &connector) == MOTION_STATUS_OK);

    PushBytes(connector, VmcBoneMessage(), 0.1);
    PushBytes(connector, VmcBlendMessage("\xFF\xFEname", 0.5f), 0.1);
    PushBytes(connector, VmcTimeMessage(1.0f), 0.1);
    assert(motion_connector_flush_input(connector, 0.1) == MOTION_STATUS_OK);
    motion_frame_t* refused = nullptr;
    assert(motion_connector_poll(connector, &refused) == MOTION_STATUS_EMPTY);
    assert(refused == nullptr);

    MOTION_INIT(motion_connector_stats_t, stats);
    assert(motion_connector_get_stats(connector, &stats) == MOTION_STATUS_OK);
    assert(stats.refused_frames == 1);

    bool found = false;
    motion_diagnostic_t* diagnostic = nullptr;
    while (motion_connector_poll_diagnostic(connector, &diagnostic) == MOTION_STATUS_OK) {
        MOTION_INIT(motion_diagnostic_view_t, view);
        assert(motion_diagnostic_get_view(diagnostic, &view) == MOTION_STATUS_OK);
        if (Text(view.code) == "C_ABI_FRAME_NOT_UTF8") {
            found = true;
            assert(view.severity == MOTION_SEVERITY_WARNING);
            assert(view.recoverable != 0);
            assert(Text(view.subject) == "actors[0].pose.channels[0].name");
            assert(view.has_sequence != 0);
        }
        motion_diagnostic_release(diagnostic);
    }
    assert(found);

    // Only that frame: the session continues, and the next one is delivered.
    PushBytes(connector, VmcBoneMessage(), 0.2);
    PushBytes(connector, VmcBlendMessage("Joy", 0.5f), 0.2);
    PushBytes(connector, VmcTimeMessage(2.0f), 0.2);
    assert(motion_connector_flush_input(connector, 0.2) == MOTION_STATUS_OK);
    motion_frame_t* frame = nullptr;
    assert(motion_connector_poll(connector, &frame) == MOTION_STATUS_OK);
    MOTION_INIT(motion_channel_view_t, channel);
    assert(motion_frame_get_channel(frame, 0, 0, &channel) == MOTION_STATUS_OK);
    assert(Text(channel.name) == "vmc:Joy");
    assert(channel.value == 0.5f);
    motion_frame_release(frame);
    assert(motion_connector_get_stats(connector, &stats) == MOTION_STATUS_OK);
    assert(stats.refused_frames == 1);

    // An unknown bone's raw bytes are quoted in a diagnostic, repaired there.
    std::vector<std::uint8_t> badBone;
    AppendOscString(badBone, "/VMC/Ext/Bone/Pos");
    AppendOscString(badBone, ",sfffffff");
    AppendOscString(badBone, "\xE3\x81");
    for (const float value : {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}) {
        AppendOscFloat(badBone, value);
    }
    PushBytes(connector, badBone, 0.3);
    bool repaired = false;
    while (motion_connector_poll_diagnostic(connector, &diagnostic) == MOTION_STATUS_OK) {
        MOTION_INIT(motion_diagnostic_view_t, view);
        assert(motion_diagnostic_get_view(diagnostic, &view) == MOTION_STATUS_OK);
        if (Text(view.subject).find("\xEF\xBF\xBD") != std::string_view::npos) {
            repaired = true;
        }
        motion_diagnostic_release(diagnostic);
    }
    assert(repaired);
    motion_connector_destroy(connector);
}

const char WebSocketFrame[] =
    R"({"format":"openstrata.motion.frame/v1","frameNumber":"7","sourceProfile":"test.v1",)"
    R"("timing":{"receiveTimestamp":1.5,"sourceClock":"none"},"actors":[{"actor":"a",)"
    R"("trackers":[{"trackerId":"t\u0000x","position":[1,2,3],"confidence":0.25}]}]})";

const std::vector<std::uint8_t>&
WebSocketFrameBytes()
{
    static const std::vector<std::uint8_t> bytes(WebSocketFrame,
                                                 WebSocketFrame + sizeof(WebSocketFrame) - 1);
    return bytes;
}

// Every pose field the corpus does not reach, set, through both paths.
void
TestFullPose()
{
    const std::string_view message =
        R"({"format":"openstrata.motion.frame/v1","frameNumber":"3","sourceProfile":"test.v1",)"
        R"("timing":{"sourceTimestamp":2.5,"receiveTimestamp":1.5,"sequence":"9",)"
        R"("sourceClock":"wall"},"actors":[{"actor":"a","pose":{"timestamp":2.5,"root":{)"
        R"("worldPosition":[1,2,3],"worldOrientation":[0,1,0,0],"linearVelocity":[4,5,6],)"
        R"("angularVelocity":[7,8,9]},"joints":{"hips":[0,0,1,0],"head":[0,0,0,1]},)"
        R"("confidence":{"hips":0.25,"head":0.75},"contacts":{"leftFoot":"contact",)"
        R"("rightFoot":"free"},"lookAtTarget":[0.5,1.5,2.5],"channels":{"a":1.5,"b":-0.5},)"
        R"("metadata":{"kind":"generated","provider":"p","protocol":"q","sourceId":"s",)"
        R"("sourceTimestamp":2.25,"sequenceNumber":"18446744073709551615"}},"trackers":[]}]})";

    connectors::websocket::WebSocketConnector direct;
    assert(direct.PushMessage(message, 1.5) == 1);
    core::MotionFrame expected;
    assert(direct.Poll(expected));
    const motion::MotionPose& pose = *expected.actors.at(0).pose;
    // The comparison below is only worth what it compares.
    assert(pose.confidence && pose.contacts && pose.lookAtTarget && pose.root.hasAngularVelocity &&
           pose.metadata.sequenceNumber == std::numeric_limits<std::uint64_t>::max());

    motion_connector_t* connector = nullptr;
    assert(motion_connector_create("websocket", &connector) == MOTION_STATUS_OK);
    assert(motion_connector_push_input(connector,
                                       reinterpret_cast<const std::uint8_t*>(message.data()),
                                       message.size(),
                                       1.5,
                                       nullptr) == MOTION_STATUS_OK);
    motion_frame_t* frame = nullptr;
    assert(motion_connector_poll(connector, &frame) == MOTION_STATUS_OK);
    CheckFrame(frame, expected);

    // Spelled out once, so a mapping and its comparison cannot share a mistake.
    MOTION_INIT(motion_pose_view_t, view);
    assert(motion_frame_get_pose(frame, 0, &view) == MOTION_STATUS_OK);
    assert(view.root_world_orientation[0] == 0.0f && view.root_world_orientation[1] == 1.0f);
    assert(view.valid_rotations[0] != 0 && view.local_rotations[0][2] == 1.0f);
    assert(view.confidence[0] == 0.25f);
    assert(view.left_foot_contact == MOTION_FOOT_CONTACT_IN_CONTACT);
    assert(view.right_foot_contact == MOTION_FOOT_CONTACT_NOT_IN_CONTACT);
    assert(view.source_kind == MOTION_SOURCE_KIND_GENERATED);
    assert(view.look_at_target[2] == 2.5f);
    motion_frame_release(frame);
    motion_connector_destroy(connector);
}

// A frame owns its data: it outlives the next poll, and the connector.
void
TestLifetimes()
{
    motion_connector_t* connector = nullptr;
    assert(motion_connector_create("websocket", &connector) == MOTION_STATUS_OK);
    PushBytes(connector, WebSocketFrameBytes(), 1.5);
    motion_frame_t* first = nullptr;
    assert(motion_connector_poll(connector, &first) == MOTION_STATUS_OK);
    MOTION_INIT(motion_tracker_view_t, tracker);
    assert(motion_frame_get_tracker(first, 0, 0, &tracker) == MOTION_STATUS_OK);
    const std::string_view id = Text(tracker.tracker_id);
    // A value may contain NUL; `size` is the length, not strlen.
    assert(id == std::string_view("t\0x", 3));
    assert(tracker.has_position != 0 && tracker.has_rotation == 0 && tracker.has_confidence != 0);
    assert(tracker.confidence == 0.25f);
    CheckZero(tracker.rotation);

    PushBytes(connector, WebSocketFrameBytes(), 1.6);
    motion_frame_t* second = nullptr;
    assert(motion_connector_poll(connector, &second) == MOTION_STATUS_OK);

    motion_connector_destroy(connector);
    assert(id == std::string_view("t\0x", 3));
    MOTION_INIT(motion_frame_info_t, info);
    assert(motion_frame_get_info(first, &info) == MOTION_STATUS_OK);
    assert(info.frame_number == 1);
    assert(info.has_source_timestamp == 0 && info.has_sequence == 0);
    assert(info.source_clock == MOTION_CLOCK_NONE);
    assert(Text(info.source_profile) == "test.v1");
    motion_frame_release(first);
    assert(motion_frame_get_info(second, &info) == MOTION_STATUS_OK);
    assert(info.frame_number == 2);
    motion_frame_release(second);

    // Null is ignored on every release.
    motion_frame_release(nullptr);
    motion_diagnostic_release(nullptr);
    motion_connector_destroy(nullptr);
}

void
TestStructSizes()
{
    motion_connector_t* connector = nullptr;
    assert(motion_connector_create("websocket", &connector) == MOTION_STATUS_OK);
    PushBytes(connector, WebSocketFrameBytes(), 1.5);
    motion_frame_t* frame = nullptr;
    assert(motion_connector_poll(connector, &frame) == MOTION_STATUS_OK);

    // Too tooSmall: refused, and nothing is written past what the caller stated.
    MOTION_INIT(motion_frame_info_t, tooSmall);
    tooSmall.struct_size = sizeof(motion_frame_info_t) - 1;
    tooSmall.frame_number = 99;
    assert(motion_frame_get_info(frame, &tooSmall) == MOTION_STATUS_UNSUPPORTED_STRUCT);
    assert(tooSmall.frame_number == 99);
    tooSmall.struct_size = 0;
    assert(motion_frame_get_info(frame, &tooSmall) == MOTION_STATUS_UNSUPPORTED_STRUCT);

    // A newer caller's larger output struct: what this library knows is
    // written, the rest is left alone, and struct_size says how much.
    struct Larger {
        motion_frame_info_t known;
        std::uint64_t future;
    } larger = {};
    larger.known.struct_size = sizeof(Larger);
    larger.future = 0x1234;
    assert(motion_frame_get_info(frame, &larger.known) == MOTION_STATUS_OK);
    assert(larger.known.struct_size == sizeof(motion_frame_info_t));
    assert(larger.known.frame_number == 1);
    assert(larger.future == 0x1234);
    motion_frame_release(frame);

    // Input: exact, either way: a field this library does not know could not
    // be honoured.
    MOTION_INIT(motion_connector_config_t, config);
    config.buffer_capacity = 1;
    config.struct_size = sizeof(config) - 8;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_UNSUPPORTED_STRUCT);
    config.struct_size = sizeof(config) + 8;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_UNSUPPORTED_STRUCT);
    motion_string_view_t error{};
    assert(motion_connector_last_error(connector, &error) == MOTION_STATUS_OK);
    assert(!Text(error).empty());

    MOTION_INIT(motion_websocket_options_t, websocket);
    websocket.struct_size = 8;
    MOTION_INIT(motion_connector_config_t, withOptions);
    withOptions.buffer_capacity = 1;
    withOptions.websocket = &websocket;
    assert(motion_connector_open(connector, &withOptions) == MOTION_STATUS_UNSUPPORTED_STRUCT);
    motion_connector_destroy(connector);
}

void
TestRefusals()
{
    assert(motion_abi_version() == MOTION_ABI_VERSION);
    assert(MOTION_ABI_VERSION >> 16 == 1);
    assert(std::string_view(motion_status_name(MOTION_STATUS_EMPTY)) == "MOTION_STATUS_EMPTY");
    assert(std::string_view(motion_status_name(12345)).empty());

    assert(motion_joint_count() == motion::HumanJointCount);
    assert(motion_joint_vocabulary_version() == motion::HumanJointVocabularyVersion);
    motion_string_view_t name{};
    assert(motion_joint_name(0, &name) == MOTION_STATUS_OK);
    assert(Text(name) == motion::HumanJointName(motion::HumanJoint::Hips));
    assert(motion_joint_name(motion::HumanJointCount, &name) == MOTION_STATUS_OUT_OF_RANGE);
    assert(motion_joint_name(0, nullptr) == MOTION_STATUS_INVALID_ARGUMENT);

    assert(motion_source_count() == 4);
    connectors::vmc::VmcConnector directVmc;
    for (std::uint64_t index = 0; index < motion_source_count(); ++index) {
        MOTION_INIT(motion_source_info_t, info);
        assert(motion_source_get_info(index, &info) == MOTION_STATUS_OK);
        motion_connector_t* connector = nullptr;
        assert(motion_connector_create(info.id.data, &connector) == MOTION_STATUS_OK);
        motion_capabilities_t capabilities = 0;
        assert(motion_connector_get_capabilities(connector, &capabilities) == MOTION_STATUS_OK);
        assert(capabilities == info.capabilities);
        motion_string_view_t profile{};
        assert(motion_connector_get_source_profile(connector, &profile) == MOTION_STATUS_OK);
        assert(Text(profile) == Text(info.default_profile));
        motion_connector_state_t state = MOTION_STATE_ERROR;
        assert(motion_connector_get_state(connector, &state) == MOTION_STATUS_OK);
        assert(state == MOTION_STATE_DISCONNECTED);
        motion_string_view_t endpoint{};
        assert(motion_connector_get_endpoint(connector, &endpoint) == MOTION_STATUS_OK);
        assert(Text(endpoint).empty());
        if (Text(info.id) == "vmc") {
            assert(capabilities == directVmc.GetCapabilities().Bits());
        }
        motion_connector_destroy(connector);
    }
    MOTION_INIT(motion_source_info_t, past);
    assert(motion_source_get_info(motion_source_count(), &past) == MOTION_STATUS_OUT_OF_RANGE);

    motion_connector_t* connector = reinterpret_cast<motion_connector_t*>(&past);
    assert(motion_connector_create("no-such-source", &connector) == MOTION_STATUS_UNKNOWN_SOURCE);
    assert(connector == nullptr);
    // OpenXR's handles are the caller's; it is not a version 1 source.
    assert(motion_connector_create("openxr", &connector) == MOTION_STATUS_UNKNOWN_SOURCE);
    assert(motion_connector_create(nullptr, &connector) == MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_create("vmc", nullptr) == MOTION_STATUS_INVALID_ARGUMENT);

    // Null handles and outputs fail plainly, never crash.
    motion_connector_state_t state = 0;
    motion_frame_t* frame = nullptr;
    motion_diagnostic_t* diagnostic = nullptr;
    motion_string_view_t view{};
    MOTION_INIT(motion_connector_stats_t, stats);
    MOTION_INIT(motion_connector_config_t, config);
    assert(motion_connector_open(nullptr, &config) == MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_close(nullptr) == MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_get_state(nullptr, &state) == MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_get_stats(nullptr, &stats) == MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_get_endpoint(nullptr, &view) == MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_last_error(nullptr, &view) == MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_poll(nullptr, &frame) == MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_poll_diagnostic(nullptr, &diagnostic) ==
           MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_push_input(nullptr, nullptr, 0, 0.0, nullptr) ==
           MOTION_STATUS_INVALID_ARGUMENT);
    MOTION_INIT(motion_frame_info_t, info);
    assert(motion_frame_get_info(nullptr, &info) == MOTION_STATUS_INVALID_ARGUMENT);

    assert(motion_connector_create("vmc", &connector) == MOTION_STATUS_OK);
    assert(motion_connector_poll(connector, nullptr) == MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_open(connector, nullptr) == MOTION_STATUS_INVALID_ARGUMENT);
    const std::uint8_t byte = 0;
    assert(motion_connector_push_input(connector, nullptr, 1, 0.0, nullptr) ==
           MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_push_input(
               connector, &byte, 1, std::numeric_limits<double>::infinity(), nullptr) ==
           MOTION_STATUS_INVALID_ARGUMENT);
    assert(motion_connector_flush_input(connector, std::numeric_limits<double>::quiet_NaN()) ==
           MOTION_STATUS_INVALID_ARGUMENT);

    // Configuration refusals, none of which touch the connector.
    config.buffer_capacity = 0;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_INVALID_ARGUMENT);
    config.buffer_capacity = 1;
    config.buffer_mode = 3;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_INVALID_ARGUMENT);
    config.buffer_mode = MOTION_BUFFER_LATEST;
    config.address = "\xFF";
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_INVALID_ARGUMENT);
    config.address = nullptr;
    MOTION_INIT(motion_websocket_options_t, websocket);
    config.websocket = &websocket;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_INVALID_ARGUMENT);
    config.websocket = nullptr;
    assert(motion_connector_get_state(connector, &state) == MOTION_STATUS_OK);
    assert(state == MOTION_STATE_DISCONNECTED);

    // A connector's own refusal is a state, with a reason, and it can retry.
    config.source_profile = "not-the-profile";
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_OPEN_FAILED);
    assert(motion_connector_get_state(connector, &state) == MOTION_STATUS_OK);
    assert(state == MOTION_STATE_ERROR);
    assert(motion_connector_last_error(connector, &view) == MOTION_STATUS_OK);
    assert(!Text(view).empty());
    assert(motion_connector_close(connector) == MOTION_STATUS_OK);
    assert(motion_connector_close(connector) == MOTION_STATUS_OK);
    assert(motion_connector_last_error(connector, &view) == MOTION_STATUS_OK);
    assert(Text(view).empty());
    motion_connector_destroy(connector);

    // WebSocket option refusals.
    assert(motion_connector_create("websocket", &connector) == MOTION_STATUS_OK);
    config = motion_connector_config_t{};
    config.struct_size = sizeof(config);
    config.buffer_capacity = 1;
    config.websocket = &websocket;
    websocket.role = 2;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_INVALID_ARGUMENT);
    websocket.role = MOTION_WEBSOCKET_LISTEN;
    websocket.silence_timeout_seconds = -1.0;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_INVALID_ARGUMENT);
    websocket.silence_timeout_seconds = 0.0;
    websocket.allowed_origin_count = 1;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_INVALID_ARGUMENT);
    const char* origins[] = {nullptr};
    websocket.allowed_origins = origins;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_INVALID_ARGUMENT);
    websocket.allowed_origin_count = 0;
    websocket.allowed_origins = nullptr;
    // A connect role refuses port 0, which is the connector's rule, passed up.
    websocket.role = MOTION_WEBSOCKET_CONNECT;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_OPEN_FAILED);
    motion_connector_destroy(connector);
}

// The bound on undelivered diagnostics: oldest dropped, and counted.
void
TestDiagnosticBound()
{
    motion_connector_t* connector = nullptr;
    assert(motion_connector_create("websocket", &connector) == MOTION_STATUS_OK);
    MOTION_INIT(motion_connector_config_t, config);
    config.buffer_capacity = 1;
    config.diagnostic_capacity = 2;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_OK);
    motion_string_view_t endpoint{};
    assert(motion_connector_get_endpoint(connector, &endpoint) == MOTION_STATUS_OK);
    assert(!Text(endpoint).empty());
    const std::vector<std::uint8_t> refused = {'{', '{', '{'};
    for (int index = 0; index < 5; ++index) {
        PushBytes(connector, refused, static_cast<double>(index));
    }
    MOTION_INIT(motion_connector_stats_t, stats);
    assert(motion_connector_get_stats(connector, &stats) == MOTION_STATUS_OK);
    assert(stats.dropped_diagnostics == 3);
    motion_diagnostic_t* diagnostic = nullptr;
    int delivered = 0;
    while (motion_connector_poll_diagnostic(connector, &diagnostic) == MOTION_STATUS_OK) {
        motion_diagnostic_release(diagnostic);
        ++delivered;
    }
    assert(delivered == 2);

    // Close keeps what has not been delivered, and discards pending frames.
    PushBytes(connector, refused, 6.0);
    PushBytes(connector, WebSocketFrameBytes(), 7.0);
    assert(motion_connector_close(connector) == MOTION_STATUS_OK);
    motion_frame_t* frame = nullptr;
    assert(motion_connector_poll(connector, &frame) == MOTION_STATUS_EMPTY);
    assert(motion_connector_poll_diagnostic(connector, &diagnostic) == MOTION_STATUS_OK);
    motion_diagnostic_release(diagnostic);
    motion_connector_destroy(connector);
}

// The live path: a real socket on loopback, on an OS-chosen port.
void
TestLoopback(const std::string& libs)
{
#ifdef _WIN32
    WSADATA wsa;
    assert(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
#endif
    motion_connector_t* connector = nullptr;
    assert(motion_connector_create("vmc", &connector) == MOTION_STATUS_OK);
    MOTION_INIT(motion_connector_config_t, config);
    config.buffer_mode = MOTION_BUFFER_ORDERED;
    config.buffer_capacity = 64;
    assert(motion_connector_open(connector, &config) == MOTION_STATUS_OK);
    motion_connector_state_t state = MOTION_STATE_ERROR;
    assert(motion_connector_get_state(connector, &state) == MOTION_STATUS_OK);
    assert(state == MOTION_STATE_CONNECTING);

    motion_string_view_t endpoint{};
    assert(motion_connector_get_endpoint(connector, &endpoint) == MOTION_STATUS_OK);
    const std::string text(Text(endpoint));
    const std::size_t colon = text.rfind(':');
    assert(colon != std::string::npos);

    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(static_cast<std::uint16_t>(std::stoi(text.substr(colon + 1))));
    assert(inet_pton(AF_INET, text.substr(0, colon).c_str(), &peer.sin_addr) == 1);
    const auto handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    const PacketCapture capture =
        Read(&connectors::vmc::ReadPacketCaptureFile,
             libs + "/motionConnectorVmc/tests/corpus/neutral-standing-30hz.vmcpackets");
    std::size_t frames = 0;
    for (const auto& datagram : capture.datagrams) {
        const auto sent = sendto(handle,
                                 reinterpret_cast<const char*>(datagram.bytes.data()),
                                 static_cast<int>(datagram.bytes.size()),
                                 0,
                                 reinterpret_cast<const sockaddr*>(&peer),
                                 sizeof(peer));
        assert(sent == static_cast<decltype(sent)>(datagram.bytes.size()));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        motion_frame_t* frame = nullptr;
        motion_status_t status = MOTION_STATUS_EMPTY;
        // One datagram is one poll here; a frame may close on a later one.
        while (status == MOTION_STATUS_EMPTY && frames == 0 &&
               std::chrono::steady_clock::now() < deadline &&
               &datagram == &capture.datagrams.back()) {
            status = motion_connector_poll(connector, &frame);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (status == MOTION_STATUS_EMPTY) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            status = motion_connector_poll(connector, &frame);
        }
        if (status == MOTION_STATUS_OK) {
            MOTION_INIT(motion_frame_info_t, info);
            assert(motion_frame_get_info(frame, &info) == MOTION_STATUS_OK);
            assert(Text(info.source_profile) == "vmc.v1");
            motion_frame_release(frame);
            ++frames;
        }
    }
    assert(frames > 0);
    assert(motion_connector_get_state(connector, &state) == MOTION_STATUS_OK);
    assert(state == MOTION_STATE_CONNECTED || state == MOTION_STATE_DEGRADED);
#ifdef _WIN32
    closesocket(handle);
#else
    close(handle);
#endif
    assert(motion_connector_close(connector) == MOTION_STATUS_OK);
    assert(motion_connector_get_state(connector, &state) == MOTION_STATUS_OK);
    assert(state == MOTION_STATE_DISCONNECTED);
    motion_connector_destroy(connector);
#ifdef _WIN32
    WSACleanup();
#endif
    std::printf("loopback: %zu frame(s)\n", frames);
}

} // namespace

int
main(int argc, char** argv)
{
    assert(argc >= 2);
    const std::string libs = argv[1];
    if (argc >= 3 && std::string_view(argv[2]) == "--loopback") {
        TestLoopback(libs);
        return 0;
    }
    TestRefusals();
    TestReplayParity(libs);
    TestUtf8Boundary();
    TestFullPose();
    TestLifetimes();
    TestStructSizes();
    TestDiagnosticBound();
    return 0;
}
