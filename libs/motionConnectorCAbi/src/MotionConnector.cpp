// SPDX-License-Identifier: Apache-2.0
//
// The acquisition C ABI over the concrete connectors (docs/design/C_ABI.md).
//
// Frames and diagnostics are copied out of the connector into handles of their
// own, so a consumer holds one for as long as it likes, past the next poll and
// past the connector. Nothing here interprets an observation: what a connector
// delivered is what a view reads, apart from the two boundary rules of C_ABI §7.
#include "motionConnectorCAbi/MotionConnector.h"

#include "motionConnectorCore/IMotionConnector.h"
#include "motionConnectorCore/Types.h"

#ifdef MOTIONCONNECTORCABI_HAS_VMC
#include "motionConnectorVmc/Connector.h"
#endif
#ifdef MOTIONCONNECTORCABI_HAS_MOCOPI
#include "motionConnectorMocopi/Connector.h"
#endif
#ifdef MOTIONCONNECTORCABI_HAS_VRCHAT_OSC
#include "motionConnectorVrchatOsc/Connector.h"
#endif
#ifdef MOTIONCONNECTORCABI_HAS_WEBSOCKET
#include "motionConnectorWebSocket/Connector.h"
#endif

#include "motionCore/MotionPose.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace core = openstrata::connectors::core;
namespace motion = openstrata::motion;

static_assert(sizeof(void*) == 8, "the version 1 struct layouts are defined for 64-bit targets");

static_assert(
    static_cast<std::uint32_t>(core::ConnectorState::Disconnected) == MOTION_STATE_DISCONNECTED &&
    static_cast<std::uint32_t>(core::ConnectorState::Connecting) == MOTION_STATE_CONNECTING &&
    static_cast<std::uint32_t>(core::ConnectorState::Connected) == MOTION_STATE_CONNECTED &&
    static_cast<std::uint32_t>(core::ConnectorState::Degraded) == MOTION_STATE_DEGRADED &&
    static_cast<std::uint32_t>(core::ConnectorState::Error) == MOTION_STATE_ERROR);
static_assert(
    static_cast<std::uint32_t>(core::ConnectorCapability::Body) == MOTION_CAPABILITY_BODY &&
    static_cast<std::uint32_t>(core::ConnectorCapability::Hands) == MOTION_CAPABILITY_HANDS &&
    static_cast<std::uint32_t>(core::ConnectorCapability::Face) == MOTION_CAPABILITY_FACE &&
    static_cast<std::uint32_t>(core::ConnectorCapability::Eyes) == MOTION_CAPABILITY_EYES &&
    static_cast<std::uint32_t>(core::ConnectorCapability::RootMotion) ==
        MOTION_CAPABILITY_ROOT_MOTION &&
    static_cast<std::uint32_t>(core::ConnectorCapability::Trackers) == MOTION_CAPABILITY_TRACKERS &&
    static_cast<std::uint32_t>(core::ConnectorCapability::Controllers) ==
        MOTION_CAPABILITY_CONTROLLERS &&
    static_cast<std::uint32_t>(core::ConnectorCapability::SourceTimestamps) ==
        MOTION_CAPABILITY_SOURCE_TIMESTAMPS &&
    static_cast<std::uint32_t>(core::ConnectorCapability::Confidence) ==
        MOTION_CAPABILITY_CONFIDENCE &&
    static_cast<std::uint32_t>(core::ConnectorCapability::MultipleActors) ==
        MOTION_CAPABILITY_MULTIPLE_ACTORS);
static_assert(static_cast<std::uint32_t>(core::BufferMode::Latest) == MOTION_BUFFER_LATEST &&
              static_cast<std::uint32_t>(core::BufferMode::Ordered) == MOTION_BUFFER_ORDERED &&
              static_cast<std::uint32_t>(core::BufferMode::Lossless) == MOTION_BUFFER_LOSSLESS);
static_assert(static_cast<std::uint32_t>(core::ClockDomain::Device) == MOTION_CLOCK_DEVICE &&
              static_cast<std::uint32_t>(core::ClockDomain::LocalMonotonic) ==
                  MOTION_CLOCK_LOCAL_MONOTONIC &&
              static_cast<std::uint32_t>(core::ClockDomain::Wall) == MOTION_CLOCK_WALL &&
              static_cast<std::uint32_t>(core::ClockDomain::NetworkSynchronized) ==
                  MOTION_CLOCK_NETWORK_SYNCHRONIZED &&
              static_cast<std::uint32_t>(core::ClockDomain::None) == MOTION_CLOCK_NONE);
static_assert(static_cast<std::uint32_t>(motion::MotionSourceKind::Clip) ==
                  MOTION_SOURCE_KIND_CLIP &&
              static_cast<std::uint32_t>(motion::MotionSourceKind::LiveCapture) ==
                  MOTION_SOURCE_KIND_LIVE_CAPTURE &&
              static_cast<std::uint32_t>(motion::MotionSourceKind::Generated) ==
                  MOTION_SOURCE_KIND_GENERATED &&
              static_cast<std::uint32_t>(motion::MotionSourceKind::Procedural) ==
                  MOTION_SOURCE_KIND_PROCEDURAL &&
              static_cast<std::uint32_t>(motion::MotionSourceKind::Simulated) ==
                  MOTION_SOURCE_KIND_SIMULATED);
static_assert(static_cast<std::uint8_t>(motion::FootContact::Unknown) ==
                  MOTION_FOOT_CONTACT_UNKNOWN &&
              static_cast<std::uint8_t>(motion::FootContact::NotInContact) ==
                  MOTION_FOOT_CONTACT_NOT_IN_CONTACT &&
              static_cast<std::uint8_t>(motion::FootContact::InContact) ==
                  MOTION_FOOT_CONTACT_IN_CONTACT);

