// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorVmc/FrameSource.h"
#include "motionConnectorVmc/OscPacket.h"

#include <utility>

namespace openstrata::connectors::vmc {

VmcFrameSource::VmcFrameSource(const VmcFrameConfig& config) : _assembler(config) {}

void
VmcFrameSource::SetSource(std::string source)
{
    _assembler.SetSource(std::move(source));
}

std::size_t
VmcFrameSource::_Deliver()
{
    for (const VmcFrame& frame : _frames) {
        _restartPending = _restartPending || frame.beginsNewSession;
    }
    _stats.framesDelivered += _frames.size();
    return _frames.size();
}

void
VmcFrameSource::_StampDatagram(std::vector<Diagnostic>* diagnostics, std::size_t from) const
{
    if (!diagnostics) {
        return;
    }
    for (std::size_t index = from; index != diagnostics->size(); ++index) {
        (*diagnostics)[index].source = _assembler.GetSource();
        (*diagnostics)[index].sequence = _datagramSerial;
    }
}

std::size_t
VmcFrameSource::PushDatagram(const std::uint8_t* bytes, std::size_t size, double receiveTime,
                             std::vector<Diagnostic>* diagnostics)
{
    ++_datagramSerial;
    const std::size_t before = diagnostics ? diagnostics->size() : 0;
    OscPacket osc;
    Diagnostic refusal;
    if (!DecodeOscPacket(bytes, size, &osc, &refusal)) {
        ++_stats.datagramsRefused;
        _frames.clear();
        if (diagnostics) {
            diagnostics->push_back(std::move(refusal));
            _StampDatagram(diagnostics, before);
        }
        return 0;
    }
    ++_stats.datagramsDecoded;
    VmcPacket packet;
    DecodeVmcPacket(osc, &packet, diagnostics);
    const std::size_t emitted = PushPacket(packet, receiveTime, diagnostics);
    _StampDatagram(diagnostics, before);
    return emitted;
}

std::size_t
VmcFrameSource::PushPacket(const VmcPacket& packet, double receiveTime,
                           std::vector<Diagnostic>* diagnostics)
{
    _frames.clear();
    _assembler.Push(packet, receiveTime, &_frames, diagnostics);
    return _Deliver();
}

std::size_t
VmcFrameSource::Flush(std::vector<Diagnostic>* diagnostics)
{
    _frames.clear();
    _assembler.Flush(&_frames, diagnostics);
    return _Deliver();
}

bool
VmcFrameSource::ConsumeSessionRestart() noexcept
{
    const bool pending = _restartPending;
    _restartPending = false;
    return pending;
}

void
VmcFrameSource::Reset()
{
    _assembler.Reset();
    _frames.clear();
    _restartPending = false;
}

} // namespace openstrata::connectors::vmc
