# WebSocket connector

> Status: **accepted**, 2026-10-04, and **binding** since 2026-10-04, when
> `motionConnectorWebSocket` landed with the tests each section names: §3–§6
> by `motionConnectorWebSocket_loopback` and `_framingCorpus`, §7 by
> `_connector` and `_messageCorpus`, §8 by `_connector`'s code table, and §9
> by both corpora and the loopback suite's capture. The capability matrix says
> what is implemented.
>
> This document owns how `openstrata.motion.frame/v1` messages travel over
> WebSocket: who listens, the opening handshake, the subset of RFC 6455 this
> repository speaks, the session and its state, how a relayed frame is
> assembled, the connector's diagnostics and its raw capture. A browser or any
> other peer that talks to this connector follows §3–§5. How a message is
> spelled is [FRAME_WIRE_FORMAT.md](FRAME_WIRE_FORMAT.md)'s, and what a frame
> means is [CONNECTOR_CONTRACT.md](CONNECTOR_CONTRACT.md)'s; neither is
> restated here. Section numbers are stable; open questions are `WSC-O<n>` and
> never reused.

---

## 1. Scope

- **In:** carrying frame messages over WebSocket in both directions; the
  listen and connect roles; the handshake, its subprotocol and its `Origin`
  rule; the RFC 6455 subset; session state; frame assembly for a relayed frame;
  the `WEBSOCKET_*` diagnostics; raw capture of received messages.
