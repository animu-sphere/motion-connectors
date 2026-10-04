// SPDX-License-Identifier: Apache-2.0
//
// A recorded WebSocket session: each message the connector received, after
// unmasking and reassembly and before the codec read it, one message per
// record (docs/design/WEBSOCKET_CONNECTOR.md §9).
//
// The format, its reader and its writer are `motionConnectorTransport`'s. What
// is this connector's is the magic line. `p` lines name the peer, so a new
// connection shows as the change of peer it is. The handshake, control frames
// and framing bytes are not recorded: they are the session, not the source's
// data.
#pragma once

#include "motionConnectorWebSocket/api.h"

#include "motionConnectorTransport/PacketCapture.h"

#include <iosfwd>
#include <string>
#include <string_view>

namespace openstrata::connectors::websocket
{

inline constexpr std::string_view PacketCaptureMagic = "!websocket-packet-capture";

using transport::PacketCapture;
using transport::PacketCaptureError;
using transport::RecordedDatagram;

inline bool
ReadPacketCapture(std::istream& input, PacketCapture* capture, PacketCaptureError* error = nullptr)
{
    return transport::ReadPacketCapture(PacketCaptureMagic, input, capture, error);
}

inline bool
ReadPacketCaptureFile(const std::string& path, PacketCapture* capture,
                      PacketCaptureError* error = nullptr)
{
    return transport::ReadPacketCaptureFile(PacketCaptureMagic, path, capture, error);
}

inline bool
WritePacketCapture(std::ostream& output, const PacketCapture& capture)
{
    return transport::WritePacketCapture(PacketCaptureMagic, output, capture);
}

inline bool
WritePacketCaptureFile(const std::string& path, const PacketCapture& capture)
{
    return transport::WritePacketCaptureFile(PacketCaptureMagic, path, capture);
}

} // namespace openstrata::connectors::websocket
