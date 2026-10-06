// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorMocopi/FrameSource.h"

#include <utility>

namespace openstrata::connectors::mocopi {

MocopiFrameSource::MocopiFrameSource(const MocopiFrameConfig& config) : _assembler(config) {}

void
MocopiFrameSource::SetSource(std::string source)
{
    _assembler.SetSource(std::move(source));
}

std::size_t
MocopiFrameSource::_Deliver()
{
    for (const MocopiFrame& frame : _frames) {
        _restartPending = _restartPending || frame.beginsNewSession;
    }
    _stats.framesDelivered += _frames.size();
    return _frames.size();
}

void
MocopiFrameSource::_StampDatagram(std::vector<Diagnostic>* diagnostics, std::size_t from) const
{
    if (!diagnostics) {
        return;
    }
    for (std::size_t index = from; index != diagnostics->size(); ++index) {
        (*diagnostics)[index].source = _assembler.GetSource();
        // Overwrites the assembler's own serial on the lines it raised, which is
        // the point: it counts packets it was handed and this counts deliveries,
        // and only one of the two can name the datagram a reader would go
        // looking for in a capture.
        (*diagnostics)[index].sequence = _datagramSerial;
    }
}

std::size_t
MocopiFrameSource::PushDatagram(const std::uint8_t* bytes, std::size_t size, double receiveTime,
                                std::vector<Diagnostic>* diagnostics)
{
    ++_datagramSerial;
    const std::size_t before = diagnostics ? diagnostics->size() : 0;

    MotionPacket packet;
    if (!DecodeMotionPacket(bytes, size, &packet, diagnostics)) {
        ++_stats.datagramsRefused;
        // The frames of the previous push must not survive a datagram this one
        // refused: `GetFramesFromLastPush()` is a window on the delivery that
        // just happened, and a caller reading it after a refusal would be shown
        // the frame before last as though it had just arrived.
        _frames.clear();
        _StampDatagram(diagnostics, before);
        return 0;
    }
    ++_stats.datagramsDecoded;
    // Read off the packet before it goes out of scope at the end of this call.
    // It is the only place the tally is reachable from: a caller that passed no
    // diagnostics has no other way to learn that records were dropped.
    _stats.bonesRefused += packet.refusedBones;

    const std::size_t emitted = PushPacket(packet, receiveTime, diagnostics);
    // After the assembler rather than before it, so one datagram's diagnostics
    // are numbered alike however many layers raised them.
    _StampDatagram(diagnostics, before);
    return emitted;
}

std::size_t
MocopiFrameSource::PushPacket(const MotionPacket& packet, double receiveTime,
                              std::vector<Diagnostic>* diagnostics)
{
    _frames.clear();
    _assembler.Push(packet, receiveTime, &_frames, diagnostics);
    return _Deliver();
}

bool
MocopiFrameSource::ConsumeSessionRestart() noexcept
{
    const bool pending = _restartPending;
    _restartPending = false;
    return pending;
}

void
MocopiFrameSource::Reset()
{
    _assembler.Reset();
    _frames.clear();
    _restartPending = false;
}

} // namespace openstrata::connectors::mocopi
