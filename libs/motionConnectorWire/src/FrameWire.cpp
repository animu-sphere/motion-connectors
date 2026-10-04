// SPDX-License-Identifier: Apache-2.0
#include "motionConnectorWire/FrameWire.h"

#include "Json.h"

#include "motionCore/MotionPose.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openstrata::connectors::wire
{

namespace
{

namespace motion = openstrata::motion;

using json::Value;

// The deepest the format nests: the frame, `actors`, an actor, its `pose`, the
// pose's `root`, and a vector inside it (§3).
constexpr std::size_t kMaxDepth = 6;

// The trace reader's tolerance (usd-motion-plugins' CaptureTrace.cpp), which
// the motion contract names for "is this a rotation": a squared length within
// 1e-3 of one. Anything past it was never a unit quaternion.
constexpr float kQuaternionLengthTolerance = 1e-3f;

constexpr std::array<std::string_view, 16> kCodeNames = {
    "WIRE_MESSAGE_TOO_LARGE",
    "WIRE_MESSAGE_MALFORMED",
    "WIRE_NESTING_TOO_DEEP",
    "WIRE_KEY_DUPLICATE",
    "WIRE_FORMAT_UNKNOWN",
    "WIRE_KEY_UNKNOWN",
    "WIRE_KEY_MISSING",
    "WIRE_TYPE_MISMATCH",
    "WIRE_VALUE_UNKNOWN",
    "WIRE_JOINT_UNKNOWN",
    "WIRE_CONFIDENCE_MISMATCH",
    "WIRE_NUMBER_OUT_OF_RANGE",
    "WIRE_QUATERNION_INVALID",
    "WIRE_COUNTER_INVALID",
    "WIRE_ID_DUPLICATE",
    "WIRE_VALUE_NOT_FINITE",
};

// The vocabularies the format spells as words (§3.2, §3.4), in enum order.
constexpr std::array<std::string_view, 5> kClockNames = {
    "device", "localMonotonic", "wall", "networkSynchronized", "none"};
constexpr std::array<std::string_view, 5> kKindNames = {
    "clip", "liveCapture", "generated", "procedural", "simulated"};
// The trace's words, in `FootContact` order: Unknown, NotInContact, InContact.
constexpr std::array<std::string_view, 3> kContactNames = {"unknown", "free", "contact"};

template <std::size_t N>
std::string_view
WordFor(const std::array<std::string_view, N>& names, std::size_t index) noexcept
{
    return index < N ? names[index] : std::string_view();
}

template <std::size_t N>
std::optional<std::size_t>
FindWord(const std::array<std::string_view, N>& names, std::string_view word) noexcept
{
    for (std::size_t index = 0; index < N; ++index)
    {
        if (names[index] == word)
        {
            return index;
        }
    }
    return std::nullopt;
}

// Joint indices in name order, which is how confidence is written (§4.5).
const std::array<std::size_t, motion::HumanJointCount>&
JointsByName()
{
    static const auto order = [] {
        std::array<std::size_t, motion::HumanJointCount> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            result[index] = index;
        }
        std::sort(result.begin(), result.end(), [](std::size_t a, std::size_t b) {
            return motion::HumanJointName(static_cast<motion::HumanJoint>(a)) <
                   motion::HumanJointName(static_cast<motion::HumanJoint>(b));
        });
        return result;
    }();
    return order;
}

std::string_view
JointName(std::size_t index)
{
    return motion::HumanJointName(static_cast<motion::HumanJoint>(index));
}

bool
IsUnitQuaternion(float w, float x, float y, float z) noexcept
{
    const float lengthSquared = w * w + x * x + y * y + z * z;
    return lengthSquared > 0.0f && std::fabs(lengthSquared - 1.0f) <= kQuaternionLengthTolerance;
}

// The one place both directions report from, so a subject is spelled the same
// way whichever side found the problem.
class Reporter
{
  public:
    explicit Reporter(WireError* error) : _error(error) {}

  protected:
    bool
    Fail(WireErrorCode code, std::string detail)
    {
        if (_error)
        {
            _error->code = code;
            _error->subject = _path.ToString();
            _error->detail = std::move(detail);
        }
        return false;
    }

    // Runs `body` with `key` (or `index`) appended to the path, so a refusal
    // inside it names the field it is about.
    template <class Body>
    bool
    At(std::string_view key, Body&& body)
    {
        _path.PushKey(key);
        const bool ok = body();
        _path.Pop();
        return ok;
    }

    template <class Body>
    bool
    AtIndex(std::size_t index, Body&& body)
    {
        _path.PushIndex(index);
        const bool ok = body();
        _path.Pop();
        return ok;
    }

    json::Path _path;

  private:
    WireError* _error;
};

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

class Reader final : public Reporter
{
  public:
    using Reporter::Reporter;

    bool
    Frame(const Value& root, core::MotionFrame* frame)
    {
        if (root.kind != Value::Kind::Object)
        {
            return Fail(WireErrorCode::TypeMismatch, "a message is a JSON object");
        }
        // The format is read before anything else, so a later version's frame
        // is reported as a version this reader does not know rather than as a
        // version 1 frame with unknown keys (§5).
        const Value* format = Find(root, "format");
        if (!format)
        {
            return At("format",
                      [&] { return Fail(WireErrorCode::KeyMissing, "the message has no format"); });
        }
        const bool known = At("format", [&] {
            if (format->kind != Value::Kind::String)
            {
                return Fail(WireErrorCode::TypeMismatch, "the format is a string");
            }
            if (format->text != FrameFormatV1)
            {
                return Fail(WireErrorCode::FormatUnknown,
                            "this reader reads " + std::string(FrameFormatV1));
            }
            return true;
        });
        if (!known)
        {
            return false;
        }
        if (!Keys(root, {"format", "frameNumber", "sourceProfile", "timing", "actors"},
                  {"frameNumber", "sourceProfile", "timing", "actors"}))
        {
            return false;
        }
        return At("frameNumber",
                  [&] { return Counter(*Find(root, "frameNumber"), &frame->frameNumber); }) &&
               At("sourceProfile",
                  [&] { return String(*Find(root, "sourceProfile"), &frame->sourceProfile); }) &&
               At("timing", [&] { return Timing(*Find(root, "timing"), &frame->timing); }) &&
               At("actors", [&] { return Actors(*Find(root, "actors"), &frame->actors); });
    }

  private:
    static const Value*
    Find(const Value& object, std::string_view key) noexcept
    {
        for (const json::Member& member : object.members)
        {
            if (member.key == key)
            {
                return &member.value;
            }
        }
        return nullptr;
    }

    // Unknown keys first, in message order, then missing ones in table order:
    // a version 1 reader refuses an unknown key at any depth (§5).
    bool
    Keys(const Value& object, std::initializer_list<std::string_view> allowed,
         std::initializer_list<std::string_view> required)
    {
        for (const json::Member& member : object.members)
        {
            if (std::find(allowed.begin(), allowed.end(), member.key) == allowed.end())
            {
                return At(member.key,
                          [&] { return Fail(WireErrorCode::KeyUnknown, "not a version 1 key"); });
            }
        }
        for (const std::string_view key : required)
        {
            if (!Find(object, key))
            {
                return At(key, [&] { return Fail(WireErrorCode::KeyMissing, "a required key"); });
            }
        }
        return true;
    }

    bool
    Object(const Value& value)
    {
        return value.kind == Value::Kind::Object ||
               Fail(WireErrorCode::TypeMismatch, "expected an object");
    }

    bool
    Array(const Value& value)
    {
        return value.kind == Value::Kind::Array ||
               Fail(WireErrorCode::TypeMismatch, "expected an array");
    }

    bool
    String(const Value& value, std::string* out)
    {
        if (value.kind != Value::Kind::String)
        {
            return Fail(WireErrorCode::TypeMismatch, "expected a string");
        }
        *out = value.text;
        return true;
    }

    bool
    Number(const Value& value)
    {
        return value.kind == Value::Kind::Number ||
               Fail(WireErrorCode::TypeMismatch, "expected a number");
    }

    bool
    Double(const Value& value, double* out)
    {
        if (!Number(value))
        {
            return false;
        }
        return json::ParseDouble(value.text, out) ||
               Fail(WireErrorCode::NumberOutOfRange, "overflows a double");
    }

    bool
    Float(const Value& value, float* out)
    {
        if (!Number(value))
        {
            return false;
        }
        return json::ParseFloat(value.text, out) ||
               Fail(WireErrorCode::NumberOutOfRange, "overflows a float");
    }

    // A decimal uint64 as a string (§4.3), spelled one way: digits only, and no
    // leading zero, so a counter has exactly one spelling and re-encoding a
    // decoded message reproduces it.
    bool
    Counter(const Value& value, std::uint64_t* out)
    {
        if (value.kind != Value::Kind::String)
        {
            return Fail(WireErrorCode::TypeMismatch, "a counter is a decimal string");
        }
        const std::string& text = value.text;
        const bool digitsOnly =
            !text.empty() &&
            std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (!digitsOnly || (text.size() > 1 && text.front() == '0'))
        {
            return Fail(WireErrorCode::CounterInvalid, "not a decimal uint64");
        }
        std::uint64_t result = 0;
        const auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), result);
        if (status != std::errc() || end != text.data() + text.size())
        {
            return Fail(WireErrorCode::CounterInvalid, "overflows a uint64");
        }
        *out = result;
        return true;
    }

    template <std::size_t N>
    bool
    Floats(const Value& value, std::array<float, N>* out)
    {
        if (!Array(value))
        {
            return false;
        }
        if (value.elements.size() != N)
        {
            return Fail(WireErrorCode::TypeMismatch,
                        "expected " + std::to_string(N) + " numbers");
        }
        for (std::size_t index = 0; index < N; ++index)
        {
            if (!AtIndex(index, [&] { return Float(value.elements[index], &(*out)[index]); }))
            {
                return false;
            }
        }
        return true;
    }

    bool
    Vector(const Value& value, pxr::GfVec3f* out)
    {
        std::array<float, 3> components{};
        if (!Floats(value, &components))
        {
            return false;
        }
        *out = pxr::GfVec3f(components[0], components[1], components[2]);
        return true;
    }

    // `[w, x, y, z]` (§4.3), refused when zero or not unit length rather than
    // normalized into a plausible pose (CONNECTOR_CONTRACT §10).
    bool
    Rotation(const Value& value, pxr::GfQuatf* out)
    {
        std::array<float, 4> c{};
        if (!Floats(value, &c))
        {
            return false;
        }
        if (!IsUnitQuaternion(c[0], c[1], c[2], c[3]))
        {
            return Fail(WireErrorCode::QuaternionInvalid, "not a unit quaternion");
        }
        *out = pxr::GfQuatf(c[0], c[1], c[2], c[3]);
        return true;
    }

    template <std::size_t N>
    bool
    Word(const Value& value, const std::array<std::string_view, N>& names, std::size_t* out)
    {
        std::string text;
        if (!String(value, &text))
        {
            return false;
        }
        const std::optional<std::size_t> index = FindWord(names, text);
        if (!index)
        {
            return Fail(WireErrorCode::ValueUnknown, "not a word this field takes");
        }
        *out = *index;
        return true;
    }

    bool
    Timing(const Value& value, core::FrameTiming* timing)
    {
        if (!Object(value) ||
            !Keys(value, {"sourceTimestamp", "receiveTimestamp", "sequence", "sourceClock"},
                  {"receiveTimestamp", "sourceClock"}))
        {
            return false;
        }
        if (const Value* stamp = Find(value, "sourceTimestamp"))
        {
            double seconds = 0.0;
            if (!At("sourceTimestamp", [&] { return Double(*stamp, &seconds); }))
            {
                return false;
            }
            timing->sourceTimestamp = seconds;
        }
        if (!At("receiveTimestamp", [&] {
                return Double(*Find(value, "receiveTimestamp"), &timing->receiveTimestamp);
            }))
        {
            return false;
        }
        if (const Value* sequence = Find(value, "sequence"))
        {
            std::uint64_t counter = 0;
            if (!At("sequence", [&] { return Counter(*sequence, &counter); }))
            {
                return false;
            }
            timing->sequence = counter;
        }
        std::size_t clock = 0;
        if (!At("sourceClock",
                [&] { return Word(*Find(value, "sourceClock"), kClockNames, &clock); }))
        {
            return false;
        }
        timing->sourceClock = static_cast<core::ClockDomain>(clock);
        return true;
    }

    bool
    Actors(const Value& value, std::vector<core::ActorFrame>* actors)
    {
        if (!Array(value))
        {
            return false;
        }
        std::unordered_set<std::string> seen;
        actors->reserve(value.elements.size());
        for (std::size_t index = 0; index < value.elements.size(); ++index)
        {
            core::ActorFrame& actor = actors->emplace_back();
            if (!AtIndex(index, [&] { return Actor(value.elements[index], &actor, &seen); }))
            {
                return false;
            }
        }
        return true;
    }

    bool
    Actor(const Value& value, core::ActorFrame* actor, std::unordered_set<std::string>* seen)
    {
        if (!Object(value) || !Keys(value, {"actor", "pose", "trackers"}, {"actor", "trackers"}))
        {
            return false;
        }
        const bool id = At("actor", [&] {
            if (!String(*Find(value, "actor"), &actor->actor))
            {
                return false;
            }
            return seen->insert(actor->actor).second ||
                   Fail(WireErrorCode::IdDuplicate, "an actor appears twice in one frame");
        });
        if (!id)
        {
            return false;
        }
        if (const Value* pose = Find(value, "pose"))
        {
            motion::MotionPose result;
            if (!At("pose", [&] { return Pose(*pose, &result); }))
            {
                return false;
            }
            actor->pose = std::move(result);
        }
        return At("trackers", [&] { return Trackers(*Find(value, "trackers"), &actor->trackers); });
    }

    bool
    Pose(const Value& value, motion::MotionPose* pose)
    {
        if (!Object(value) ||
            !Keys(value,
                  {"timestamp", "root", "joints", "confidence", "contacts", "lookAtTarget",
                   "channels", "metadata"},
                  {"timestamp", "root", "joints", "channels", "metadata"}))
        {
            return false;
        }
        if (!At("timestamp", [&] { return Double(*Find(value, "timestamp"), &pose->timestamp); }) ||
            !At("root", [&] { return Root(*Find(value, "root"), &pose->root); }) ||
            !At("joints", [&] { return Joints(*Find(value, "joints"), pose); }))
        {
            return false;
        }
        if (const Value* confidence = Find(value, "confidence"))
        {
            if (!At("confidence", [&] { return Confidence(*confidence, pose); }))
            {
                return false;
            }
        }
        if (const Value* contacts = Find(value, "contacts"))
        {
            motion::ContactState state;
            if (!At("contacts", [&] { return Contacts(*contacts, &state); }))
            {
                return false;
            }
            pose->contacts = state;
        }
        if (const Value* target = Find(value, "lookAtTarget"))
        {
            pxr::GfVec3f point(0.0f);
            if (!At("lookAtTarget", [&] { return Vector(*target, &point); }))
            {
                return false;
            }
            pose->lookAtTarget = point;
        }
        return At("channels",
                  [&] { return Channels(*Find(value, "channels"), &pose->channels); }) &&
               At("metadata", [&] { return Metadata(*Find(value, "metadata"), &pose->metadata); });
    }

    bool
    Root(const Value& value, motion::RootMotion* root)
    {
        if (!Object(value) ||
            !Keys(value,
                  {"worldPosition", "worldOrientation", "linearVelocity", "angularVelocity"}, {}))
        {
            return false;
        }
        const auto vector = [&](std::string_view key, pxr::GfVec3f* out, bool* has) {
            const Value* field = Find(value, key);
            if (!field)
            {
                return true;
            }
            *has = true;
            return At(key, [&] { return Vector(*field, out); });
        };
        if (!vector("worldPosition", &root->worldPosition, &root->hasPosition))
        {
            return false;
        }
        if (const Value* orientation = Find(value, "worldOrientation"))
        {
            root->hasOrientation = true;
            if (!At("worldOrientation",
                    [&] { return Rotation(*orientation, &root->worldOrientation); }))
            {
                return false;
            }
        }
        return vector("linearVelocity", &root->linearVelocity, &root->hasLinearVelocity) &&
               vector("angularVelocity", &root->angularVelocity, &root->hasAngularVelocity);
    }

    // A joint is keyed by its vocabulary name, and a name the vocabulary does
    // not have is refused: a typo must not read as a missing limb (§6).
    bool
    Joints(const Value& value, motion::MotionPose* pose)
    {
        if (!Object(value))
        {
            return false;
        }
        for (const json::Member& member : value.members)
        {
            const bool ok = At(member.key, [&] {
                const std::optional<motion::HumanJoint> joint = motion::FindHumanJoint(member.key);
                if (!joint)
                {
                    return Fail(WireErrorCode::JointUnknown, "not a joint of the vocabulary");
                }
                const auto index = static_cast<std::size_t>(*joint);
                if (!Rotation(member.value, &pose->localRotations[index]))
                {
                    return false;
                }
                pose->validRotations.set(index);
                return true;
            });
            if (!ok)
            {
                return false;
            }
        }
        return true;
    }

    // Over exactly the keys `joints` has (§3.4): a joint in one and not the
    // other is refused in both directions, because neither direction has a
    // value to fill the gap with.
    bool
    Confidence(const Value& value, motion::MotionPose* pose)
    {
        if (!Object(value))
        {
            return false;
        }
        std::array<float, motion::HumanJointCount> confidence{};
        std::bitset<motion::HumanJointCount> present;
        for (const json::Member& member : value.members)
        {
            const bool ok = At(member.key, [&] {
                const std::optional<motion::HumanJoint> joint = motion::FindHumanJoint(member.key);
                if (!joint)
                {
                    return Fail(WireErrorCode::JointUnknown, "not a joint of the vocabulary");
                }
                const auto index = static_cast<std::size_t>(*joint);
                if (!pose->validRotations.test(index))
                {
                    return Fail(WireErrorCode::ConfidenceMismatch,
                                "a confidence for a joint the pose does not carry");
                }
                present.set(index);
                return Float(member.value, &confidence[index]);
            });
            if (!ok)
            {
                return false;
            }
        }
        for (std::size_t index = 0; index < motion::HumanJointCount; ++index)
        {
            if (pose->validRotations.test(index) && !present.test(index))
            {
                return At(JointName(index), [&] {
                    return Fail(WireErrorCode::ConfidenceMismatch,
                                "a carried joint has no confidence");
                });
            }
        }
        pose->confidence = confidence;
        return true;
    }

    bool
    Contacts(const Value& value, motion::ContactState* state)
    {
        if (!Object(value) || !Keys(value, {"leftFoot", "rightFoot"}, {"leftFoot", "rightFoot"}))
        {
            return false;
        }
        std::size_t left = 0;
        std::size_t right = 0;
        if (!At("leftFoot", [&] { return Word(*Find(value, "leftFoot"), kContactNames, &left); }) ||
            !At("rightFoot",
                [&] { return Word(*Find(value, "rightFoot"), kContactNames, &right); }))
        {
            return false;
        }
        state->leftFoot = static_cast<motion::FootContact>(left);
        state->rightFoot = static_cast<motion::FootContact>(right);
        return true;
    }

    bool
    Channels(const Value& value, motion::MotionChannelSet* channels)
    {
        if (!Object(value))
        {
            return false;
        }
        for (const json::Member& member : value.members)
        {
            float weight = 0.0f;
            if (!At(member.key, [&] { return Float(member.value, &weight); }))
            {
                return false;
            }
            // The parser refused a repeated key, so every Set adds.
            channels->Set(member.key, weight);
        }
        return true;
    }

    bool
    Metadata(const Value& value, motion::SourceMetadata* metadata)
    {
        if (!Object(value) ||
            !Keys(value,
                  {"kind", "provider", "protocol", "sourceId", "sourceTimestamp", "sequenceNumber"},
                  {"kind", "provider", "protocol", "sourceId"}))
        {
            return false;
        }
        std::size_t kind = 0;
        if (!At("kind", [&] { return Word(*Find(value, "kind"), kKindNames, &kind); }) ||
            !At("provider",
                [&] { return String(*Find(value, "provider"), &metadata->provider); }) ||
            !At("protocol",
                [&] { return String(*Find(value, "protocol"), &metadata->protocol); }) ||
            !At("sourceId", [&] { return String(*Find(value, "sourceId"), &metadata->sourceId); }))
        {
            return false;
        }
        metadata->kind = static_cast<motion::MotionSourceKind>(kind);
        if (const Value* stamp = Find(value, "sourceTimestamp"))
        {
            double seconds = 0.0;
            if (!At("sourceTimestamp", [&] { return Double(*stamp, &seconds); }))
            {
                return false;
            }
            metadata->sourceTimestamp = seconds;
        }
        if (const Value* sequence = Find(value, "sequenceNumber"))
        {
            std::uint64_t counter = 0;
            if (!At("sequenceNumber", [&] { return Counter(*sequence, &counter); }))
            {
                return false;
            }
            metadata->sequenceNumber = counter;
        }
        return true;
    }

    bool
    Trackers(const Value& value, std::vector<core::TrackerObservation>* trackers)
    {
        if (!Array(value))
        {
            return false;
        }
        std::unordered_set<std::string> seen;
        trackers->reserve(value.elements.size());
        for (std::size_t index = 0; index < value.elements.size(); ++index)
        {
            core::TrackerObservation& tracker = trackers->emplace_back();
            if (!AtIndex(index, [&] { return Tracker(value.elements[index], &tracker, &seen); }))
            {
                return false;
            }
        }
        return true;
    }

    bool
    Tracker(const Value& value, core::TrackerObservation* tracker,
            std::unordered_set<std::string>* seen)
    {
        if (!Object(value) ||
            !Keys(value, {"trackerId", "position", "rotation", "confidence"}, {"trackerId"}))
        {
            return false;
        }
        const bool id = At("trackerId", [&] {
            if (!String(*Find(value, "trackerId"), &tracker->trackerId))
            {
                return false;
            }
            return seen->insert(tracker->trackerId).second ||
                   Fail(WireErrorCode::IdDuplicate, "a tracker appears twice in one actor");
        });
        if (!id)
        {
            return false;
        }
        if (const Value* position = Find(value, "position"))
        {
            tracker->hasPosition = true;
            if (!At("position", [&] { return Vector(*position, &tracker->position); }))
            {
                return false;
            }
        }
        if (const Value* rotation = Find(value, "rotation"))
        {
            tracker->hasRotation = true;
            if (!At("rotation", [&] { return Rotation(*rotation, &tracker->rotation); }))
            {
                return false;
            }
        }
        if (const Value* confidence = Find(value, "confidence"))
        {
            float weight = 0.0f;
            if (!At("confidence", [&] { return Float(*confidence, &weight); }))
            {
                return false;
            }
            tracker->confidence = weight;
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

class Writer final : public Reporter
{
  public:
    using Reporter::Reporter;

    bool
    Frame(const core::MotionFrame& frame)
    {
        _out += "{\"format\":";
        json::AppendString(_out, FrameFormatV1);
        _out += ",\"frameNumber\":";
        Counter(frame.frameNumber);
        if (!Key("sourceProfile") ||
            !At("sourceProfile", [&] { return String(frame.sourceProfile); }) ||
            !Key("timing") || !At("timing", [&] { return Timing(frame.timing); }) ||
            !Key("actors") || !At("actors", [&] { return Actors(frame.actors); }))
        {
            return false;
        }
        _out += '}';
        return true;
    }

    std::string&
    Output() noexcept
    {
        return _out;
    }

  private:
    // `,"key":` -- every key but an object's first, which `Open` writes.
    bool
    Key(std::string_view key)
    {
        _out += ',';
        json::AppendString(_out, key);
        _out += ':';
        return true;
    }

    void
    FirstKey(std::string_view key)
    {
        _out += '{';
        json::AppendString(_out, key);
        _out += ':';
    }

    bool
    String(std::string_view text)
    {
        if (!json::IsValidUtf8(text))
        {
            return Fail(WireErrorCode::MessageMalformed, "a string that is not UTF-8");
        }
        json::AppendString(_out, text);
        return true;
    }

    void
    Counter(std::uint64_t value)
    {
        _out += '"';
        _out += std::to_string(value);
        _out += '"';
    }

    bool
    Double(double value)
    {
        if (!std::isfinite(value))
        {
            return Fail(WireErrorCode::ValueNotFinite, "JSON cannot spell a non-finite number");
        }
        json::AppendDouble(_out, value);
        return true;
    }

    bool
    Float(float value)
    {
        if (!std::isfinite(value))
        {
            return Fail(WireErrorCode::ValueNotFinite, "JSON cannot spell a non-finite number");
        }
        json::AppendFloat(_out, value);
        return true;
    }

    bool
    Floats(std::initializer_list<float> values)
    {
        _out += '[';
        std::size_t index = 0;
        for (const float value : values)
        {
            if (index != 0)
            {
                _out += ',';
            }
            if (!AtIndex(index, [&] { return Float(value); }))
            {
                return false;
            }
            ++index;
        }
        _out += ']';
        return true;
    }

    bool
    Vector(const pxr::GfVec3f& value)
    {
        return Floats({value[0], value[1], value[2]});
    }

    bool
    Rotation(const pxr::GfQuatf& value)
    {
        const float w = value.GetReal();
        const pxr::GfVec3f& v = value.GetImaginary();
        if (!Floats({w, v[0], v[1], v[2]}))
        {
            return false;
        }
        return IsUnitQuaternion(w, v[0], v[1], v[2]) ||
               Fail(WireErrorCode::QuaternionInvalid, "not a unit quaternion");
    }

    template <std::size_t N>
    bool
    Word(const std::array<std::string_view, N>& names, std::size_t index)
    {
        const std::string_view word = WordFor(names, index);
        if (word.empty())
        {
            return Fail(WireErrorCode::ValueUnknown, "outside the enum this field spells");
        }
        json::AppendString(_out, word);
        return true;
    }

    bool
    Timing(const core::FrameTiming& timing)
    {
        _out += '{';
        bool first = true;
        const auto key = [&](std::string_view name) {
            if (!first)
            {
                _out += ',';
            }
            first = false;
            json::AppendString(_out, name);
            _out += ':';
        };
        if (timing.sourceTimestamp)
        {
            key("sourceTimestamp");
            if (!At("sourceTimestamp", [&] { return Double(*timing.sourceTimestamp); }))
            {
                return false;
            }
        }
        key("receiveTimestamp");
        if (!At("receiveTimestamp", [&] { return Double(timing.receiveTimestamp); }))
        {
            return false;
        }
        if (timing.sequence)
        {
            key("sequence");
            Counter(*timing.sequence);
        }
        key("sourceClock");
        if (!At("sourceClock",
                [&] { return Word(kClockNames, static_cast<std::size_t>(timing.sourceClock)); }))
        {
            return false;
        }
        _out += '}';
        return true;
    }

    bool
    Actors(const std::vector<core::ActorFrame>& actors)
    {
        _out += '[';
        std::unordered_set<std::string_view> seen;
        for (std::size_t index = 0; index < actors.size(); ++index)
        {
            if (index != 0)
            {
                _out += ',';
            }
            if (!AtIndex(index, [&] { return Actor(actors[index], &seen); }))
            {
                return false;
            }
        }
        _out += ']';
        return true;
    }

    bool
    Actor(const core::ActorFrame& actor, std::unordered_set<std::string_view>* seen)
    {
        FirstKey("actor");
        const bool id = At("actor", [&] {
            if (!String(actor.actor))
            {
                return false;
            }
            return seen->insert(actor.actor).second ||
                   Fail(WireErrorCode::IdDuplicate, "an actor appears twice in one frame");
        });
        if (!id)
        {
            return false;
        }
        if (actor.pose && (!Key("pose") || !At("pose", [&] { return Pose(*actor.pose); })))
        {
            return false;
        }
        if (!Key("trackers") || !At("trackers", [&] { return Trackers(actor.trackers); }))
        {
            return false;
        }
        _out += '}';
        return true;
    }

    bool
    Pose(const motion::MotionPose& pose)
    {
        FirstKey("timestamp");
        if (!At("timestamp", [&] { return Double(pose.timestamp); }) || !Key("root") ||
            !At("root", [&] { return Root(pose.root); }) || !Key("joints") ||
            !At("joints", [&] { return Joints(pose); }))
        {
            return false;
        }
        if (pose.confidence &&
            (!Key("confidence") || !At("confidence", [&] { return Confidence(pose); })))
        {
            return false;
        }
        if (pose.contacts &&
            (!Key("contacts") || !At("contacts", [&] { return Contacts(*pose.contacts); })))
        {
            return false;
        }
        if (pose.lookAtTarget &&
            (!Key("lookAtTarget") ||
             !At("lookAtTarget", [&] { return Vector(*pose.lookAtTarget); })))
        {
            return false;
        }
        if (!Key("channels") || !At("channels", [&] { return Channels(pose.channels); }) ||
            !Key("metadata") || !At("metadata", [&] { return Metadata(pose.metadata); }))
        {
            return false;
        }
        _out += '}';
        return true;
    }

    bool
    Root(const motion::RootMotion& root)
    {
        _out += '{';
        bool first = true;
        const auto field = [&](bool has, std::string_view key, auto&& body) {
            if (!has)
            {
                return true;
            }
            if (!first)
            {
                _out += ',';
            }
            first = false;
            json::AppendString(_out, key);
            _out += ':';
            return At(key, body);
        };
        if (!field(root.hasPosition, "worldPosition", [&] { return Vector(root.worldPosition); }) ||
            !field(root.hasOrientation, "worldOrientation",
                   [&] { return Rotation(root.worldOrientation); }) ||
            !field(root.hasLinearVelocity, "linearVelocity",
                   [&] { return Vector(root.linearVelocity); }) ||
            !field(root.hasAngularVelocity, "angularVelocity",
                   [&] { return Vector(root.angularVelocity); }))
        {
            return false;
        }
        _out += '}';
        return true;
    }

    bool
    Joints(const motion::MotionPose& pose)
    {
        _out += '{';
        bool first = true;
        for (std::size_t index = 0; index < motion::HumanJointCount; ++index)
        {
            if (!pose.validRotations.test(index))
            {
                continue;
            }
            if (!first)
            {
                _out += ',';
            }
            first = false;
            json::AppendString(_out, JointName(index));
            _out += ':';
            if (!At(JointName(index), [&] { return Rotation(pose.localRotations[index]); }))
            {
                return false;
            }
        }
        _out += '}';
        return true;
    }

    bool
    Confidence(const motion::MotionPose& pose)
    {
        _out += '{';
        bool first = true;
        for (const std::size_t index : JointsByName())
        {
            if (!pose.validRotations.test(index))
            {
                continue;
            }
            if (!first)
            {
                _out += ',';
            }
            first = false;
            json::AppendString(_out, JointName(index));
            _out += ':';
            if (!At(JointName(index), [&] { return Float((*pose.confidence)[index]); }))
            {
                return false;
            }
        }
        _out += '}';
        return true;
    }

    bool
    Contacts(const motion::ContactState& contacts)
    {
        FirstKey("leftFoot");
        if (!At("leftFoot",
                [&] { return Word(kContactNames, static_cast<std::size_t>(contacts.leftFoot)); }) ||
            !Key("rightFoot") ||
            !At("rightFoot",
                [&] { return Word(kContactNames, static_cast<std::size_t>(contacts.rightFoot)); }))
        {
            return false;
        }
        _out += '}';
        return true;
    }

    // Name order (§4.5). `MotionChannelSet` keeps its entries sorted and
    // unique, but a caller can fill `entries` directly, so the order is
    // established here and a repeated name -- which would be a repeated key --
    // is refused.
    bool
    Channels(const motion::MotionChannelSet& channels)
    {
        std::vector<const motion::MotionChannel*> sorted;
        sorted.reserve(channels.entries.size());
        for (const motion::MotionChannel& channel : channels.entries)
        {
            sorted.push_back(&channel);
        }
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const motion::MotionChannel* a, const motion::MotionChannel* b) {
                             return a->name < b->name;
                         });
        _out += '{';
        for (std::size_t index = 0; index < sorted.size(); ++index)
        {
            const motion::MotionChannel& channel = *sorted[index];
            const bool ok = At(channel.name, [&] {
                if (index != 0 && sorted[index - 1]->name == channel.name)
                {
                    return Fail(WireErrorCode::KeyDuplicate, "a channel appears twice");
                }
                if (!json::IsValidUtf8(channel.name))
                {
                    return Fail(WireErrorCode::MessageMalformed,
                                "a channel name that is not UTF-8");
                }
                if (index != 0)
                {
                    _out += ',';
                }
                json::AppendString(_out, channel.name);
                _out += ':';
                return Float(channel.value);
            });
            if (!ok)
            {
                return false;
            }
        }
        _out += '}';
        return true;
    }

    bool
    Metadata(const motion::SourceMetadata& metadata)
    {
        FirstKey("kind");
        if (!At("kind",
                [&] { return Word(kKindNames, static_cast<std::size_t>(metadata.kind)); }) ||
            !Key("provider") || !At("provider", [&] { return String(metadata.provider); }) ||
            !Key("protocol") || !At("protocol", [&] { return String(metadata.protocol); }) ||
            !Key("sourceId") || !At("sourceId", [&] { return String(metadata.sourceId); }))
        {
            return false;
        }
        if (metadata.sourceTimestamp &&
            (!Key("sourceTimestamp") ||
             !At("sourceTimestamp", [&] { return Double(*metadata.sourceTimestamp); })))
        {
            return false;
        }
        if (metadata.sequenceNumber)
        {
            Key("sequenceNumber");
            Counter(*metadata.sequenceNumber);
        }
        _out += '}';
        return true;
    }

    bool
    Trackers(const std::vector<core::TrackerObservation>& trackers)
    {
        _out += '[';
        std::unordered_set<std::string_view> seen;
        for (std::size_t index = 0; index < trackers.size(); ++index)
        {
            if (index != 0)
            {
                _out += ',';
            }
            if (!AtIndex(index, [&] { return Tracker(trackers[index], &seen); }))
            {
                return false;
            }
        }
        _out += ']';
        return true;
    }

    bool
    Tracker(const core::TrackerObservation& tracker, std::unordered_set<std::string_view>* seen)
    {
        FirstKey("trackerId");
        const bool id = At("trackerId", [&] {
            if (!String(tracker.trackerId))
            {
                return false;
            }
            return seen->insert(tracker.trackerId).second ||
                   Fail(WireErrorCode::IdDuplicate, "a tracker appears twice in one actor");
        });
        if (!id)
        {
            return false;
        }
        if (tracker.hasPosition &&
            (!Key("position") || !At("position", [&] { return Vector(tracker.position); })))
        {
            return false;
        }
        if (tracker.hasRotation &&
            (!Key("rotation") || !At("rotation", [&] { return Rotation(tracker.rotation); })))
        {
            return false;
        }
        if (tracker.confidence &&
            (!Key("confidence") || !At("confidence", [&] { return Float(*tracker.confidence); })))
        {
            return false;
        }
        _out += '}';
        return true;
    }

    std::string _out;
};

} // namespace

