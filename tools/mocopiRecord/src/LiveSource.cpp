// SPDX-License-Identifier: Apache-2.0

#include "LiveSource.h"

#include <utility>

namespace openstrata::connectors::mocopi {

MocopiLiveSource::MocopiLiveSource(const MocopiLiveSourceConfig& config)
    : _source(config.frame), _intake(config.intake), _restart(config.restart)
{
    // Before a frame exists, so the first pose out of the buffer already says it
    // arrived over this protocol from a live capture. Told once and never
    // compared again: unlike the sibling's, this protocol carries no handshake
    // that could teach the session anything later (see the header).
    _intake.SetSourceMetadata(_source.GetSourceMetadata());
}

void
MocopiLiveSource::SetSource(std::string source)
{
    _source.SetSource(std::move(source));
}

std::size_t
MocopiLiveSource::_Deliver()
{
    std::size_t admitted = 0;
    for (const MocopiFrame& frame : _frames) {
        if (frame.beginsNewSession) {
            _restartPending = true;
            if (_restart == SessionRestartPolicy::Reset) {
                // Before the frame is pushed, so it is admitted as the first of
                // the new buffer rather than refused against the old one's head.
                _intake.Reset();
                ++_stats.sessionsReset;
            }
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
MocopiLiveSource::PushDatagram(const std::uint8_t* bytes, std::size_t size, double receiveTime,
                               std::vector<Diagnostic>* diagnostics)
{
    _source.PushDatagram(bytes, size, receiveTime, diagnostics);
    _stats.datagramsDecoded = _source.GetStats().datagramsDecoded;
    _stats.datagramsRefused = _source.GetStats().datagramsRefused;
    _stats.bonesRefused = _source.GetStats().bonesRefused;
    _frames = _source.GetFramesFromLastPush();
    return _Deliver();
}

std::size_t
MocopiLiveSource::PushPacket(const MotionPacket& packet, double receiveTime,
                             std::vector<Diagnostic>* diagnostics)
{
    _source.PushPacket(packet, receiveTime, diagnostics);
    _frames = _source.GetFramesFromLastPush();
    return _Deliver();
}

bool
MocopiLiveSource::ConsumeSessionRestart() noexcept
{
    const bool pending = _restartPending;
    _restartPending = false;
    return pending;
}

openstrata::motion::PoseSampleResult
MocopiLiveSource::Sample(double evaluationTime)
{
    return _intake.Sample(evaluationTime);
}

openstrata::motion::SourceMetadata
MocopiLiveSource::GetSourceMetadata() const
{
    // The intake's rather than the assembler's, which on this protocol are the
    // same value — reported through the intake anyway, because this must say
    // what is actually stamped on the poses a caller is sampling, and a
    // constant that is read from the source of truth stays correct if the source
    // of truth stops being constant.
    return _intake.GetSourceMetadata();
}

bool
MocopiLiveSource::GetTimeRange(double* startTime, double* endTime) const
{
    return _intake.GetTimeRange(startTime, endTime);
}

void
MocopiLiveSource::Reset()
{
    _source.Reset();
    _intake.Reset();
    _frames.clear();
    _restartPending = false;
    _intake.SetSourceMetadata(_source.GetSourceMetadata());
}

} // namespace openstrata::connectors::mocopi