namespace {

constexpr std::size_t DefaultDiagnosticCapacity = 1024;

// One owned record, whichever connector's code enum produced it.
struct DiagnosticRecord {
    motion_severity_t severity = MOTION_SEVERITY_ERROR;
    bool recoverable = false;
    std::string code;
    std::string source;
    std::string subject;
    std::string detail;
    std::optional<double> timestamp;
    std::optional<std::uint64_t> sequence;
};

// The WebSocket options, owned, with no connector type in them, so this file
// compiles the same without that source.
struct WebSocketRequest {
    motion_websocket_role_t role = MOTION_WEBSOCKET_LISTEN;
    std::string path;
    std::vector<std::string> allowedOrigins;
    std::uint64_t maxMessageBytes = 0;
    std::uint64_t readBudgetMessages = 0;
    double handshakeTimeoutSeconds = 0.0;
    double silenceTimeoutSeconds = 0.0;
    double reconnectInitialSeconds = 0.0;
    double reconnectMaxSeconds = 0.0;
};

class Source {
public:
    virtual ~Source() = default;
    virtual core::Status Open(const core::ConnectorConfig& config,
                              const std::optional<WebSocketRequest>& websocket) = 0;
    virtual core::IMotionConnector& Connector() = 0;
    virtual const core::IMotionConnector& Connector() const = 0;
    virtual core::FrameBufferStats Stats() const = 0;
    virtual std::string_view Endpoint() const = 0;
    virtual void Push(const std::uint8_t* bytes, std::size_t size, double receiveTimestamp,
                      const char* peer) = 0;
    virtual void Flush(double receiveTimestamp) = 0;
    virtual void TakeDiagnostics(std::vector<DiagnosticRecord>& out) = 0;
};

// What differs between connectors is only input and open; each trait states it.
template <class Traits> class ConcreteSource final : public Source {
public:
    core::Status Open(const core::ConnectorConfig& config,
                      const std::optional<WebSocketRequest>& websocket) override
    {
        return Traits::Open(_connector, config, websocket);
    }

    core::IMotionConnector& Connector() override { return _connector; }

    const core::IMotionConnector& Connector() const override { return _connector; }

    core::FrameBufferStats Stats() const override { return _connector.GetBufferStats(); }

    std::string_view Endpoint() const override { return _connector.GetEndpoint(); }

    void Push(const std::uint8_t* bytes, std::size_t size, double receiveTimestamp,
              const char* peer) override
    {
        Traits::Push(_connector, bytes, size, receiveTimestamp, peer);
    }

    void Flush(double receiveTimestamp) override { Traits::Flush(_connector, receiveTimestamp); }

    void TakeDiagnostics(std::vector<DiagnosticRecord>& out) override
    {
        for (const auto& diagnostic : _connector.GetDiagnostics()) {
            DiagnosticRecord record;
            record.severity = static_cast<motion_severity_t>(diagnostic.severity);
            record.recoverable = diagnostic.recoverable;
            record.code = std::string(DiagnosticCodeString(diagnostic.code));
            record.source = diagnostic.source;
            record.subject = diagnostic.subject;
            record.detail = diagnostic.detail;
            record.timestamp = diagnostic.timestamp;
            record.sequence = diagnostic.sequence;
            out.push_back(std::move(record));
        }
        _connector.ClearDiagnostics();
    }

private:
    typename Traits::Type _connector;
};

#ifdef MOTIONCONNECTORCABI_HAS_VMC
struct VmcTraits {
    using Type = openstrata::connectors::vmc::VmcConnector;

    static core::Status Open(Type& connector, const core::ConnectorConfig& config,
                             const std::optional<WebSocketRequest>&)
    {
        return connector.Open(config);
    }

    static void Push(Type& connector, const std::uint8_t* bytes, std::size_t size,
                     double receiveTimestamp, const char*)
    {
        connector.PushDatagram(bytes, size, receiveTimestamp);
    }

    static void Flush(Type& connector, double receiveTimestamp)
    {
        connector.Flush(receiveTimestamp);
    }
};
#endif

#ifdef MOTIONCONNECTORCABI_HAS_MOCOPI
struct MocopiTraits {
    using Type = openstrata::connectors::mocopi::MocopiConnector;

    static core::Status Open(Type& connector, const core::ConnectorConfig& config,
                             const std::optional<WebSocketRequest>&)
    {
        return connector.Open(config);
    }

    static void Push(Type& connector, const std::uint8_t* bytes, std::size_t size,
                     double receiveTimestamp, const char*)
    {
        connector.PushDatagram(bytes, size, receiveTimestamp);
    }

    static void Flush(Type&, double) {}
};
#endif

#ifdef MOTIONCONNECTORCABI_HAS_VRCHAT_OSC
struct VrchatOscTraits {
    using Type = openstrata::connectors::vrchatOsc::VrchatOscConnector;

    static core::Status Open(Type& connector, const core::ConnectorConfig& config,
                             const std::optional<WebSocketRequest>&)
    {
        return connector.Open(config);
    }

    static void Push(Type& connector, const std::uint8_t* bytes, std::size_t size,
                     double receiveTimestamp, const char* peer)
    {
        if (peer) {
            connector.PushDatagram(bytes, size, receiveTimestamp, peer);
        } else {
            connector.PushDatagram(bytes, size, receiveTimestamp);
        }
    }

    static void Flush(Type& connector, double) { connector.Flush(); }
};
#endif

#ifdef MOTIONCONNECTORCABI_HAS_WEBSOCKET
struct WebSocketTraits {
    using Type = openstrata::connectors::websocket::WebSocketConnector;

