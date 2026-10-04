// SPDX-License-Identifier: Apache-2.0
//
// The WebSocket connector's diagnostic namespace, `WEBSOCKET_*`
// (docs/design/WEBSOCKET_CONNECTOR.md §8).
//
// The fourteen codes were frozen in that section before this directory
// existed, as each imported connector's set was frozen before its decoder. The
// set is appended to and never renumbered. Both directions raise from it: the
// receiving `WebSocketConnector` and the sending `WebSocketFrameSender`.
//
// A refusal by the frame codec is not one of these. It is the codec's
// `WIRE_*` code and passes through unchanged (DIAGNOSTICS.md §1), so this
// record carries an optional `wireCode` beside its own: a dropped message
// reads the same whichever connector or tool decoded it.
#pragma once

#include "motionConnectorWebSocket/api.h"

#include "motionConnectorTransport/Diagnostics.h"
#include "motionConnectorWire/FrameWire.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openstrata::connectors::websocket
{

// Values are stable array indices, in §8's table order. Append only before
// Count.
enum class DiagnosticCode : std::uint8_t
{
    // The listener cannot bind its address and port, or its socket failed.
    SocketBindFailed,
    // A connect role's peer refused or is unreachable. Once per episode.
    ConnectFailed,
    // An opening handshake failed: not an upgrade, a wrong path, no
    // subprotocol, a head over 8 KiB or slower than the handshake timeout.
    HandshakeRefused,
    // A request's `Origin` is not on the allow list (§4.2).
    OriginRefused,
    // A peer arrived when the receiving session or the sender's peer bound was
    // full, and was answered HTTP 503.
    PeerRefused,
    // An RFC 6455 framing violation or invalid UTF-8 (§5); the connection was
    // closed with 1002 or 1007.
    ProtocolViolation,
    // A message would exceed the bound; closed with 1009.
    MessageTooLarge,
    // A binary message; closed with 1003.
    UnsupportedMessage,
    // The peer closed or the connection reset.
    PeerDisconnected,
    // No message arrived within the silence timeout. Once per episode.
    SourceTimeout,
    // The sender's `frameNumber` went back, or a new connection began after
    // frames (§7).
    SourceRestarted,
    // The sender's `frameNumber` skipped (§7).
    FrameGap,
    // An actor's pose timestamp did not advance; the whole frame was dropped.
    TimestampRegression,
    // A frame's profile is not the configured one; the frame was dropped.
    ProfileMismatch,

    Count,
};

inline constexpr std::size_t DiagnosticCodeCount = static_cast<std::size_t>(DiagnosticCode::Count);

using DiagnosticSeverity = transport::DiagnosticSeverity;
using transport::DiagnosticSeverityString;

// The stable string, e.g. "WEBSOCKET_PROTOCOL_VIOLATION". This is the
// contract; the enumerator spelling is not.
MOTIONCONNECTORWEBSOCKET_API std::string_view DiagnosticCodeString(DiagnosticCode code) noexcept;

MOTIONCONNECTORWEBSOCKET_API std::optional<DiagnosticCode>
FindDiagnosticCode(std::string_view name) noexcept;

MOTIONCONNECTORWEBSOCKET_API DiagnosticSeverity
DiagnosticDefaultSeverity(DiagnosticCode code) noexcept;

MOTIONCONNECTORWEBSOCKET_API bool DiagnosticIsRecoverable(DiagnosticCode code) noexcept;

// One reported diagnostic: this connector's code in the shared vehicle, or the
// codec's code passing through.
//
// The default code is named rather than left to enumerator 0, which here is
// the one fatal code: a default-constructed diagnostic should not read as a
// listener that failed.
struct Diagnostic : transport::Diagnostic<DiagnosticCode, DiagnosticCode::ProtocolViolation>
{
    // Set when the frame codec refused a message (or the sender's encoder
    // refused a frame). `code` is then not read: the stable string, severity
    // and recoverability are the codec's.
    std::optional<wire::WireErrorCode> wireCode;
};

// Fills `severity` and `recoverable` from the code's row.
MOTIONCONNECTORWEBSOCKET_API Diagnostic MakeDiagnostic(DiagnosticCode code,
                                                       std::string detail = {});

// A codec refusal, passing through: warning and recoverable, as every `WIRE_*`
// code is, with the codec's subject.
MOTIONCONNECTORWEBSOCKET_API Diagnostic MakeWireDiagnostic(const wire::WireError& error);

// The stable string this diagnostic is reported under: a `WEBSOCKET_*` code,
// or the `WIRE_*` code it carries.
MOTIONCONNECTORWEBSOCKET_API std::string_view DiagnosticName(const Diagnostic& diagnostic) noexcept;

// The shared one-line form:
//
//     [WEBSOCKET_FRAME_GAP] info recoverable source=127.0.0.1:51000
//     subject=8..9 seq=10: the sender skipped 2 frame(s)
MOTIONCONNECTORWEBSOCKET_API std::string FormatDiagnostic(const Diagnostic& diagnostic);

} // namespace openstrata::connectors::websocket
