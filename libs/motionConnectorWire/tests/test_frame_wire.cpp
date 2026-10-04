// SPDX-License-Identifier: Apache-2.0
//
// The codec, from frames built in code: what the generated corpus cannot
// carry. A corpus message is text, so it cannot hold a NaN for the writer to
// refuse, a frame a test fills field by field, a message one byte over a bound
// the test chooses, or a process locale.
//
// "Decode after encode is the identity" (FRAME_WIRE_FORMAT §7) is checked
// against the motion contract's `==` for the pose, and field by field for the
// connector's own types, reading only what each says it carries.
#include "motionConnectorWire/FrameWire.h"

#include "motionCore/MotionPose.h"

#include <array>
#include <cassert>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>

namespace
{

namespace core = openstrata::connectors::core;
namespace motion = openstrata::motion;
namespace wire = openstrata::connectors::wire;

using motion::HumanJoint;

// ---------------------------------------------------------------------------
// Frame equality, reading only what each value says it carries.
// ---------------------------------------------------------------------------

bool
Equal(const core::TrackerObservation& a, const core::TrackerObservation& b)
{
    return a.trackerId == b.trackerId && a.hasPosition == b.hasPosition &&
           a.hasRotation == b.hasRotation && (!a.hasPosition || a.position == b.position) &&
           (!a.hasRotation || a.rotation == b.rotation) && a.confidence == b.confidence;
}

bool
Equal(const core::MotionFrame& a, const core::MotionFrame& b)
{
    if (a.frameNumber != b.frameNumber || a.sourceProfile != b.sourceProfile ||
        a.timing.sourceTimestamp != b.timing.sourceTimestamp ||
        a.timing.receiveTimestamp != b.timing.receiveTimestamp ||
        a.timing.sequence != b.timing.sequence || a.timing.sourceClock != b.timing.sourceClock ||
        a.actors.size() != b.actors.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < a.actors.size(); ++index)
    {
        const core::ActorFrame& first = a.actors[index];
        const core::ActorFrame& second = b.actors[index];
        if (first.actor != second.actor || first.pose.has_value() != second.pose.has_value() ||
            (first.pose && *first.pose != *second.pose) ||
            first.trackers.size() != second.trackers.size())
        {
            return false;
        }
        for (std::size_t tracker = 0; tracker < first.trackers.size(); ++tracker)
        {
            if (!Equal(first.trackers[tracker], second.trackers[tracker]))
            {
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Fixtures.
// ---------------------------------------------------------------------------

void
SetJoint(motion::MotionPose& pose, HumanJoint joint, const pxr::GfQuatf& rotation)
{
    const auto index = static_cast<std::size_t>(joint);
    pose.localRotations[index] = rotation;
    pose.validRotations.set(index);
}

// Every field the format has, at least once.
core::MotionFrame
FullFrame()
{
    core::MotionFrame frame;
    frame.frameNumber = 1042;
    frame.sourceProfile = "example.v1";
    frame.timing.sourceTimestamp = 1789795161.0333333;
    frame.timing.receiveTimestamp = 5123.481902;
    frame.timing.sequence = 88213;
    frame.timing.sourceClock = core::ClockDomain::Device;

    core::ActorFrame& body = frame.actors.emplace_back();
    body.actor = "0";
    motion::MotionPose pose;
    pose.timestamp = 1789795161.0333333;
    pose.root.worldPosition = pxr::GfVec3f(0.0f, 0.9f, 0.0f);
    pose.root.hasPosition = true;
    pose.root.worldOrientation = pxr::GfQuatf(0.6f, 0.0f, 0.8f, 0.0f);
    pose.root.hasOrientation = true;
    pose.root.linearVelocity = pxr::GfVec3f(0.1f, 0.0f, -1.0f / 3.0f);
    pose.root.hasLinearVelocity = true;
    pose.root.angularVelocity = pxr::GfVec3f(0.0f, std::numeric_limits<float>::denorm_min(), 0.0f);
    pose.root.hasAngularVelocity = true;
    SetJoint(pose, HumanJoint::Hips, pxr::GfQuatf(1.0f, 0.0f, 0.0f, 0.0f));
    SetJoint(pose, HumanJoint::Spine, pxr::GfQuatf(0.9999619f, 0.0087265f, 0.0f, 0.0f));
    SetJoint(pose, HumanJoint::LeftHand, pxr::GfQuatf(0.5f, -0.5f, 0.5f, -0.5f));
    SetJoint(pose, HumanJoint::RightLittleDistal, pxr::GfQuatf(-1.0f, 0.0f, 0.0f, 0.0f));
    std::array<float, motion::HumanJointCount> confidence{};
    confidence[static_cast<std::size_t>(HumanJoint::Hips)] = 1.0f;
    confidence[static_cast<std::size_t>(HumanJoint::Spine)] = 0.75f;
    confidence[static_cast<std::size_t>(HumanJoint::LeftHand)] = 0.1f;
    confidence[static_cast<std::size_t>(HumanJoint::RightLittleDistal)] = 0.0f;
    pose.confidence = confidence;
    pose.contacts = motion::ContactState{motion::FootContact::InContact,
                                         motion::FootContact::NotInContact};
    pose.lookAtTarget = pxr::GfVec3f(0.0f, 1.6f, 2.0f);
    pose.channels.Set("mouth:smile", 0.75f);
    pose.channels.Set("mouth:aa", 0.25f);
    pose.channels.Set("a name with a space", 1.5f);
    pose.channels.Set(std::string_view("quote \" backslash \\ tab \t nul \0 end", 35), -0.25f);
    pose.channels.Set("\xe3\x81\x82", 0.5f);
    pose.metadata.kind = motion::MotionSourceKind::LiveCapture;
    pose.metadata.provider = "example.sender";
    pose.metadata.protocol = "example";
    pose.metadata.sourceId = "Example Avatar";
    pose.metadata.sourceTimestamp = 1789795161.0333333;
    pose.metadata.sequenceNumber = std::numeric_limits<std::uint64_t>::max();
    body.pose = pose;

    core::ActorFrame& rig = frame.actors.emplace_back();
    rig.actor = "tracker rig";
    core::TrackerObservation& both = rig.trackers.emplace_back();
    both.trackerId = "1";
    both.position = pxr::GfVec3f(0.1f, 1.2f, -0.3f);
    both.hasPosition = true;
    both.rotation = pxr::GfQuatf(0.0f, 0.0f, 1.0f, 0.0f);
    both.hasRotation = true;
    both.confidence = 0.5f;
    core::TrackerObservation& positionOnly = rig.trackers.emplace_back();
    positionOnly.trackerId = "head";
    positionOnly.position = pxr::GfVec3f(0.0f, 1.7f, 0.0f);
    positionOnly.hasPosition = true;
    core::TrackerObservation& rotationOnly = rig.trackers.emplace_back();
    rotationOnly.trackerId = "2";
    rotationOnly.rotation = pxr::GfQuatf(0.5f, 0.5f, 0.5f, 0.5f);
    rotationOnly.hasRotation = true;

    core::ActorFrame& empty = frame.actors.emplace_back();
    empty.actor = "";
    return frame;
}

std::string
Encode(const core::MotionFrame& frame)
{
    std::string message;
    wire::WireError error;
    const bool ok = wire::EncodeFrame(frame, &message, &error);
    if (!ok)
    {
        std::fprintf(stderr, "encode refused at %s: %s\n", error.subject.c_str(),
                     error.detail.c_str());
    }
    assert(ok);
    return message;
}

core::MotionFrame
Decode(const std::string& message)
{
    core::MotionFrame frame;
    wire::WireError error;
    const bool ok = wire::DecodeFrame(message, &frame, &error);
    if (!ok)
    {
        std::fprintf(stderr, "decode refused: %s at %s: %s\n",
                     std::string(wire::WireErrorCodeName(error.code)).c_str(),
                     error.subject.c_str(), error.detail.c_str());
    }
    assert(ok);
    return frame;
}

void
ExpectEncodeRefusal(const core::MotionFrame& frame, wire::WireErrorCode code,
                    const std::string& subject)
{
    std::string message = "untouched";
    wire::WireError error;
    const bool ok = wire::EncodeFrame(frame, &message, &error);
    if (ok || error.code != code || error.subject != subject)
    {
        std::fprintf(stderr, "expected %s at %s, got %s at %s\n",
                     std::string(wire::WireErrorCodeName(code)).c_str(), subject.c_str(),
                     ok ? "success" : std::string(wire::WireErrorCodeName(error.code)).c_str(),
                     error.subject.c_str());
    }
    assert(!ok);
    assert(error.code == code);
    assert(error.subject == subject);
    assert(message == "untouched");
}

// ---------------------------------------------------------------------------
// Tests.
// ---------------------------------------------------------------------------

void
TestCodeNames()
{
    const auto last = static_cast<std::uint8_t>(wire::WireErrorCode::ValueNotFinite);
    for (std::uint8_t value = 0; value <= last; ++value)
    {
        const auto code = static_cast<wire::WireErrorCode>(value);
        const std::string_view name = wire::WireErrorCodeName(code);
        assert(name.substr(0, 5) == "WIRE_");
        assert(wire::FindWireErrorCode(name) == code);
    }
    assert(wire::WireErrorCodeName(wire::WireErrorCode::MessageTooLarge) ==
           "WIRE_MESSAGE_TOO_LARGE");
    assert(wire::WireErrorCodeName(wire::WireErrorCode::ValueNotFinite) ==
           "WIRE_VALUE_NOT_FINITE");
    assert(wire::WireErrorCodeName(static_cast<wire::WireErrorCode>(200)).empty());
    assert(!wire::FindWireErrorCode("WIRE_NOT_A_CODE"));
}

// §7: decode after encode is the identity, and encoding the decoded frame
// writes the same bytes.
void
TestRoundTrip()
{
    const core::MotionFrame frame = FullFrame();
    const std::string message = Encode(frame);
    const core::MotionFrame decoded = Decode(message);
    assert(Equal(frame, decoded));
    assert(Encode(decoded) == message);
}

// §4.3: every float and double reads back to the same value, at both ends of
// the range and between them.
void
TestNumbersRoundTrip()
{
    const float floats[] = {0.0f,
                            -0.0f,
                            0.1f,
                            1.0f / 3.0f,
                            std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::min(),
                            std::numeric_limits<float>::denorm_min(),
                            16777217.0f,
                            1e-7f};
    const double doubles[] = {0.0,
                              1789795161.0333333,
                              0.1,
                              1.0 / 3.0,
                              std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::denorm_min(),
                              -5e-324,
                              123456.0};
    for (const float value : floats)
    {
        core::MotionFrame frame;
        core::TrackerObservation& tracker = frame.actors.emplace_back().trackers.emplace_back();
        tracker.trackerId = "t";
        tracker.position = pxr::GfVec3f(value, -value, value);
        tracker.hasPosition = true;
        tracker.confidence = value;
        const core::MotionFrame decoded = Decode(Encode(frame));
        const core::TrackerObservation& read = decoded.actors[0].trackers[0];
        assert(read.position[0] == value && std::signbit(read.position[0]) == std::signbit(value));
        assert(read.position[1] == -value);
        assert(*read.confidence == value);
    }
    for (const double value : doubles)
    {
        core::MotionFrame frame;
        frame.timing.receiveTimestamp = value;
        frame.timing.sourceTimestamp = -value;
        const core::MotionFrame decoded = Decode(Encode(frame));
        assert(decoded.timing.receiveTimestamp == value);
        assert(*decoded.timing.sourceTimestamp == -value);
    }

    core::MotionFrame frame;
    frame.frameNumber = std::numeric_limits<std::uint64_t>::max();
    frame.timing.sequence = 0;
    const std::string message = Encode(frame);
    assert(message.find("\"frameNumber\":\"18446744073709551615\"") != std::string::npos);
    const core::MotionFrame decoded = Decode(message);
    assert(decoded.frameNumber == std::numeric_limits<std::uint64_t>::max());
    assert(decoded.timing.sequence == 0u);
}

// §4.1: what a frame does not carry is an absent key, never null, zero or an
// identity.
void
TestAbsenceIsAbsentKey()
{
    core::MotionFrame frame;
    frame.sourceProfile = "p";
    core::ActorFrame& actor = frame.actors.emplace_back();
    actor.actor = "0";
    actor.pose = motion::MotionPose();
    core::TrackerObservation& tracker = actor.trackers.emplace_back();
    tracker.trackerId = "1";
    tracker.hasRotation = true;
    const std::string message = Encode(frame);
    assert(message ==
           "{\"format\":\"openstrata.motion.frame/v1\",\"frameNumber\":\"0\","
           "\"sourceProfile\":\"p\",\"timing\":{\"receiveTimestamp\":0,\"sourceClock\":\"none\"},"
           "\"actors\":[{\"actor\":\"0\",\"pose\":{\"timestamp\":0,\"root\":{},\"joints\":{},"
           "\"channels\":{},\"metadata\":{\"kind\":\"clip\",\"provider\":\"\",\"protocol\":\"\","
           "\"sourceId\":\"\"}},\"trackers\":[{\"trackerId\":\"1\",\"rotation\":[1,0,0,0]}]}]}");
    assert(message.find("null") == std::string::npos);
    assert(Equal(frame, Decode(message)));
}

// §4.5: channels and confidence in name order, joints in vocabulary order,
// whatever order the frame held them in.
void
TestOrder()
{
    core::MotionFrame frame;
    motion::MotionPose pose;
    SetJoint(pose, HumanJoint::RightHand, pxr::GfQuatf(1.0f, 0.0f, 0.0f, 0.0f));
    SetJoint(pose, HumanJoint::Hips, pxr::GfQuatf(1.0f, 0.0f, 0.0f, 0.0f));
    SetJoint(pose, HumanJoint::Chest, pxr::GfQuatf(1.0f, 0.0f, 0.0f, 0.0f));
    pose.confidence = std::array<float, motion::HumanJointCount>{};
    // Filled directly, out of order: the set's invariant is not the writer's
    // to assume.
    pose.channels.entries = {{"b", 0.5f}, {"a", 0.25f}};
    frame.actors.emplace_back().pose = pose;
    const std::string message = Encode(frame);
    assert(message.find("\"joints\":{\"hips\":[1,0,0,0],\"chest\":[1,0,0,0],"
                        "\"rightHand\":[1,0,0,0]}") != std::string::npos);
    assert(message.find("\"confidence\":{\"chest\":0,\"hips\":0,\"rightHand\":0}") !=
           std::string::npos);
    assert(message.find("\"channels\":{\"a\":0.25,\"b\":0.5}") != std::string::npos);
    const core::MotionFrame decoded = Decode(message);
    assert(decoded.actors[0].pose->channels.entries.size() == 2);
    assert(decoded.actors[0].pose->channels.entries[0].name == "a");
}

// The writer refuses what the reader would refuse, and names where.
void
TestEncodeRefusals()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    {
        core::MotionFrame frame = FullFrame();
        frame.timing.receiveTimestamp = nan;
        ExpectEncodeRefusal(frame, wire::WireErrorCode::ValueNotFinite,
                            "$.timing.receiveTimestamp");
    }
    {
        core::MotionFrame frame = FullFrame();
        frame.actors[0].pose->localRotations[static_cast<std::size_t>(HumanJoint::Spine)] =
            pxr::GfQuatf(1.0f, inf, 0.0f, 0.0f);
        ExpectEncodeRefusal(frame, wire::WireErrorCode::ValueNotFinite,
                            "$.actors[0].pose.joints.spine[1]");
    }
    {
        core::MotionFrame frame = FullFrame();
        frame.actors[0].pose->channels.Set("mouth:aa", inf);
        ExpectEncodeRefusal(frame, wire::WireErrorCode::ValueNotFinite,
                            "$.actors[0].pose.channels[\"mouth:aa\"]");
    }
    {
        core::MotionFrame frame = FullFrame();
        frame.actors[0].pose->localRotations[static_cast<std::size_t>(HumanJoint::Hips)] =
            pxr::GfQuatf(0.0f, 0.0f, 0.0f, 0.0f);
        ExpectEncodeRefusal(frame, wire::WireErrorCode::QuaternionInvalid,
                            "$.actors[0].pose.joints.hips");
    }
    {
        core::MotionFrame frame = FullFrame();
        frame.actors[1].trackers[0].rotation = pxr::GfQuatf(2.0f, 0.0f, 0.0f, 0.0f);
        ExpectEncodeRefusal(frame, wire::WireErrorCode::QuaternionInvalid,
                            "$.actors[1].trackers[0].rotation");
    }
    {
        core::MotionFrame frame = FullFrame();
        frame.actors[2].actor = "0";
        ExpectEncodeRefusal(frame, wire::WireErrorCode::IdDuplicate, "$.actors[2].actor");
    }
    {
        core::MotionFrame frame = FullFrame();
        frame.actors[1].trackers[2].trackerId = "1";
        ExpectEncodeRefusal(frame, wire::WireErrorCode::IdDuplicate,
                            "$.actors[1].trackers[2].trackerId");
    }
    {
        core::MotionFrame frame = FullFrame();
        frame.actors[0].pose->channels.entries.push_back({"mouth:aa", 0.0f});
        ExpectEncodeRefusal(frame, wire::WireErrorCode::KeyDuplicate,
                            "$.actors[0].pose.channels[\"mouth:aa\"]");
    }
    {
        core::MotionFrame frame = FullFrame();
        frame.sourceProfile = "\xc0\xaf";
        ExpectEncodeRefusal(frame, wire::WireErrorCode::MessageMalformed, "$.sourceProfile");
    }
    {
        core::MotionFrame frame = FullFrame();
        frame.timing.sourceClock = static_cast<core::ClockDomain>(42);
        ExpectEncodeRefusal(frame, wire::WireErrorCode::ValueUnknown, "$.timing.sourceClock");
    }
    {
        // A slot the pose does not carry is not read: a NaN there is not on
        // the wire, so it is not refused.
        core::MotionFrame frame = FullFrame();
        frame.actors[0].pose->localRotations[static_cast<std::size_t>(HumanJoint::Jaw)] =
            pxr::GfQuatf(static_cast<float>(nan), 0.0f, 0.0f, 0.0f);
        (*frame.actors[0].pose->confidence)[static_cast<std::size_t>(HumanJoint::Jaw)] = inf;
        frame.actors[1].trackers[1].rotation = pxr::GfQuatf(0.0f, 0.0f, 0.0f, 0.0f);
        Encode(frame);
    }
}

// §6: the size bound is checked before parsing, and a refused message leaves
// the frame untouched.
void
TestDecodeBoundsAndAtomicity()
{
    const std::string message = Encode(FullFrame());

    core::MotionFrame frame;
    frame.sourceProfile = "untouched";
    wire::WireError error;
    wire::DecodeOptions options;
    options.maxMessageBytes = message.size() - 1;
    assert(!wire::DecodeFrame(message, &frame, &error, options));
    assert(error.code == wire::WireErrorCode::MessageTooLarge);
    assert(error.subject == "$");
    assert(frame.sourceProfile == "untouched");

    options.maxMessageBytes = message.size();
    assert(wire::DecodeFrame(message, &frame, &error, options));
    assert(frame.sourceProfile == "example.v1");

    // Refused deep inside, after the frame number and the first actor were
    // read: none of it lands.
    core::MotionFrame untouched;
    untouched.sourceProfile = "untouched";
    std::string broken = message;
    const std::string head = "\"trackerId\":\"head\"";
    broken.replace(broken.find(head), head.size(), "\"trackerId\":\"1\"");
    assert(!wire::DecodeFrame(broken, &untouched, &error));
    assert(error.code == wire::WireErrorCode::IdDuplicate);
    assert(error.subject == "$.actors[1].trackers[1].trackerId");
    assert(untouched.sourceProfile == "untouched" && untouched.actors.empty());

    // No error out-parameter is fine too.
    assert(!wire::DecodeFrame("{}", &untouched));
}

// The token's point is '.', whatever the host process's LC_NUMERIC says.
void
TestLocaleIndependence()
{
    const char* const commaLocales[] = {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "German_Germany.1252",
                                        "fr_FR.UTF-8"};
    const char* chosen = nullptr;
    for (const char* name : commaLocales)
    {
        if (std::setlocale(LC_NUMERIC, name) && *std::localeconv()->decimal_point == ',')
        {
            chosen = name;
            break;
        }
    }
    if (!chosen)
    {
        std::setlocale(LC_NUMERIC, "C");
        std::printf("  (no comma-decimal locale installed; locale check skipped)\n");
        return;
    }
    const core::MotionFrame frame = FullFrame();
    const std::string message = Encode(frame);
    const core::MotionFrame decoded = Decode(message);
    std::setlocale(LC_NUMERIC, "C");
    assert(message.find("0.9") != std::string::npos);
    assert(Equal(frame, decoded));
    assert(Encode(decoded) == message);
}

} // namespace

int
main()
{
    TestCodeNames();
    TestRoundTrip();
    TestNumbersRoundTrip();
    TestAbsenceIsAbsentKey();
    TestOrder();
    TestEncodeRefusals();
    TestDecodeBoundsAndAtomicity();
    TestLocaleIndependence();
    std::printf("motionConnectorWire frame wire tests passed\n");
    return 0;
}
