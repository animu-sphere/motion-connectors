// SPDX-License-Identifier: Apache-2.0
//
// What an endpoint states beyond `ConnectorConfig`: its role, its address,
// the path, the origins it lets in, its bounds and its timeouts
// (docs/design/WEBSOCKET_CONNECTOR.md §3–§6). One struct for both directions;
// the few fields only one direction reads say so.
#pragma once

#include "motionConnectorWebSocket/api.h"

#include "motionConnectorTransport/PacketCapture.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openstrata::connectors::websocket
{

// The subprotocol a client offers and a server selects (§4.1): the format
// identifier with `/` spelled `.`, because `/` is a separator in an HTTP
// token. A browser opens a session with
// `new WebSocket(url, ["openstrata.motion.frame.v1"])`.
inline constexpr std::string_view Subprotocol = "openstrata.motion.frame.v1";

// The largest message either direction accepts (§5): the transport's
// `MaxDatagramBytes`, which is also the packet-capture record bound, so every
// accepted message can be recorded (§9). Configurable lower, never higher.
inline constexpr std::size_t MaxMessageBytes = transport::MaxDatagramBytes;

// The bound on an opening handshake's head (§4.3).
inline constexpr std::size_t MaxHandshakeHeadBytes = 8192;

// Who opens the connection (§3). After the handshake a connection is
// symmetric; the role decides nothing else.
enum class WebSocketRole : std::uint8_t
{
    // An HTTP server that accepts the upgrade.
    Listen,
    // A client that sends it, and reconnects when it is refused or lost.
    Connect,
};

struct WebSocketConfig
{
    WebSocketRole role = WebSocketRole::Listen;

    // Listen: the interface to bind, loopback by default
    // (CONNECTOR_CONTRACT §10); another interface is an explicit choice.
    // Connect: the peer's address. Either way a numeric address and never a
    // hostname: an unresolvable string fails at `Open` without touching the
    // network.
    std::string address = "127.0.0.1";
    // Always stated. Port 0 lets the OS choose a listening port, for tests; a
    // connect role refuses it.
    std::uint16_t port = 0;

    // The request target (§4.3). A listener answers any other with HTTP 404;
    // a connect role requests this one.
    std::string path = "/";

    // The origins a listener lets in (§4.2). A request carrying `Origin` is
    // refused with HTTP 403 unless its value is listed here, compared exactly.
    // Empty by default: only a native peer, which sends no `Origin`, gets in.
    std::vector<std::string> allowedOrigins;

    // §5's bound, at most `MaxMessageBytes`.
    std::size_t maxMessageBytes = MaxMessageBytes;
    // How many maximum messages' worth of bytes one `Poll` or `Service` reads
    // from one peer, so a flood cannot hold the caller's tick. What it does not
    // read stays in the socket for the next call.
    std::size_t readBudgetMessages = 4;

    // A handshake's head must complete within this many seconds of the TCP
    // accept (listen) or of the connection completing (connect), and a
    // connect attempt must complete within it too.
    double handshakeTimeoutSeconds = 5.0;

    // The connector only: seconds without a message after which the link is
    // `Degraded` and `WEBSOCKET_SOURCE_TIMEOUT` is raised, once per episode.
    // 0 is off, the default: how long a sender may stay quiet is the
    // session's property, not the socket's (§6).
    double silenceTimeoutSeconds = 0.0;

    // The connect role's retry: the first wait after a refused or lost
    // connection, doubling up to the second (§3).
    double reconnectInitialSeconds = 0.5;
    double reconnectMaxSeconds = 5.0;

    // The sender only, listening: how many peers it serves at once (§3). The
    // receiving connector holds one peer at a time whatever this says.
    std::size_t maxPeers = 8;
    // The sender only: each peer's bound on messages that have not started.
    // An overflow drops the oldest of them and counts it for that peer.
    std::size_t peerQueueMessages = 64;
};

} // namespace openstrata::connectors::websocket