    static core::Status Open(Type& connector, const core::ConnectorConfig& config,
                             const std::optional<WebSocketRequest>& request)
    {
        namespace websocket = openstrata::connectors::websocket;
        websocket::WebSocketConfig options;
        options.address = config.bindAddress;
        options.port = config.port;
        if (request) {
            options.role = request->role == MOTION_WEBSOCKET_CONNECT
                               ? websocket::WebSocketRole::Connect
                               : websocket::WebSocketRole::Listen;
            if (!request->path.empty()) {
                options.path = request->path;
            }
            options.allowedOrigins = request->allowedOrigins;
            const auto setIfStated = [](auto& field, auto value) {
                if (value != 0) {
                    field = static_cast<std::decay_t<decltype(field)>>(value);
                }
            };
            setIfStated(options.maxMessageBytes, request->maxMessageBytes);
            setIfStated(options.readBudgetMessages, request->readBudgetMessages);
            setIfStated(options.handshakeTimeoutSeconds, request->handshakeTimeoutSeconds);
            setIfStated(options.silenceTimeoutSeconds, request->silenceTimeoutSeconds);
            setIfStated(options.reconnectInitialSeconds, request->reconnectInitialSeconds);
            setIfStated(options.reconnectMaxSeconds, request->reconnectMaxSeconds);
        }
        return connector.Open(config, options);
    }

    static void Push(Type& connector, const std::uint8_t* bytes, std::size_t size,
                     double receiveTimestamp, const char* peer)
    {
        connector.PushMessage(std::string_view(reinterpret_cast<const char*>(bytes), size),
                              receiveTimestamp,
                              peer ? std::string_view(peer) : std::string_view());
    }

    static void Flush(Type&, double) {}
};
#endif

template <class Traits>
std::unique_ptr<Source>
MakeSource()
{
    return std::make_unique<ConcreteSource<Traits>>();
}

struct SourceEntry {
    std::string_view id;
    std::string_view defaultProfile;
    std::uint16_t defaultPort;
    std::unique_ptr<Source> (*make)();
};

// The same names, profiles and ports as `motion_connect list`
// (MOTION_CONNECT.md). OpenXR is not here: its handles are the caller's, and
// C_ABI §2 says why it is out of version 1.
constexpr SourceEntry Sources[] = {
#ifdef MOTIONCONNECTORCABI_HAS_VMC
    {"vmc", "vmc.v1", 39539, &MakeSource<VmcTraits>},
#endif
#ifdef MOTIONCONNECTORCABI_HAS_MOCOPI
    {"mocopi", "mocopi.body.v1", 12351, &MakeSource<MocopiTraits>},
#endif
#ifdef MOTIONCONNECTORCABI_HAS_VRCHAT_OSC
    {"vrchat-osc", "vrchat-osc.trackers.v1", 9001, &MakeSource<VrchatOscTraits>},
#endif
#ifdef MOTIONCONNECTORCABI_HAS_WEBSOCKET
    {"websocket", "", 0, &MakeSource<WebSocketTraits>},
#endif
};
constexpr std::size_t SourceCount = sizeof(Sources) / sizeof(Sources[0]);

motion_string_view_t
View(std::string_view text) noexcept
{
    // An empty view of a default string_view has no data; `data` is promised.
    return {text.data() ? text.data() : "", static_cast<std::uint64_t>(text.size())};
}

// A view of a whole string also promises the NUL after it, which only an
// owning string or a literal supplies.
motion_string_view_t
View(const std::string& text) noexcept
{
    return {text.c_str(), static_cast<std::uint64_t>(text.size())};
}

// Strict RFC 3629: no overlong form, no surrogate, nothing above U+10FFFF.
// Returns the length of the valid sequence at `index`, or 0.
std::size_t
Utf8SequenceLength(std::string_view text, std::size_t index) noexcept
{
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const unsigned char lead = byte(index);
    std::size_t length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (lead < 0x80) {
        return 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        low = lead == 0xE0 ? 0xA0 : 0x80;
        high = lead == 0xED ? 0x9F : 0xBF;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        low = lead == 0xF0 ? 0x90 : 0x80;
        high = lead == 0xF4 ? 0x8F : 0xBF;
    } else {
        return 0;
    }
    if (index + length > text.size() || byte(index + 1) < low || byte(index + 1) > high) {
        return 0;
    }
    for (std::size_t offset = 2; offset < length; ++offset) {
        if (byte(index + offset) < 0x80 || byte(index + offset) > 0xBF) {
            return 0;
        }
    }
    return length;
}

bool
IsUtf8(std::string_view text) noexcept
{
    for (std::size_t index = 0; index < text.size();) {
        const std::size_t length = Utf8SequenceLength(text, index);
        if (length == 0) {
            return false;
        }
        index += length;
    }
    return true;
}

// A report's text, not an identity: a refused address may be the very bytes a
// diagnostic quotes, and losing the report would lose why (C_ABI §7).
std::string
ReplaceInvalidUtf8(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        const std::size_t length = Utf8SequenceLength(text, index);
        if (length == 0) {
            out += "\xEF\xBF\xBD";
            ++index;
        } else {
            out.append(text.substr(index, length));
            index += length;
        }
    }
    return out;
}

