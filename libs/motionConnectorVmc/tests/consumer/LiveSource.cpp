// SPDX-License-Identifier: Apache-2.0

#include "LiveSource.h"

#include <utility>

namespace openstrata::connectors::vmc {

VmcLiveSource::VmcLiveSource(const VmcLiveSourceConfig& config)
    : _source(config.frame), _intake(config.intake), _restart(config.restart)
{
    // Before a frame exists, so the first pose out of the buffer already says it
    // arrived over VMC from a live capture. The model's title joins it when the
    // sender sends one.
    _metadata = _source.GetSourceMetadata();
    _intake.SetSourceMetadata(_metadata);
}

void
VmcLiveSource::SetSource(std::string source)
{
    _source.SetSource(std::move(source));
}

std::size_t
VmcLiveSource::_Deliver()
{
    std::size_t admitted = 0;
    for (const VmcFrame& frame : _frames) {
        if (frame.beginsNewSession) {
            _restartPending = true;
            if (_restart == SessionRestartPolicy::Reset) {
                // Before the frame is pushed, so it is admitted as the first of
                // the new buffer rather than refused against the old one's head.
                _intake.Reset();
                ++_stats.sessionsReset;
            }
        }

        // The handshake can arrive at any point in a session, including between
        // two frames. Comparing rather than assigning keeps a 30 Hz stream from
        // re-stamping the intake sixty times a second to say the same thing.
        const openstrata::motion::SourceMetadata& metadata = _source.GetSourceMetadata();
        if (metadata != _metadata) {
            _metadata = metadata;
            _intake.SetSourceMetadata(_metadata);
        }

        ++_stats.framesDelivered;
        if (_intake.Push(frame.pose)) {
            ++_stats.framesAdmitted;
            ++admitted;
        } else {
            ++_stats.framesRefused;
        }
    }
    return admitted;
}

std::size_t
VmcLiveSource::PushDatagram(const std::uint8_t* bytes, std::size_t size, double receiveTime,
                            std::vector<Diagnostic>* diagnostics)
{
    _source.PushDatagram(bytes, size, receiveTime, diagnostics);
    _stats.datagramsDecoded = _source.GetStats().datagramsDecoded;
    _stats.datagramsRefused = _source.GetStats().datagramsRefused;
    _frames = _source.GetFramesFromLastPush();
    return _Deliver();
}

std::size_t
VmcLiveSource::PushPacket(const VmcPacket& packet, double receiveTime,
                          std::vector<Diagnostic>* diagnostics)
{
    _source.PushPacket(packet, receiveTime, diagnostics);
    _frames = _source.GetFramesFromLastPush();
    return _Deliver();
}

std::size_t
VmcLiveSource::Flush(std::vector<Diagnostic>* diagnostics)
{
    _source.Flush(diagnostics);
    _frames = _source.GetFramesFromLastPush();
    return _Deliver();
}

bool
VmcLiveSource::ConsumeSessionRestart() noexcept
{
    const bool pending = _restartPending;
    _restartPending = false;
    return pending;
}

openstrata::motion::PoseSampleResult
VmcLiveSource::Sample(double evaluationTime)
{
    return _intake.Sample(evaluationTime);
}

openstrata::motion::SourceMetadata
VmcLiveSource::GetSourceMetadata() const
{
    // The intake's rather than the assembler's: this reports the provenance
    // actually stamped on the poses a caller is sampling, which lags the
    // assembler's by nothing except a handshake that arrived after the last
    // frame was delivered.
    return _intake.GetSourceMetadata();
}

bool
VmcLiveSource::GetTimeRange(double* startTime, double* endTime) const
{
    return _intake.GetTimeRange(startTime, endTime);
}

void
VmcLiveSource::Reset()
{
    _source.Reset();
    _intake.Reset();
    _frames.clear();
    _restartPending = false;
    _metadata = _source.GetSourceMetadata();
    _intake.SetSourceMetadata(_metadata);
}

} // namespace openstrata::connectors::vmc
