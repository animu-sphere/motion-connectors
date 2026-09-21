// SPDX-License-Identifier: Apache-2.0
//
// The whole of this adapter's receiver: the map from a transport event to a
// `VRCHAT_OSC_*` code.
//
// There is no socket in this file, and there never was one -- which is the
// difference between this adapter and its two siblings. Each of those grew a
// receiver of its own and then gave it up; this one was written after the
// extraction, so the third copy of `UdpReceiver.cpp` is these sixty lines
// instead of the five hundred and fifty the census measured
// (osc-and-vrchat-trackers.md §2).
//
// What could not be shared is the naming. `motionConnectorTransport` reports what it
// observed; a code is frozen per adapter, before its decoder exists, so the
// layer that knows which adapter it is has to be the one that names it
// (WORKSPACE.md §2's diagnostic split).

#include "motionConnectorVrchatOsc/UdpReceiver.h"

#include <utility>

namespace openstrata::connectors::vrchatOsc
{

namespace
{

// One event, one code.
//
// The silence detail carries which of the two silences it was -- a source that
// has not started, and one that has stopped -- because the frozen code covers
// both by design ("the silence of a sender that has not been started and of one
// that has stopped is the same silence" -- Diagnostics.h) and the sentence is
// what tells an operator which they are looking at.
//
// No `timestamp` on either, and the shared receiver supplies none. That field is
// seconds in the *source's* own clock, and neither of these is tied to anything
// the source sent: nothing arrived to carry a clock reading. Putting the
// receiver's clock there would give a session report two unrelated timelines in
// one column.
Diagnostic
Translate(const transport::TransportEventReport& report)
{
    switch (report.event)
    {
    case transport::TransportEvent::Silence:
    {
        Diagnostic diagnostic = MakeDiagnostic(DiagnosticCode::SourceTimeout, report.detail);
        diagnostic.source = report.source;
        diagnostic.subject = report.subject;
        return diagnostic;
    }
    case transport::TransportEvent::BindFailed:
        break;
    }
    Diagnostic diagnostic = MakeDiagnostic(DiagnosticCode::SocketBindFailed, report.detail);
    diagnostic.source = report.source;
    diagnostic.subject = report.subject;
    return diagnostic;
}

void
Append(const std::vector<transport::TransportEventReport>& events,
       std::vector<Diagnostic>* diagnostics)
{
    if (!diagnostics)
    {
        return;
    }
    for (const transport::TransportEventReport& report : events)
    {
        diagnostics->push_back(Translate(report));
    }
}

} // namespace

bool
UdpReceiver::Open(const UdpReceiverConfig& config, std::vector<Diagnostic>* diagnostics)
{
    transport::UdpReceiverConfig transport;
    transport.listenAddress = config.listenAddress;
    transport.listenPort = config.listenPort;
    transport.reuseAddress = config.reuseAddress;
    transport.receiveBufferBytes = config.receiveBufferBytes;
    transport.silenceTimeoutSeconds = config.silenceTimeoutSeconds;

    std::vector<transport::TransportEventReport> events;
    const bool opened = _receiver.Open(transport, &events);
    Append(events, diagnostics);
    return opened;
}

ReceiveStatus
UdpReceiver::Receive(ReceivedDatagram* datagram, double timeoutSeconds,
                     std::vector<Diagnostic>* diagnostics)
{
    // The sink is passed through only when the caller wants one. The shared
    // receiver counts a silence episode either way -- the tally is what a
    // session report reads, and it must not depend on whether the loop that
    // noticed had somewhere to put a message -- so a null `diagnostics` here
    // still moves `silenceReports`.
    if (!diagnostics)
    {
        return _receiver.Receive(datagram, timeoutSeconds);
    }

    std::vector<transport::TransportEventReport> events;
    const ReceiveStatus status = _receiver.Receive(datagram, timeoutSeconds, &events);
    Append(events, diagnostics);
    return status;
}

} // namespace openstrata::connectors::vrchatOsc
