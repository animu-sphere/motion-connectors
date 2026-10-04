// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorWebSocket/Diagnostics.h"

#include <array>
#include <utility>

namespace openstrata::connectors::websocket
{

namespace
{

using transport::DiagnosticCodeEntry;

// §8's table, in its order. Severity and recoverability live here and nowhere
// else, so two raise sites cannot report one code two ways.
//
// One code is fatal: a listener that cannot bind has nothing to recover into.
// A peer going away is never an error (CONNECTOR_CONTRACT §5), so every code a
// session can raise is recoverable.
constexpr std::array<DiagnosticCodeEntry, DiagnosticCodeCount> kCodes{{
    {"WEBSOCKET_SOCKET_BIND_FAILED", DiagnosticSeverity::Error, false},
    {"WEBSOCKET_CONNECT_FAILED", DiagnosticSeverity::Warning, true},
    {"WEBSOCKET_HANDSHAKE_REFUSED", DiagnosticSeverity::Warning, true},
    {"WEBSOCKET_ORIGIN_REFUSED", DiagnosticSeverity::Warning, true},
    {"WEBSOCKET_PEER_REFUSED", DiagnosticSeverity::Info, true},
    {"WEBSOCKET_PROTOCOL_VIOLATION", DiagnosticSeverity::Warning, true},
    {"WEBSOCKET_MESSAGE_TOO_LARGE", DiagnosticSeverity::Warning, true},
    {"WEBSOCKET_UNSUPPORTED_MESSAGE", DiagnosticSeverity::Warning, true},
    {"WEBSOCKET_PEER_DISCONNECTED", DiagnosticSeverity::Info, true},
    {"WEBSOCKET_SOURCE_TIMEOUT", DiagnosticSeverity::Warning, true},
    {"WEBSOCKET_SOURCE_RESTARTED", DiagnosticSeverity::Info, true},
    {"WEBSOCKET_FRAME_GAP", DiagnosticSeverity::Info, true},
    {"WEBSOCKET_TIMESTAMP_REGRESSION", DiagnosticSeverity::Warning, true},
    {"WEBSOCKET_PROFILE_MISMATCH", DiagnosticSeverity::Warning, true},
}};

constexpr transport::DiagnosticCodeTable<DiagnosticCode> kTable{kCodes.data(), kCodes.size()};

} // namespace

std::string_view
DiagnosticCodeString(DiagnosticCode code) noexcept
{
    return kTable.Name(code);
}

std::optional<DiagnosticCode>
FindDiagnosticCode(std::string_view name) noexcept
{
    return kTable.Find(name);
}

DiagnosticSeverity
DiagnosticDefaultSeverity(DiagnosticCode code) noexcept
{
    return kTable.Severity(code);
}

bool
DiagnosticIsRecoverable(DiagnosticCode code) noexcept
{
    return kTable.Recoverable(code);
}

Diagnostic
MakeDiagnostic(DiagnosticCode code, std::string detail)
{
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = DiagnosticDefaultSeverity(code);
    diagnostic.recoverable = DiagnosticIsRecoverable(code);
    diagnostic.detail = std::move(detail);
    return diagnostic;
}

Diagnostic
MakeWireDiagnostic(const wire::WireError& error)
{
    // Every `WIRE_*` code is a warning and recoverable (FrameWire.h): a refused
    // message is a dropped message and the session continues.
    Diagnostic diagnostic;
    diagnostic.wireCode = error.code;
    diagnostic.severity = DiagnosticSeverity::Warning;
    diagnostic.recoverable = true;
    diagnostic.subject = error.subject;
    diagnostic.detail = error.detail;
    return diagnostic;
}

std::string_view
DiagnosticName(const Diagnostic& diagnostic) noexcept
{
    return diagnostic.wireCode ? wire::WireErrorCodeName(*diagnostic.wireCode)
                               : DiagnosticCodeString(diagnostic.code);
}

std::string
FormatDiagnostic(const Diagnostic& diagnostic)
{
    return transport::FormatDiagnostic(DiagnosticName(diagnostic), diagnostic);
}

} // namespace openstrata::connectors::websocket
