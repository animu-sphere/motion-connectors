// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionConnectorMocopi/FrameAssembler.h"
#include "motionConnectorMocopi/MotionPacket.h"
#include "motionConnectorMocopi/api.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openstrata::connectors::mocopi {

struct MocopiFrameSourceStats {
    std::uint64_t datagramsDecoded = 0;
    std::uint64_t datagramsRefused = 0;
    std::uint64_t bonesRefused = 0;
    std::uint64_t framesDelivered = 0;
};

// Acquisition only: decode, normalize, assemble and report source facts.
// Restart handling, sampling and semantic recording belong to the consumer.
class MOTIONCONNECTORMOCOPI_API MocopiFrameSource {
public:
    explicit MocopiFrameSource(const MocopiFrameConfig& config = {});
    void SetSource(std::string source);

    // Returns emitted observations, including the first frame after a restart.
    // Appended diagnostics name the received datagram, including refusals.
    std::size_t PushDatagram(const std::uint8_t* bytes, std::size_t size, double receiveTime,
                             std::vector<Diagnostic>* diagnostics = nullptr);
    std::size_t PushDatagram(const std::vector<std::uint8_t>& bytes, double receiveTime,
                             std::vector<Diagnostic>* diagnostics = nullptr)
    {
        return PushDatagram(bytes.data(), bytes.size(), receiveTime, diagnostics);
    }
    std::size_t PushPacket(const MotionPacket& packet, double receiveTime,
                           std::vector<Diagnostic>* diagnostics = nullptr);

    // A notification only; no continuity or intake policy is applied here.
    bool ConsumeSessionRestart() noexcept;
    const openstrata::motion::SourceMetadata& GetSourceMetadata() const noexcept
    {
        return _assembler.GetSourceMetadata();
    }
    const MocopiFrameAssembler& GetAssembler() const noexcept { return _assembler; }

    const SkeletonMap* GetSkeletonMap() const noexcept { return _assembler.GetSkeletonMap(); }

    // Valid until the next push or reset. Refused datagrams clear it.
    const std::vector<MocopiFrame>& GetFramesFromLastPush() const noexcept { return _frames; }
    const MocopiFrameSourceStats& GetStats() const noexcept { return _stats; }
    void ResetStats() noexcept { _stats = MocopiFrameSourceStats(); }
    // Forget acquisition state; counters and diagnostic datagram serial survive.
    void Reset();

private:
    std::size_t _Deliver();
    void _StampDatagram(std::vector<Diagnostic>* diagnostics, std::size_t from) const;
    MocopiFrameAssembler _assembler;
    std::vector<MocopiFrame> _frames;
    std::uint64_t _datagramSerial = 0;
    bool _restartPending = false;
    MocopiFrameSourceStats _stats;
};

} // namespace openstrata::connectors::mocopi