- **Out:** the message itself (FRAME_WIRE_FORMAT); a binary encoding
  (`FW-O1`); connector state and diagnostics as messages, which version 1
  does not carry (`FW-O2`,
  [MOTION_CONNECT §4](MOTION_CONNECT.md#4-state-and-diagnostics-stay-at-the-bridge));
  `motion_connect bridge`'s own options
  ([MOTION_CONNECT §3](MOTION_CONNECT.md#3-bridge)); TLS (`WSC-O1`).

## 2. The decision

**RFC 6455 is implemented here**, inside `motionConnectorWebSocket`, with no
third-party library. The choice was made on 2026-10-04 between that, vendoring
IXWebSocket, and an Asio-based library (Boost.Beast, websocketpp):

- **The subset needed is small.** It is an HTTP/1.1 upgrade, SHA-1 and base64
  for the accept key, frame headers, masking, fragmentation, ping/pong and
  close. There are no extensions, no compression and no binary messages
  (§4.3, §5).
- **Every connector here is caller-driven.** `Poll` never blocks and the
  transport starts no thread (CONNECTOR_CONTRACT §2,
  `motionConnectorTransport`'s `UdpReceiver`). Each library considered brings
  its own threads or event loop. A connector built on one would make it the
  first connector with a second thread, which is the one thing `Poll` exists
  to avoid.
- **The refusals are properties of the parse.** A non-minimal length, an
  unmasked client frame and a declared length over the bound have to be seen
  in the frame header before a payload is read. `motionConnectorWire` owns its
  JSON parser for the same reason
  ([its README](../../libs/motionConnectorWire/README.md#the-json-layer-is-here)).
- **Nothing third-party is bundled yet**
  ([THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)), and `ost`
  distributes only sibling packages. A vendored library would be the first
  notice. It would also be the first build of someone else's code on three
  lanes.

The cost is a security-relevant parser that this repository now owns. A
generated corpus tests every refusal in §5, as the codec's corpus does.
TLS is where a library would earn its cost (OpenSSL or the platform's), so it
stays open as `WSC-O1`. The default bind is loopback, and loopback carries no
TLS.

## 3. Roles and directions

Two choices are independent:

- **Role.** An endpoint either **listens** (an HTTP server that accepts the
  upgrade) or **connects** (a client that sends it).
- **Direction.** An endpoint either **receives** frames, through the
  `IMotionConnector` `WebSocketConnector`, or **sends** them, through
  `WebSocketFrameSender`, which `motion_connect bridge` uses.

| Role | Receives | Sends |
| --- | --- | --- |
| listen | a browser module (MediaPipe, WebXR) or a remote bridge pushes frames to a native runtime | `bridge` serves a browser or a native consumer |
| connect | a native runtime reads from a remote `bridge` | `bridge` pushes to a remote runtime that listens |

After the handshake a connection is symmetric. Role decides only who opens it.
A test therefore runs both directions over loopback in one process: a sender
that listens and a connector that connects, and the reverse.

- **The default bind is loopback** (`127.0.0.1`), as CONNECTOR_CONTRACT §10
  requires. Another interface is an explicit configuration value. The port is
  always stated, and port 0 lets the OS choose it, for tests.
- **A connect role takes a numeric address, never a hostname**, as the
  transport resolves its listen address. An unresolvable string fails at
  `Open` without touching the network.
- **A receiving endpoint holds one peer at a time.** A second peer that
  arrives while one is in session is refused before the upgrade, with HTTP
  503 and `WEBSOCKET_PEER_REFUSED`. Merging two senders into one frame stream
  is a multi-source question (`WSC-O2`).
- **A sending listener serves a bounded number of peers**, 8 by default. Each
  peer has its own bounded queue of outbound messages. A message is encoded
  once and the same text goes to each peer. A queue that overflows drops its
  oldest message that has not started, never a message part-written, and the
  drop is counted for that peer, as `DatagramQueue` counts its own.
- **A connect role reconnects.** A refused or lost connection is retried
  inside `Poll` / `Service`, at 0.5 s and doubling to 5 s. The failure is
  reported once per episode, as the transport reports silence.

`IMotionConnector::Open(ConnectorConfig)` opens the receiving listen role on
`bindAddress:port` with every other value at its default. The connector's own
`Open` takes a `WebSocketConfig` that states the rest: role, peer address,
path, allowed origins, bounds and timeouts. A frame on the wire is already in
the canonical basis, so the connector converts nothing, and `Open` refuses a
non-empty `coordinateConversion`.

## 4. The opening handshake

RFC 6455 §4, over HTTP/1.1, with these rules on top:

### 4.1 The subprotocol is required

The client offers `Sec-WebSocket-Protocol: openstrata.motion.frame.v1` and the
server selects it. A listen role refuses a request that does not offer it,
with HTTP 400. A connect role fails a response that does not select it. The
token is the format identifier with `/` spelled `.`. A subprotocol is an HTTP
token, and `/` is a separator there. A browser opens a session with
`new WebSocket(url, ["openstrata.motion.frame.v1"])`. A binary encoding
(`FW-O1`) is negotiated as a second subprotocol beside this one, so a peer
that offers both gets the one the server prefers.

### 4.2 `Origin` is checked

A request that carries `Origin` is refused with HTTP 403 and
`WEBSOCKET_ORIGIN_REFUSED`, unless that origin is on the configured allow
list, which is empty by default. A request without `Origin` is a native peer
and is accepted.

**Loopback does not stop a web page.** Any page the user opens can call
`new WebSocket("ws://127.0.0.1:…")`. Without this rule, an advertisement
could read a performer's motion from `bridge`, or inject frames into a
runtime's receiving connector. A browser always sends `Origin` and a page
cannot forge it, so an explicit allow list closes that hole and costs a native
peer nothing.

### 4.3 Nothing else is negotiated

- `Sec-WebSocket-Extensions` offers are ignored and never echoed, so
  `permessage-deflate` is never on. That needs no zlib, and the size bound in
  §5 is a bound on bytes on the wire rather than on bytes after inflation.
- The request target must equal the configured path, `/` by default.
  Otherwise the listener answers HTTP 404.
- The request or response head is bounded at 8 KiB and must complete within
  5 s of the TCP accept. A peer that sends more, or sends it slower, is
  dropped with `WEBSOCKET_HANDSHAKE_REFUSED`.

## 5. The RFC 6455 subset

**One text message carries one frame message.** The framing rules:

| Received | Action |
| --- | --- |
| a text message, possibly fragmented, with control frames interleaved | reassembled and handed to the codec |
| a binary message | close 1003, `WEBSOCKET_UNSUPPORTED_MESSAGE`; version 1 negotiates no binary encoding |
| a declared length that would take the message over the bound | close 1009, `WEBSOCKET_MESSAGE_TOO_LARGE`, checked from the frame header before the payload is read |
| a client frame without a mask, or a server frame with one | close 1002, `WEBSOCKET_PROTOCOL_VIOLATION` |
| a reserved bit set, an unknown opcode, a continuation with nothing to continue, a new message before the last one finished, a control frame over 125 bytes or fragmented, a length that is not minimally encoded or has its top bit set | close 1002, `WEBSOCKET_PROTOCOL_VIOLATION` |
| text that is not UTF-8 | close 1007, `WEBSOCKET_PROTOCOL_VIOLATION` |
| a ping | a pong with the same payload |
| an unsolicited pong | ignored |
| a close | the close echoed, then the session ends (§6) |

- **The maximum message size is 65 507 bytes**, `motionConnectorTransport`'s
  `MaxDatagramBytes`. It is configurable lower, never higher. A 55-joint pose
  with 52 face channels is about 6 KB, so the bound holds about ten actors.
  It is also the bound under which every accepted message can be recorded
  (§9). The codec's own 1 MiB bound stays the codec's, for callers that read
  messages from elsewhere.
- **A framing error ends the connection, not just the message.** TCP is a
  byte stream, and nothing after a bad header can be trusted to start a frame.
  A refusal by the codec (`WIRE_*`) is different: the frame text was well
  delimited, so that message is dropped and the session continues.
- **Each `Poll` reads a bounded number of bytes**, four maximum messages by
  default, so a flood cannot hold the caller's tick. What it reads stays
  buffered for the next call.
- **This endpoint answers pings and sends none in version 1.** Liveness is the
  silence timeout (§6) and TCP's own reset.
- **`Close` sends close 1001** (going away) and releases the socket without
  waiting for the echo. `Close` does not block, as `Poll` does not.

## 6. The session and its state

[CONNECTOR_CONTRACT §5](CONNECTOR_CONTRACT.md#5-state)'s states, as this
connector reaches them:

| Event | State | Diagnostic |
| --- | --- | --- |
| `Open`, listen role, bound | `Connecting` | — |
| `Open`, listen role, bind failed | `Error` | `WEBSOCKET_SOCKET_BIND_FAILED` |
| `Open`, connect role | `Connecting`; the connection is attempted in `Poll` | — |
| a connect attempt refused or unreachable | `Connecting`; retried (§3) | `WEBSOCKET_CONNECT_FAILED`, once per episode |
| handshake complete | `Connecting` | — |
| a frame accepted | `Connected` | — |
| no message within the silence timeout | `Degraded` | `WEBSOCKET_SOURCE_TIMEOUT`, once per episode |
| a frame accepted after silence | `Connected` | — |
| the peer closed, or the connection reset | `Connecting`; a listener waits for the next peer, a connect role retries | `WEBSOCKET_PEER_DISCONNECTED`, with the close code in its detail |
| a framing violation (§5) | `Connecting`, as above | the §5 code |
| the listening socket fails | `Error` | `WEBSOCKET_SOCKET_BIND_FAILED` |

**A peer going away is a transition, never an `Error`** (CONNECTOR_CONTRACT
§5). The silence timeout defaults to 0, which is off, for the reason the
transport gives: how long a sender may stay quiet is the session's property,
not the socket's.

`ConnectorState` is the state of this link and not of the source behind it. A
bridged VMC sender that is `Degraded` upstream reaches this connector as a
quiet or sparse link. The upstream state is not carried across (`FW-O2`,
[MOTION_CONNECT §4](MOTION_CONNECT.md#4-state-and-diagnostics-stay-at-the-bridge)).

## 7. Frame assembly

**One message is one frame, and nothing is assembled across messages.** The
policy CONNECTOR_CONTRACT §7 asks each connector to state:

| Item | Policy |
| --- | --- |
| frame boundary | the message |
| packet arrival order, late packet | TCP delivers bytes in order, so messages are delivered in arrival order and nothing arrives late |
| duplicate packet | impossible below the message. A repeated sender `frameNumber` is a restart (below) |
| missing joint, stale joint value | the sender's. An absent joint stays absent, and this connector holds no joint state to carry forward, so it has no staleness horizon |
| sequence reset | `timing.sequence` is the original source's, carried verbatim and not checked again. The connector that first read the source checked it |
| sender restart | the sender's `frameNumber` not advancing within one connection (`n ≤ previous`), or the first frame of a new connection after an earlier one delivered frames: `WEBSOCKET_SOURCE_RESTARTED`, the frame is delivered, and the per-actor timestamp history starts again |
| a gap in the sender's `frameNumber` | `n > previous + 1`: `WEBSOCKET_FRAME_GAP`, whose subject is the missing range. The frame is delivered. The sender dropped frames, which is information, not a fault here |
| timestamp regression | an actor's pose `timestamp` that does not strictly increase within a session: the **whole frame** is dropped with `WEBSOCKET_TIMESTAMP_REGRESSION`, whose subject is the actor. A partly delivered frame would be a frame nobody sent |
| source clock drift, body / expression synchronization, root update rate | the sender's. Drift is estimated by the stream layer (CONNECTOR_CONTRACT §6), never here |

**What the connector rewrites, and what it carries.** FRAME_WIRE_FORMAT §7's
receiving half, made specific:

- `frameNumber` is this connector's own: from 1 after each `Open`, monotone
  across connections.
- `timing.receiveTimestamp` is this process's monotonic clock, read when the
  last fragment of the message was read, on the same axis as the session's
  other events.
- Everything else is carried verbatim: `sourceProfile`, `sourceTimestamp`,
  `sequence`, `sourceClock`, every actor and every pose, the pose's
  provenance metadata included. A bridged VMC frame still says `protocol`
  `vmc`, because that is where its values came from.
- **The profile is the sender's.** With `ConnectorConfig::sourceProfile`
  empty, any profile is accepted. With it set, a frame naming another profile
  is dropped with `WEBSOCKET_PROFILE_MISMATCH`, so a runtime that expects
  `mocopi.body.v1` is never handed trackers.
- **Capabilities are what the wire can carry**: body, hands, face, eyes, root
  motion, trackers, source timestamps, confidence and multiple actors. A
  relay can fill any of them, and a single frame may be sparser
  (CONNECTOR_CONTRACT §2.1). `controllers` is not among them, because version
  1 has no field for it.

### 7.1 Sending

`WebSocketFrameSender` writes what it is given, and the codec is a pure
function (FRAME_WIRE_FORMAT §7). So `bridge` sends the source connector's
frame as that connector delivered it, `frameNumber` and `receiveTimestamp`
included. The next receiver rewrites both. A frame the encoder refuses is
reported with its `WIRE_*` code and sent to nobody. The sender is caller-driven
like the connector: `Send` encodes and queues, and `Service` accepts peers,
answers control frames and flushes the queues without blocking.

## 8. Diagnostics

`WEBSOCKET_*` is this connector's namespace. It is frozen here, before the
code exists, as each imported connector's set was frozen before its decoder.
The set is appended to, never renumbered. A `WIRE_*` code from the codec
passes through unchanged ([DIAGNOSTICS.md](../reference/DIAGNOSTICS.md) §1).
Both directions raise from the same set.

| Code | Severity | Recoverable | Meaning | Subject |
| --- | --- | --- | --- | --- |
| `WEBSOCKET_SOCKET_BIND_FAILED` | error | no | The listener cannot bind its address and port, or its socket failed. | the requested endpoint |
| `WEBSOCKET_CONNECT_FAILED` | warning | yes | A connect role's peer refused or is unreachable. | the peer endpoint |
| `WEBSOCKET_HANDSHAKE_REFUSED` | warning | yes | An opening handshake failed: not an upgrade, a wrong path, no subprotocol, a head over 8 KiB or slower than 5 s. | the peer endpoint |
| `WEBSOCKET_ORIGIN_REFUSED` | warning | yes | A request's `Origin` is not on the allow list (§4.2). | the origin |
| `WEBSOCKET_PEER_REFUSED` | info | yes | A peer arrived when the receiving session or the sender's peer bound was full. | the peer endpoint |
| `WEBSOCKET_PROTOCOL_VIOLATION` | warning | yes | An RFC 6455 framing violation or invalid UTF-8 (§5); the connection was closed. | `byte N` of the connection's inbound stream |
| `WEBSOCKET_MESSAGE_TOO_LARGE` | warning | yes | A message would exceed the bound; the connection was closed with 1009. | `byte N` |
| `WEBSOCKET_UNSUPPORTED_MESSAGE` | warning | yes | A binary message; the connection was closed with 1003. | `byte N` |
| `WEBSOCKET_PEER_DISCONNECTED` | info | yes | The peer closed or the connection reset. | the peer endpoint |
| `WEBSOCKET_SOURCE_TIMEOUT` | warning | yes | No message arrived within the silence timeout. | the peer endpoint |
| `WEBSOCKET_SOURCE_RESTARTED` | info | yes | The sender's `frameNumber` went back, or a new connection began after frames (§7). | the sender's `frameNumber` |
| `WEBSOCKET_FRAME_GAP` | info | yes | The sender's `frameNumber` skipped (§7). | the missing range, `a..b` |
| `WEBSOCKET_TIMESTAMP_REGRESSION` | warning | yes | An actor's pose timestamp did not advance; the frame was dropped. | the actor |
| `WEBSOCKET_PROFILE_MISMATCH` | warning | yes | A frame's profile is not the configured one; the frame was dropped. | the frame's profile |

The catalog in DIAGNOSTICS.md takes these rows when the connector lands, with
the test that holds the enum order to the stable strings.

## 9. Raw capture

What the connector records is what it **received**, below decoding
([CONNECTOR_CONTRACT §12](CONNECTOR_CONTRACT.md#12-raw-capture)): **each
message after unmasking and reassembly, before the codec reads it.** One
message is one record, in `motionConnectorTransport`'s packet-capture format
with the magic `!websocket-packet-capture`. `p` lines name the peer, so a
capture shows a new connection as the change of peer it is. The maximum
message size (§5) equals that format's record bound, so every accepted message
can be recorded. The handshake, control frames and framing bytes are not
recorded. They are the session, not the source's data, and the framing corpus
(below) tests them on their own.

This is what FRAME_WIRE_FORMAT §1 meant by design policy §35's
`tests/data/websocket/body-pose.jsonl`. It is a capture of messages, and
because it is the transport's format it needs no reader of its own.

`StartCapture` resets saved messages; `StopCapture` and `Close` stop
appending and retain them. `Open` stops any previous capture. Live receive
times stay on the clock since `Open`; replay injection is excluded.
The shared CLI uses this facility as described in
[MOTION_CONNECT §5](MOTION_CONNECT.md#5-record).

The hardware-free path is `PushMessage(text, receiveTimestamp, peer)`, as
mocopi's is `PushDatagram`. A replayed capture goes through it and through the
same assembly as a live session. Two generated corpora come with the connector:

- **framing**: byte streams for the RFC 6455 layer, one per row of §5's table
  and per handshake refusal in §4, each with the code, the close status and the
  byte it names;
- **messages**: packet captures replayed through `PushMessage`, for §7's
  restart, gap, regression and profile rows.

## 10. Where the code lives

`motionConnectorWebSocket`
([WORKSPACE §1.1](../architecture/WORKSPACE.md#11-native-libraries)), built
when `MOTIONCONNECTORS_BUILD_WEBSOCKET` is on. It is on by default, because
the connector brings no third-party dependency. Inside it:

- **a socket-free RFC 6455 codec**: handshake request and response parsing
  and writing, the accept key, frame headers, masking and reassembly. It is
  tested on bytes alone, as `motionConnectorOsc` is;
- **a TCP layer**: a non-blocking listener and stream, on the OS sockets the
  transport already uses;
- **`WebSocketConnector`** and **`WebSocketFrameSender`** over both.

Its edges are `motionConnectorCore`, `motionConnectorWire`, and
`motionConnectorTransport` for the diagnostic vehicle and the capture format.
Both the codec and the TCP layer are private. Each becomes a library when
another component needs it, and not before. The transport was extracted the
same way, once there were copies to share (its `UdpReceiver.h`).

## 11. Open questions

| Id | Question | Resolve by |
| --- | --- | --- |
| WSC-O1 | TLS (`wss://`): which implementation (OpenSSL, or each platform's: SChannel, Secure Transport), declared how in the manifest, and whether a certificate is the operator's or generated | a session that must leave the machine across a network the operator does not trust |
| WSC-O2 | More than one receiving peer: whether one connector merges several senders' actors into one frame stream, and how their `ActorId`s stay apart | the first multi-actor source (`CC-O5`) |
