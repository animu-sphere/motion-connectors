# motionConnectorWebSocket

`motionConnectorWebSocket` carries `MotionFrame`s over WebSocket, as
`openstrata.motion.frame/v1` text messages, in both directions
([WEBSOCKET_CONNECTOR.md](../../docs/design/WEBSOCKET_CONNECTOR.md)):

- `WebSocketConnector` receives them as an `IMotionConnector`;
- `WebSocketFrameSender` sends them, for `motion_connect bridge`. Its
  peer statistics include queued messages and pending framing bytes so a
  caller can finish partially written frames before closing.

Either one **listens** or **connects**. After the handshake a connection is
symmetric, so the role decides only who opens it.

```cpp
#include "motionConnectorWebSocket/Connector.h"

namespace core = openstrata::connectors::core;
namespace ws = openstrata::connectors::websocket;

ws::WebSocketConnector connector;
core::ConnectorConfig config;
config.port = 8765;                   // loopback by default
connector.Open(config);               // listens; ws::WebSocketConfig states the rest

core::MotionFrame frame;
while (running)
{
    while (connector.Poll(frame)) { /* frame.frameNumber is this connector's */ }
}
```

A browser connects with `new WebSocket("ws://127.0.0.1:8765/",
["openstrata.motion.frame.v1"])`, from an origin listed in
`WebSocketConfig::allowedOrigins`.

Current capability status, test evidence and release availability are in the
[capability matrix](../../docs/reference/CAPABILITY_MATRIX.md).

## What it guarantees

- **Caller-driven.** `Poll`, `Send` and `Service` never block, and nothing
  here starts a thread. Each call reads at most a few maximum messages, so a
  flood cannot hold the caller's tick.
- **One message is one frame.** The connector rewrites `frameNumber`, from 1
  after each `Open` and monotone across connections, and
  `timing.receiveTimestamp`, from its own clock. It carries everything else
  verbatim, and it converts nothing: `Open` refuses a `coordinateConversion`.
- **The sender's numbering is watched, not trusted.** A `frameNumber` that
  does not advance, or the first frame of a new connection after frames, is
  `WEBSOCKET_SOURCE_RESTARTED`. A skip is `WEBSOCKET_FRAME_GAP` with the
  missing range. Every frame that decodes counts, including one that is then
  dropped. A message the codec refused was never read, so the frame after it
  can report a gap. A pose timestamp that does not advance drops the whole
  frame.
- **A codec refusal costs one message, and a framing error ends the
  connection.** The first passes its `WIRE_*` code through and the session
  continues. The second closes with 1002, 1003, 1007 or 1009, because nothing
  after a bad header can be trusted to start a frame.
- **Loopback by default, and `Origin` is checked anyway.** A web page can
  reach `127.0.0.1`. A request carrying `Origin` is refused with 403 unless the
  origin is allow-listed, and the allow list is empty by default.

## RFC 6455 is here

There is no third-party library
([§2](../../docs/design/WEBSOCKET_CONNECTOR.md#2-the-decision)). Each library
considered brings its own threads or event loop, and each refusal in §5 has to
be seen in a frame's header before its payload is read. The subset is small:
an HTTP/1.1 upgrade, SHA-1 and base64 for the accept key, frame headers,
masking, fragmentation, ping/pong and close. There are no extensions, no
compression and no binary messages. TLS is open as `WSC-O1`.

The codec (the first two files below), the TCP layer and the endpoint above
them are private, and none of them is installed (§10):

```text
src/Rfc6455.*   SHA-1, base64, UTF-8, the handshake, frame writing and reading
src/Session.*   one connection's protocol state, from bytes to events; no socket
src/Tcp.*       a non-blocking listener and stream on the OS's sockets
src/Endpoint.*  roles, peers, refusals, reconnects and per-peer queues
```

## Raw capture

`WebSocketConnector::StartCapture` records each message received live, after
unmasking and reassembly and before the codec reads it. The format is the
transport's packet capture, under the magic `!websocket-packet-capture`, and
`p` lines name each peer. `PushMessage(text, receiveTimestamp, peer)` replays
a message through the same assembly as a live one, and a change of peer is a
new connection. A reconnection to the same peer endpoint does not show in a
capture, because the format names peers and not connections.

## Tests

| Name | What |
| --- | --- |
| `motionConnectorWebSocket_rfc6455` | SHA-1, base64, the accept key, UTF-8, length encoding, the 65 507-byte bound, two sessions in memory |
| `motionConnectorWebSocket_framingCorpus` | `tests/corpus/framing/`: 56 byte streams, one per row of §5 and per refusal of §4, each with its events, close status and byte |
| `motionConnectorWebSocket_connector` | the code table in §8's order, `Open`'s refusals, what §7 rewrites and carries |
| `motionConnectorWebSocket_messageCorpus` | `tests/corpus/messages/`: 8 message captures for §7's restart, gap, regression, profile and codec rows |
| `motionConnectorWebSocket_loopback` | both directions in both roles over loopback; `Origin`, path and peer bounds; retries, reconnects and silence. **Binds sockets** |
| `motionConnectorWebSocket_corpusGen` | the committed corpus is what `tools/generate_corpus.py` writes |
| `motionConnectorWebSocket_boundaries` | the edges, no third-party library, no producer name |

The corpus is regenerated with
`python libs/motionConnectorWebSocket/tools/generate_corpus.py`.