// Names the first field that is not UTF-8, or nullopt.
std::optional<std::string>
FindNonUtf8Field(const core::MotionFrame& frame)
{
    if (!IsUtf8(frame.sourceProfile)) {
        return "sourceProfile";
    }
    for (std::size_t a = 0; a < frame.actors.size(); ++a) {
        const core::ActorFrame& actor = frame.actors[a];
        const std::string where = "actors[" + std::to_string(a) + "]";
        if (!IsUtf8(actor.actor)) {
            return where + ".actor";
        }
        for (std::size_t t = 0; t < actor.trackers.size(); ++t) {
            if (!IsUtf8(actor.trackers[t].trackerId)) {
                return where + ".trackers[" + std::to_string(t) + "].trackerId";
            }
        }
        if (!actor.pose) {
            continue;
        }
        const motion::SourceMetadata& metadata = actor.pose->metadata;
        if (!IsUtf8(metadata.provider)) {
            return where + ".pose.metadata.provider";
        }
        if (!IsUtf8(metadata.protocol)) {
            return where + ".pose.metadata.protocol";
        }
        if (!IsUtf8(metadata.sourceId)) {
            return where + ".pose.metadata.sourceId";
        }
        const auto& channels = actor.pose->channels.entries;
        for (std::size_t c = 0; c < channels.size(); ++c) {
            if (!IsUtf8(channels[c].name)) {
                return where + ".pose.channels[" + std::to_string(c) + "].name";
            }
        }
    }
    return std::nullopt;
}

void
Store(const pxr::GfVec3f& value, float (&out)[3]) noexcept
{
    out[0] = value[0];
    out[1] = value[1];
    out[2] = value[2];
}

void
Store(const pxr::GfQuatf& value, float (&out)[4]) noexcept
{
    const pxr::GfVec3f& imaginary = value.GetImaginary();
    out[0] = value.GetReal();
    out[1] = imaginary[0];
    out[2] = imaginary[1];
    out[3] = imaginary[2];
}

// A pose's arrays, laid out for the view. An absent joint stays zero.
struct PoseArrays {
    float rotations[motion::HumanJointCount][4] = {};
    std::uint8_t valid[motion::HumanJointCount] = {};
    float confidence[motion::HumanJointCount] = {};
};

// `struct_size` carries the caller's size in and the written size out.
template <class Struct>
motion_status_t
WriteOut(const Struct& value, Struct* out) noexcept
{
    if (!out) {
        return MOTION_STATUS_INVALID_ARGUMENT;
    }
    if (out->struct_size < sizeof(Struct)) {
        return MOTION_STATUS_UNSUPPORTED_STRUCT;
    }
    std::memcpy(out, &value, sizeof(Struct));
    out->struct_size = static_cast<std::uint32_t>(sizeof(Struct));
    return MOTION_STATUS_OK;
}

template <class Struct>
Struct
ZeroedOut() noexcept
{
    Struct value{};
    value.struct_size = static_cast<std::uint32_t>(sizeof(Struct));
    return value;
}

// Runs one call body and stops every exception here.
template <class Body>
motion_status_t
Guarded(Body&& body) noexcept
{
    try {
        return body();
    } catch (const std::bad_alloc&) {
        return MOTION_STATUS_OUT_OF_MEMORY;
    } catch (...) {
        return MOTION_STATUS_INTERNAL;
    }
}

} // namespace

struct motion_frame_t {
    core::MotionFrame value;
    // Parallel to value.actors; null for an actor with no pose.
    std::vector<std::unique_ptr<PoseArrays>> poses;
};

struct motion_diagnostic_t {
    DiagnosticRecord record;
};

struct motion_connector_t {
    const SourceEntry* entry = nullptr;
    std::unique_ptr<Source> source;
    std::string sourceProfile;
    std::string lastError;
    std::size_t diagnosticCapacity = DefaultDiagnosticCapacity;
    std::deque<DiagnosticRecord> diagnostics;
    std::uint64_t refusedFrames = 0;
    std::uint64_t droppedDiagnostics = 0;
};