std::string_view
WireErrorCodeName(WireErrorCode code) noexcept
{
    return WordFor(kCodeNames, static_cast<std::size_t>(code));
}

std::optional<WireErrorCode>
FindWireErrorCode(std::string_view name) noexcept
{
    const std::optional<std::size_t> index = FindWord(kCodeNames, name);
    if (!index)
    {
        return std::nullopt;
    }
    return static_cast<WireErrorCode>(*index);
}

bool
EncodeFrame(const core::MotionFrame& frame, std::string* message, WireError* error)
{
    Writer writer(error);
    if (!writer.Frame(frame))
    {
        return false;
    }
    *message = std::move(writer.Output());
    return true;
}

bool
DecodeFrame(std::string_view message, core::MotionFrame* frame, WireError* error,
            const DecodeOptions& options)
{
    if (message.size() > options.maxMessageBytes)
    {
        if (error)
        {
            error->code = WireErrorCode::MessageTooLarge;
            error->subject = "$";
            error->detail = std::to_string(message.size()) + " bytes, over the " +
                            std::to_string(options.maxMessageBytes) + "-byte maximum";
        }
        return false;
    }
    Value root;
    if (!json::Parse(message, kMaxDepth, &root, error))
    {
        return false;
    }
    core::MotionFrame result;
    if (!Reader(error).Frame(root, &result))
    {
        return false;
    }
    *frame = std::move(result);
    return true;
}

} // namespace openstrata::connectors::wire
