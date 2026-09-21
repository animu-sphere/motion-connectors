// SPDX-License-Identifier: Apache-2.0
//
// The values shared by connectors and their consumers. This header contains
// no transport, protocol or avatar-specific behavior.
#pragma once

#include "motionConnectorCore/api.h"

#include "motionCore/MotionPose.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openstrata::connectors::core
{

struct Status
{
    bool succeeded = true;
    std::string message;

    static Status
    Ok()
    {
        return {};
    }

    static Status
    Failure(std::string detail)
    {
        return {false, std::move(detail)};
    }

    explicit operator bool() const noexcept
    {
        return succeeded;
    }
};

using ActorId = std::string;

enum class ConnectorState : std::uint8_t
{
    Disconnected,
    Connecting,
    Connected,
    Degraded,
    Error,
};

enum class ConnectorCapability : std::uint32_t
{
    Body = 1u << 0,
    Hands = 1u << 1,
    Face = 1u << 2,
    Eyes = 1u << 3,
    RootMotion = 1u << 4,
    Trackers = 1u << 5,
    Controllers = 1u << 6,
    SourceTimestamps = 1u << 7,
    Confidence = 1u << 8,
    MultipleActors = 1u << 9,
};

class ConnectorCapabilities final
{
  public:
    constexpr ConnectorCapabilities() noexcept = default;

    constexpr explicit ConnectorCapabilities(ConnectorCapability capability) noexcept
        : _bits(static_cast<std::uint32_t>(capability))
    {
    }

    constexpr explicit ConnectorCapabilities(std::uint32_t bits) noexcept : _bits(bits) {}

    constexpr bool
    Has(ConnectorCapability capability) const noexcept
    {
        return (_bits & static_cast<std::uint32_t>(capability)) != 0;
    }

    constexpr void
    Add(ConnectorCapability capability) noexcept
    {
        _bits |= static_cast<std::uint32_t>(capability);
    }

    constexpr std::uint32_t
    Bits() const noexcept
    {
        return _bits;
    }

    friend constexpr bool
    operator==(ConnectorCapabilities lhs, ConnectorCapabilities rhs) noexcept
    {
        return lhs._bits == rhs._bits;
    }

    friend constexpr bool
    operator!=(ConnectorCapabilities lhs, ConnectorCapabilities rhs) noexcept
    {
        return !(lhs == rhs);
    }

  private:
    std::uint32_t _bits = 0;
};

enum class BufferMode : std::uint8_t
{
    Latest,
    Ordered,
    Lossless,
};

struct ConnectorConfig
{
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 0;
    std::string sourceProfile;
    std::string coordinateConversion;
    BufferMode bufferMode = BufferMode::Latest;
    std::size_t bufferCapacity = 1;
};

enum class ClockDomain : std::uint8_t
{
    Device,
    LocalMonotonic,
    Wall,
    NetworkSynchronized,
    None,
};

struct FrameTiming
{
    std::optional<double> sourceTimestamp;
    double receiveTimestamp = 0.0;
    std::optional<std::uint64_t> sequence;
    ClockDomain sourceClock = ClockDomain::None;
};

struct TrackerObservation
{
    std::string trackerId;
    pxr::GfVec3f position{0.0f, 0.0f, 0.0f};
    pxr::GfQuatf rotation = pxr::GfQuatf::GetIdentity();
    bool hasPosition = false;
    bool hasRotation = false;
    std::optional<float> confidence;
};

struct ActorFrame
{
    ActorId actor;
    std::optional<openstrata::motion::MotionPose> pose;
    std::vector<TrackerObservation> trackers;
};

struct MotionFrame
{
    std::vector<ActorFrame> actors;
    FrameTiming timing;
    std::string sourceProfile;
    std::uint64_t frameNumber = 0;
};

struct FrameBufferStats
{
    std::uint64_t pushedFrames = 0;
    std::uint64_t polledFrames = 0;
    std::uint64_t droppedFrames = 0;
    std::uint64_t skippedFrames = 0;
    std::uint64_t rejectedFrames = 0;
};

enum class FramePushResult : std::uint8_t
{
    Accepted,
    Backpressure,
};

} // namespace openstrata::connectors::core