namespace {

void
Enqueue(motion_connector_t& connector, DiagnosticRecord record)
{
    record.source = ReplaceInvalidUtf8(record.source);
    record.subject = ReplaceInvalidUtf8(record.subject);
    record.detail = ReplaceInvalidUtf8(record.detail);
    while (connector.diagnostics.size() >= connector.diagnosticCapacity) {
        connector.diagnostics.pop_front();
        ++connector.droppedDiagnostics;
    }
    connector.diagnostics.push_back(std::move(record));
}

void
DrainDiagnostics(motion_connector_t& connector)
{
    std::vector<DiagnosticRecord> taken;
    connector.source->TakeDiagnostics(taken);
    for (DiagnosticRecord& record : taken) {
        Enqueue(connector, std::move(record));
    }
}

void
RefuseFrame(motion_connector_t& connector, const core::MotionFrame& frame, std::string field)
{
    ++connector.refusedFrames;
    DiagnosticRecord record;
    record.severity = MOTION_SEVERITY_WARNING;
    record.recoverable = true;
    record.code = "C_ABI_FRAME_NOT_UTF8";
    record.source = std::string(connector.entry->id);
    record.subject = std::move(field);
    record.sequence = frame.frameNumber;
    record.detail = "frame refused at the C ABI: this field is not valid UTF-8";
    Enqueue(connector, std::move(record));
}

std::unique_ptr<motion_frame_t>
MakeFrame(core::MotionFrame value)
{
    auto frame = std::make_unique<motion_frame_t>();
    frame->value = std::move(value);
    frame->poses.reserve(frame->value.actors.size());
    for (const core::ActorFrame& actor : frame->value.actors) {
        if (!actor.pose) {
            frame->poses.push_back(nullptr);
            continue;
        }
        auto arrays = std::make_unique<PoseArrays>();
        for (std::size_t joint = 0; joint < motion::HumanJointCount; ++joint) {
            if (actor.pose->validRotations.test(joint)) {
                arrays->valid[joint] = 1;
                Store(actor.pose->localRotations[joint], arrays->rotations[joint]);
            }
        }
        if (actor.pose->confidence) {
            std::copy(
                actor.pose->confidence->begin(), actor.pose->confidence->end(), arrays->confidence);
        }
        frame->poses.push_back(std::move(arrays));
    }
    return frame;
}

bool
FiniteAndNotNegative(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0;
}

// NULL is an absent string; anything else is a NUL-terminated UTF-8 string.
bool
ReadString(const char* text, std::string& out)
{
    if (!text) {
        out.clear();
        return true;
    }
    out = text;
    return IsUtf8(out);
}

motion_status_t
Refuse(motion_connector_t& connector, motion_status_t status, std::string_view why)
{
    connector.lastError = ReplaceInvalidUtf8(why);
    return status;
}

// `websocket` is the caller's; everything it says is copied or refused here.
motion_status_t
ReadWebSocketOptions(motion_connector_t& connector, const motion_websocket_options_t& websocket,
                     WebSocketRequest& out)
{
    if (websocket.struct_size != sizeof(motion_websocket_options_t)) {
        return Refuse(connector,
                      MOTION_STATUS_UNSUPPORTED_STRUCT,
                      "motion_websocket_options_t has an unsupported struct_size");
    }
    if (websocket.role != MOTION_WEBSOCKET_LISTEN && websocket.role != MOTION_WEBSOCKET_CONNECT) {
        return Refuse(connector, MOTION_STATUS_INVALID_ARGUMENT, "unknown WebSocket role");
    }
    if (!FiniteAndNotNegative(websocket.handshake_timeout_seconds) ||
        !FiniteAndNotNegative(websocket.silence_timeout_seconds) ||
        !FiniteAndNotNegative(websocket.reconnect_initial_seconds) ||
        !FiniteAndNotNegative(websocket.reconnect_max_seconds)) {
        return Refuse(connector,
                      MOTION_STATUS_INVALID_ARGUMENT,
                      "a WebSocket duration is negative or not finite");
    }
    if (!websocket.allowed_origins && websocket.allowed_origin_count != 0) {
        return Refuse(connector,
                      MOTION_STATUS_INVALID_ARGUMENT,
                      "allowed_origins is NULL with a non-zero count");
    }
    out.role = websocket.role;
    if (!ReadString(websocket.path, out.path)) {
        return Refuse(connector, MOTION_STATUS_INVALID_ARGUMENT, "path is not UTF-8");
    }
    out.allowedOrigins.reserve(static_cast<std::size_t>(websocket.allowed_origin_count));
    for (std::uint64_t index = 0; index < websocket.allowed_origin_count; ++index) {
        std::string origin;
        if (!websocket.allowed_origins[index] ||
            !ReadString(websocket.allowed_origins[index], origin)) {
            return Refuse(connector,
                          MOTION_STATUS_INVALID_ARGUMENT,
                          "an allowed origin is NULL or not UTF-8");
        }
        out.allowedOrigins.push_back(std::move(origin));
    }
    out.maxMessageBytes = websocket.max_message_bytes;
    out.readBudgetMessages = websocket.read_budget_messages;
    out.handshakeTimeoutSeconds = websocket.handshake_timeout_seconds;
    out.silenceTimeoutSeconds = websocket.silence_timeout_seconds;
    out.reconnectInitialSeconds = websocket.reconnect_initial_seconds;
    out.reconnectMaxSeconds = websocket.reconnect_max_seconds;
    return MOTION_STATUS_OK;
}

const core::ActorFrame*
FindActor(const motion_frame_t* frame, std::uint64_t index) noexcept
{
    return frame && index < frame->value.actors.size()
               ? &frame->value.actors[static_cast<std::size_t>(index)]
               : nullptr;
}

} // namespace

extern "C" {

uint32_t
motion_abi_version(void)
{
    return MOTION_ABI_VERSION;
}

const char*
motion_status_name(motion_status_t status)
{
    switch (status) {
    case MOTION_STATUS_OK:
        return "MOTION_STATUS_OK";
    case MOTION_STATUS_EMPTY:
        return "MOTION_STATUS_EMPTY";
    case MOTION_STATUS_INVALID_ARGUMENT:
        return "MOTION_STATUS_INVALID_ARGUMENT";
    case MOTION_STATUS_UNSUPPORTED_STRUCT:
        return "MOTION_STATUS_UNSUPPORTED_STRUCT";
    case MOTION_STATUS_UNKNOWN_SOURCE:
        return "MOTION_STATUS_UNKNOWN_SOURCE";
    case MOTION_STATUS_OPEN_FAILED:
        return "MOTION_STATUS_OPEN_FAILED";
    case MOTION_STATUS_OUT_OF_RANGE:
        return "MOTION_STATUS_OUT_OF_RANGE";
    case MOTION_STATUS_NOT_AVAILABLE:
        return "MOTION_STATUS_NOT_AVAILABLE";
    case MOTION_STATUS_OUT_OF_MEMORY:
        return "MOTION_STATUS_OUT_OF_MEMORY";
    case MOTION_STATUS_INTERNAL:
        return "MOTION_STATUS_INTERNAL";
    }
    return "";
}

uint64_t
motion_joint_count(void)
{
    return motion::HumanJointCount;
}

uint32_t
motion_joint_vocabulary_version(void)
{
    return static_cast<std::uint32_t>(motion::HumanJointVocabularyVersion);
}

motion_status_t
motion_joint_name(uint64_t joint_index, motion_string_view_t* out_name)
{
    if (!out_name) {
        return MOTION_STATUS_INVALID_ARGUMENT;
    }
    if (joint_index >= motion::HumanJointCount) {
        return MOTION_STATUS_OUT_OF_RANGE;
    }
    *out_name = View(motion::HumanJointName(static_cast<motion::HumanJoint>(joint_index)));
    return MOTION_STATUS_OK;
}

uint64_t
motion_source_count(void)
{
    return SourceCount;
}

motion_status_t
motion_source_get_info(uint64_t source_index, motion_source_info_t* out_info)
{
    return Guarded([&]() -> motion_status_t {
        if (!out_info) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        if (source_index >= SourceCount) {
            return MOTION_STATUS_OUT_OF_RANGE;
        }
        const SourceEntry& entry = Sources[static_cast<std::size_t>(source_index)];
        auto info = ZeroedOut<motion_source_info_t>();
        info.capabilities = entry.make()->Connector().GetCapabilities().Bits();
        info.id = View(entry.id);
        info.default_profile = View(entry.defaultProfile);
        info.default_port = entry.defaultPort;
        return WriteOut(info, out_info);
    });
}

motion_status_t
motion_connector_create(const char* source_id, motion_connector_t** out_connector)
{
    return Guarded([&]() -> motion_status_t {
        if (!out_connector) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        *out_connector = nullptr;
        if (!source_id) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        for (const SourceEntry& entry : Sources) {
            if (entry.id == source_id) {
                auto connector = std::make_unique<motion_connector_t>();
                connector->entry = &entry;
                connector->source = entry.make();
                connector->sourceProfile = std::string(entry.defaultProfile);
                *out_connector = connector.release();
                return MOTION_STATUS_OK;
            }
        }
        return MOTION_STATUS_UNKNOWN_SOURCE;
    });
}

void
motion_connector_destroy(motion_connector_t* connector)
{
    try {
        delete connector;
    } catch (...) {
        // A destructor that throws has nowhere to report across this boundary.
    }
}

motion_status_t
motion_connector_open(motion_connector_t* connector, const motion_connector_config_t* config)
{
    return Guarded([&]() -> motion_status_t {
        if (!connector || !config) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        connector->lastError.clear();
        if (config->struct_size != sizeof(motion_connector_config_t)) {
            return Refuse(*connector,
                          MOTION_STATUS_UNSUPPORTED_STRUCT,
                          "motion_connector_config_t has an unsupported struct_size");
        }
        if (config->buffer_mode > MOTION_BUFFER_LOSSLESS) {
            return Refuse(*connector, MOTION_STATUS_INVALID_ARGUMENT, "unknown buffer mode");
        }
        if (config->buffer_capacity == 0) {
            return Refuse(
                *connector, MOTION_STATUS_INVALID_ARGUMENT, "buffer_capacity must not be 0");
        }

        core::ConnectorConfig stated;
        std::string address;
        std::string profile;
        if (!ReadString(config->address, address) || !ReadString(config->source_profile, profile) ||
            !ReadString(config->coordinate_conversion, stated.coordinateConversion)) {
            return Refuse(
                *connector, MOTION_STATUS_INVALID_ARGUMENT, "a configuration string is not UTF-8");
        }
        if (config->address) {
            stated.bindAddress = std::move(address);
        }
        stated.sourceProfile = config->source_profile
                                   ? std::move(profile)
                                   : std::string(connector->entry->defaultProfile);
        stated.port = config->port;
        stated.bufferMode = static_cast<core::BufferMode>(config->buffer_mode);
        stated.bufferCapacity = static_cast<std::size_t>(config->buffer_capacity);

        std::optional<WebSocketRequest> websocket;
        if (config->websocket) {
            if (connector->entry->id != "websocket") {
                return Refuse(*connector,
                              MOTION_STATUS_INVALID_ARGUMENT,
                              "websocket options on a source that is not websocket");
            }
            websocket.emplace();
            const motion_status_t status =
                ReadWebSocketOptions(*connector, *config->websocket, *websocket);
            if (status != MOTION_STATUS_OK) {
                return status;
            }
        }

        connector->diagnosticCapacity = config->diagnostic_capacity != 0
                                            ? static_cast<std::size_t>(config->diagnostic_capacity)
                                            : DefaultDiagnosticCapacity;
        connector->sourceProfile = stated.sourceProfile;
        const core::Status status = connector->source->Open(stated, websocket);
        DrainDiagnostics(*connector);
        if (!status) {
            return Refuse(*connector, MOTION_STATUS_OPEN_FAILED, status.message);
        }
        return MOTION_STATUS_OK;
    });
}

motion_status_t
motion_connector_close(motion_connector_t* connector)
{
    return Guarded([&]() -> motion_status_t {
        if (!connector) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        connector->lastError.clear();
        connector->source->Connector().Close();
        DrainDiagnostics(*connector);
        return MOTION_STATUS_OK;
    });
}

motion_status_t
motion_connector_get_state(const motion_connector_t* connector, motion_connector_state_t* out_state)
{
    return Guarded([&]() -> motion_status_t {
        if (!connector || !out_state) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        *out_state =
            static_cast<motion_connector_state_t>(connector->source->Connector().GetState());
        return MOTION_STATUS_OK;
    });
}

motion_status_t
motion_connector_get_capabilities(const motion_connector_t* connector,
                                  motion_capabilities_t* out_capabilities)
{
    return Guarded([&]() -> motion_status_t {
        if (!connector || !out_capabilities) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        *out_capabilities = connector->source->Connector().GetCapabilities().Bits();
        return MOTION_STATUS_OK;
    });
}

motion_status_t
motion_connector_get_source_profile(const motion_connector_t* connector,
                                    motion_string_view_t* out_profile)
{
    if (!connector || !out_profile) {
        return MOTION_STATUS_INVALID_ARGUMENT;
    }
    *out_profile = View(connector->sourceProfile);
    return MOTION_STATUS_OK;
}

motion_status_t
motion_connector_get_endpoint(const motion_connector_t* connector,
                              motion_string_view_t* out_endpoint)
{
    return Guarded([&]() -> motion_status_t {
        if (!connector || !out_endpoint) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        *out_endpoint = View(connector->source->Endpoint());
        return MOTION_STATUS_OK;
    });
}

motion_status_t
motion_connector_last_error(const motion_connector_t* connector, motion_string_view_t* out_error)
{
    if (!connector || !out_error) {
        return MOTION_STATUS_INVALID_ARGUMENT;
    }
    *out_error = View(connector->lastError);
    return MOTION_STATUS_OK;
}

motion_status_t
motion_connector_get_stats(const motion_connector_t* connector, motion_connector_stats_t* out_stats)
{
    return Guarded([&]() -> motion_status_t {
        if (!connector) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        const core::FrameBufferStats buffer = connector->source->Stats();
        auto stats = ZeroedOut<motion_connector_stats_t>();
        stats.pushed_frames = buffer.pushedFrames;
        stats.polled_frames = buffer.polledFrames;
        stats.dropped_frames = buffer.droppedFrames;
        stats.skipped_frames = buffer.skippedFrames;
        stats.rejected_frames = buffer.rejectedFrames;
        stats.refused_frames = connector->refusedFrames;
        stats.dropped_diagnostics = connector->droppedDiagnostics;
        return WriteOut(stats, out_stats);
    });
}

motion_status_t
motion_connector_poll(motion_connector_t* connector, motion_frame_t** out_frame)
{
    return Guarded([&]() -> motion_status_t {
        if (!out_frame) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        *out_frame = nullptr;
        if (!connector) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        connector->lastError.clear();
        for (;;) {
            core::MotionFrame frame;
            const bool polled = connector->source->Connector().Poll(frame);
            DrainDiagnostics(*connector);
            if (!polled) {
                return MOTION_STATUS_EMPTY;
            }
            if (std::optional<std::string> field = FindNonUtf8Field(frame)) {
                RefuseFrame(*connector, frame, std::move(*field));
                continue;
            }
            *out_frame = MakeFrame(std::move(frame)).release();
            return MOTION_STATUS_OK;
        }
    });
}

motion_status_t
motion_connector_poll_diagnostic(motion_connector_t* connector,
                                 motion_diagnostic_t** out_diagnostic)
{
    return Guarded([&]() -> motion_status_t {
        if (!out_diagnostic) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        *out_diagnostic = nullptr;
        if (!connector) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        connector->lastError.clear();
        DrainDiagnostics(*connector);
        if (connector->diagnostics.empty()) {
            return MOTION_STATUS_EMPTY;
        }
        auto diagnostic = std::make_unique<motion_diagnostic_t>();
        diagnostic->record = std::move(connector->diagnostics.front());
        connector->diagnostics.pop_front();
        *out_diagnostic = diagnostic.release();
        return MOTION_STATUS_OK;
    });
}

motion_status_t
motion_connector_push_input(motion_connector_t* connector, const uint8_t* bytes, uint64_t size,
                            double receive_timestamp, const char* peer)
{
    return Guarded([&]() -> motion_status_t {
        if (!connector) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        connector->lastError.clear();
        if ((!bytes && size != 0) || !std::isfinite(receive_timestamp)) {
            return Refuse(*connector,
                          MOTION_STATUS_INVALID_ARGUMENT,
                          "bytes is NULL with a non-zero size, or the timestamp is not finite");
        }
        connector->source->Push(bytes, static_cast<std::size_t>(size), receive_timestamp, peer);
        DrainDiagnostics(*connector);
        return MOTION_STATUS_OK;
    });
}

motion_status_t
motion_connector_flush_input(motion_connector_t* connector, double receive_timestamp)
{
    return Guarded([&]() -> motion_status_t {
        if (!connector) {
            return MOTION_STATUS_INVALID_ARGUMENT;
        }
        connector->lastError.clear();
        if (!std::isfinite(receive_timestamp)) {
            return Refuse(
                *connector, MOTION_STATUS_INVALID_ARGUMENT, "the timestamp is not finite");
        }
        connector->source->Flush(receive_timestamp);
        DrainDiagnostics(*connector);
        return MOTION_STATUS_OK;
    });
}

void
motion_frame_release(motion_frame_t* frame)
{
    delete frame;
}

motion_status_t
motion_frame_get_info(const motion_frame_t* frame, motion_frame_info_t* out_info)
{
    if (!frame) {
        return MOTION_STATUS_INVALID_ARGUMENT;
    }
    const core::MotionFrame& value = frame->value;
    auto info = ZeroedOut<motion_frame_info_t>();
    info.source_clock = static_cast<motion_clock_domain_t>(value.timing.sourceClock);
    info.frame_number = value.frameNumber;
    info.actor_count = value.actors.size();
    info.receive_timestamp = value.timing.receiveTimestamp;
    if (value.timing.sourceTimestamp) {
        info.has_source_timestamp = 1;
        info.source_timestamp = *value.timing.sourceTimestamp;
    }
    if (value.timing.sequence) {
        info.has_sequence = 1;
        info.sequence = *value.timing.sequence;
    }
    info.source_profile = View(value.sourceProfile);
    return WriteOut(info, out_info);
}

motion_status_t
motion_frame_get_actor(const motion_frame_t* frame, uint64_t actor_index,
                       motion_actor_view_t* out_actor)
{
    if (!frame) {
        return MOTION_STATUS_INVALID_ARGUMENT;
    }
    const core::ActorFrame* actor = FindActor(frame, actor_index);
    if (!actor) {
        return MOTION_STATUS_OUT_OF_RANGE;
    }
    auto view = ZeroedOut<motion_actor_view_t>();
    view.actor_id = View(actor->actor);
    view.tracker_count = actor->trackers.size();
    if (actor->pose) {
        view.has_pose = 1;
        view.channel_count = actor->pose->channels.entries.size();
    }
    return WriteOut(view, out_actor);
}

motion_status_t
motion_frame_get_pose(const motion_frame_t* frame, uint64_t actor_index,
                      motion_pose_view_t* out_pose)
{
    if (!frame) {
        return MOTION_STATUS_INVALID_ARGUMENT;
    }
    const core::ActorFrame* actor = FindActor(frame, actor_index);
    if (!actor) {
        return MOTION_STATUS_OUT_OF_RANGE;
    }
    if (!actor->pose) {
        return MOTION_STATUS_NOT_AVAILABLE;
    }
    const motion::MotionPose& pose = *actor->pose;
    const PoseArrays& arrays = *frame->poses[static_cast<std::size_t>(actor_index)];

    auto view = ZeroedOut<motion_pose_view_t>();
    view.source_kind = static_cast<motion_source_kind_t>(pose.metadata.kind);
    view.timestamp = pose.timestamp;
    if (pose.root.hasPosition) {
        view.has_root_position = 1;
        Store(pose.root.worldPosition, view.root_world_position);
    }
    if (pose.root.hasOrientation) {
        view.has_root_orientation = 1;
        Store(pose.root.worldOrientation, view.root_world_orientation);
    }
    if (pose.root.hasLinearVelocity) {
        view.has_root_linear_velocity = 1;
        Store(pose.root.linearVelocity, view.root_linear_velocity);
    }
    if (pose.root.hasAngularVelocity) {
        view.has_root_angular_velocity = 1;
        Store(pose.root.angularVelocity, view.root_angular_velocity);
    }
    if (pose.lookAtTarget) {
        view.has_look_at_target = 1;
        Store(*pose.lookAtTarget, view.look_at_target);
    }
    view.joint_count = motion::HumanJointCount;
    view.local_rotations = arrays.rotations;
    view.valid_rotations = arrays.valid;
    view.confidence = pose.confidence ? arrays.confidence : nullptr;
    view.provider = View(pose.metadata.provider);
    view.protocol = View(pose.metadata.protocol);
    view.source_id = View(pose.metadata.sourceId);
    if (pose.metadata.sourceTimestamp) {
        view.has_metadata_source_timestamp = 1;
        view.metadata_source_timestamp = *pose.metadata.sourceTimestamp;
    }
    if (pose.metadata.sequenceNumber) {
        view.has_metadata_sequence_number = 1;
        view.metadata_sequence_number = *pose.metadata.sequenceNumber;
    }
    if (pose.contacts) {
        view.has_contacts = 1;
        view.left_foot_contact = static_cast<motion_foot_contact_t>(pose.contacts->leftFoot);
        view.right_foot_contact = static_cast<motion_foot_contact_t>(pose.contacts->rightFoot);
    }
    return WriteOut(view, out_pose);
}

motion_status_t
motion_frame_get_channel(const motion_frame_t* frame, uint64_t actor_index, uint64_t channel_index,
                         motion_channel_view_t* out_channel)
{
    if (!frame) {
        return MOTION_STATUS_INVALID_ARGUMENT;
    }
    const core::ActorFrame* actor = FindActor(frame, actor_index);
    if (!actor) {
        return MOTION_STATUS_OUT_OF_RANGE;
    }
    if (!actor->pose) {
        return MOTION_STATUS_NOT_AVAILABLE;
    }
    const auto& entries = actor->pose->channels.entries;
    if (channel_index >= entries.size()) {
        return MOTION_STATUS_OUT_OF_RANGE;
    }
    const motion::MotionChannel& channel = entries[static_cast<std::size_t>(channel_index)];
    auto view = ZeroedOut<motion_channel_view_t>();
    view.value = channel.value;
    view.name = View(channel.name);
    return WriteOut(view, out_channel);
}

motion_status_t
motion_frame_get_tracker(const motion_frame_t* frame, uint64_t actor_index, uint64_t tracker_index,
                         motion_tracker_view_t* out_tracker)
{
    if (!frame) {
        return MOTION_STATUS_INVALID_ARGUMENT;
    }
    const core::ActorFrame* actor = FindActor(frame, actor_index);
    if (!actor || tracker_index >= actor->trackers.size()) {
        return MOTION_STATUS_OUT_OF_RANGE;
    }
    const core::TrackerObservation& tracker =
        actor->trackers[static_cast<std::size_t>(tracker_index)];
    auto view = ZeroedOut<motion_tracker_view_t>();
    view.tracker_id = View(tracker.trackerId);
    if (tracker.hasPosition) {
        view.has_position = 1;
        Store(tracker.position, view.position);
    }
    if (tracker.hasRotation) {
        view.has_rotation = 1;
        Store(tracker.rotation, view.rotation);
    }
    if (tracker.confidence) {
        view.has_confidence = 1;
        view.confidence = *tracker.confidence;
    }
    return WriteOut(view, out_tracker);
}

void
motion_diagnostic_release(motion_diagnostic_t* diagnostic)
{
    delete diagnostic;
}

motion_status_t
motion_diagnostic_get_view(const motion_diagnostic_t* diagnostic,
                           motion_diagnostic_view_t* out_view)
{
    if (!diagnostic) {
        return MOTION_STATUS_INVALID_ARGUMENT;
    }
    const DiagnosticRecord& record = diagnostic->record;
    auto view = ZeroedOut<motion_diagnostic_view_t>();
    view.severity = record.severity;
    view.code = View(record.code);
    view.source = View(record.source);
    view.subject = View(record.subject);
    view.detail = View(record.detail);
    view.recoverable = record.recoverable ? 1 : 0;
    if (record.timestamp) {
        view.has_timestamp = 1;
        view.timestamp = *record.timestamp;
    }
    if (record.sequence) {
        view.has_sequence = 1;
        view.sequence = *record.sequence;
    }
    return WriteOut(view, out_view);
}

} // extern "C"
