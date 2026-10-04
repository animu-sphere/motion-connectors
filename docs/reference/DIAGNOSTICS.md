# Diagnostics

The catalog of diagnostic codes this repository raises. Status (2026-09-21):
**the three imported families use their final names**. The `VRM_` prefix from
the source repository was not retained, so a consumer can identify the
protocol without coupling the diagnostic namespace to an avatar format.

The code sets remain owned by their connectors. DIAG-O1 was resolved in one
cross-connector change so the catalog, tests, tools and recorded manifests
agree on the same stable strings.

## 1. The record

A diagnostic is a **value** reported beside frames, never an exception thrown
into the runtime ([CONNECTOR_CONTRACT.md §9](../design/CONNECTOR_CONTRACT.md#9-diagnostics)):

- a code;
- a severity from one table, so a call site cannot choose it;
- a subject naming what it is about: a joint, an address, a packet sequence
  or a byte offset;
- a detail sentence for a person;
- whether it is **recoverable**.

Tests assert the code and the subject, never the prose.

Namespaces are separate by layer, so a reader can tell a decode failure from a
contract violation without knowing which connector produced it:

| Layer | Owns |
| --- | --- |
| transport | bind, receive, capture-file failures |
| wire format (OSC) | malformed packets, as protocol-neutral events |
| frame wire format (`motionConnectorWire`) | a refused `openstrata.motion.frame/v1` message, `WIRE_*` |
| each connector | its protocol's semantics and frame assembly, frozen before its decoder |
| connector core | state transitions, buffer overflow, contract violations (non-finite values, clock regressions) |

A code raised by `usd-motion-plugins` keeps its own code when it passes
through; this repository never re-codes it.

## 2. Catalog

| Code | Severity | Recoverable | Raised by | Meaning |
| --- | --- | --- | --- | --- |
| `VMC_PACKET_MALFORMED` | warning | yes | `motionConnectorVmc` | The datagram or an OSC argument is not decodable. |
| `VMC_UNSUPPORTED_MESSAGE` | info | yes | `motionConnectorVmc` | A well-formed message uses an address this adapter does not implement. |
| `VMC_TIMESTAMP_REGRESSION` | warning | yes | `motionConnectorVmc` | An accepted frame timestamp does not advance. |
| `VMC_DUPLICATE_BONE` | warning | yes | `motionConnectorVmc` | A frame contains the same humanoid bone more than once. |
| `VMC_INCOMPLETE_FRAME` | warning | yes | `motionConnectorVmc` | A frame boundary arrives with expected content missing. |
| `VMC_SOURCE_RESTARTED` | info | yes | `motionConnectorVmc` | The sender sequence or clock starts again. |
| `VMC_SOCKET_BIND_FAILED` | error | no | `motionConnectorVmc` | The receiver cannot bind its listen address and port. |
| `VMC_STALE_JOINT` | warning | yes | `motionConnectorVmc` | A joint has exceeded the staleness horizon without an update. |
| `MOCOPI_SOCKET_BIND_FAILED` | error | no | `motionConnectorMocopi` | The receiver cannot bind its listen address and port. |
| `MOCOPI_TRACKING_LOST` | warning | yes | `motionConnectorMocopi` | The source cannot currently solve a joint or the body. |
| `MOCOPI_DEVICE_UNAVAILABLE` | warning | yes | `motionConnectorMocopi` | No packets arrive from the expected source. |
| `MOCOPI_TIMESTAMP_INVALID` | warning | yes | `motionConnectorMocopi` | A frame timestamp is non-finite or cannot order frames. |
| `MOCOPI_UNSUPPORTED_JOINT` | info | yes | `motionConnectorMocopi` | The source reports a joint with no canonical humanoid mapping. |
| `MOCOPI_SOURCE_RESTARTED` | info | yes | `motionConnectorMocopi` | The source sequence or clock starts again. |
| `MOCOPI_PACKET_MALFORMED` | warning | yes | `motionConnectorMocopi` | The datagram or a field length or type is invalid. |
| `MOCOPI_FRAME_INCOMPLETE` | warning | yes | `motionConnectorMocopi` | A frame boundary arrives with expected content missing. |
| `MOCOPI_NON_FINITE_TRANSFORM` | warning | yes | `motionConnectorMocopi` | A transform is non-finite or has a zero-length rotation. |
| `VRCHAT_OSC_PACKET_MALFORMED` | warning | yes | `motionConnectorVrchatOsc` | The datagram is not a decodable OSC packet. |
| `VRCHAT_OSC_UNSUPPORTED_ADDRESS` | info | yes | `motionConnectorVrchatOsc` | A well-formed OSC address is outside the tracker subset. |
| `VRCHAT_OSC_ARGUMENT_MISMATCH` | warning | yes | `motionConnectorVrchatOsc` | A known address has the wrong argument count or types. |
| `VRCHAT_OSC_TRACKER_ID_INVALID` | warning | yes | `motionConnectorVrchatOsc` | The tracker identifier is absent or outside the supported range. |
| `VRCHAT_OSC_TRACKER_PARTIAL` | warning | yes | `motionConnectorVrchatOsc` | A tracker reports position or rotation without the other part. |
| `VRCHAT_OSC_SOURCE_TIMEOUT` | warning | yes | `motionConnectorVrchatOsc` | No packets arrive from the expected source within the timeout. |
| `VRCHAT_OSC_SOURCE_RESTARTED` | info | yes | `motionConnectorVrchatOsc` | The source stream starts again from the beginning. |
| `VRCHAT_OSC_COORDINATE_INVALID` | warning | yes | `motionConnectorVrchatOsc` | A coordinate is non-finite or has a zero-length rotation. |
| `VRCHAT_OSC_SOCKET_BIND_FAILED` | error | no | `motionConnectorVrchatOsc` | The receiver cannot bind its listen address and port. |
| `VRCHAT_OSC_CALIBRATION_REQUIRED` | warning | yes | `motionConnectorVrchatOsc` | The stream is uncalibrated and cannot yet name its tracking space. |
| `WEBSOCKET_SOCKET_BIND_FAILED` | error | no | `motionConnectorWebSocket` | The listener cannot bind its address and port, or its socket failed. |
| `WEBSOCKET_CONNECT_FAILED` | warning | yes | `motionConnectorWebSocket` | A connect role's peer refused or is unreachable; once per episode. |
| `WEBSOCKET_HANDSHAKE_REFUSED` | warning | yes | `motionConnectorWebSocket` | An opening handshake failed: not an upgrade, a wrong path, no subprotocol, a head over 8 KiB or too slow. |
| `WEBSOCKET_ORIGIN_REFUSED` | warning | yes | `motionConnectorWebSocket` | A request's `Origin` is not on the allow list. |
| `WEBSOCKET_PEER_REFUSED` | info | yes | `motionConnectorWebSocket` | A peer arrived when the receiving session or the sender's peer bound was full; answered 503. |
| `WEBSOCKET_PROTOCOL_VIOLATION` | warning | yes | `motionConnectorWebSocket` | An RFC 6455 framing violation or invalid UTF-8; the connection was closed with 1002 or 1007. |
| `WEBSOCKET_MESSAGE_TOO_LARGE` | warning | yes | `motionConnectorWebSocket` | A message would exceed the bound; closed with 1009, or, sending, refused. |
| `WEBSOCKET_UNSUPPORTED_MESSAGE` | warning | yes | `motionConnectorWebSocket` | A binary message; closed with 1003. |
| `WEBSOCKET_PEER_DISCONNECTED` | info | yes | `motionConnectorWebSocket` | The peer closed or the connection reset. |
| `WEBSOCKET_SOURCE_TIMEOUT` | warning | yes | `motionConnectorWebSocket` | No message arrived within the silence timeout; once per episode. |
| `WEBSOCKET_SOURCE_RESTARTED` | info | yes | `motionConnectorWebSocket` | The sender's `frameNumber` went back, or a new connection began after frames. |
| `WEBSOCKET_FRAME_GAP` | info | yes | `motionConnectorWebSocket` | The sender's `frameNumber` skipped; the subject is the missing range. |
| `WEBSOCKET_TIMESTAMP_REGRESSION` | warning | yes | `motionConnectorWebSocket` | An actor's pose timestamp did not advance; the whole frame was dropped. |
| `WEBSOCKET_PROFILE_MISMATCH` | warning | yes | `motionConnectorWebSocket` | A frame's profile is not the configured one; the frame was dropped. |
| `WIRE_MESSAGE_TOO_LARGE` | warning | yes | `motionConnectorWire` | The message is over the declared maximum size; nothing was parsed. |
| `WIRE_MESSAGE_MALFORMED` | warning | yes | `motionConnectorWire` | The message is not JSON text, or not UTF-8; the writer raises it for a string that is not UTF-8. |
| `WIRE_NESTING_TOO_DEEP` | warning | yes | `motionConnectorWire` | The message nests deeper than the format's six levels. |
| `WIRE_KEY_DUPLICATE` | warning | yes | `motionConnectorWire` | An object has the same key twice; the writer raises it for a repeated channel. |
| `WIRE_FORMAT_UNKNOWN` | warning | yes | `motionConnectorWire` | `format` names a version this reader was not built for. |
| `WIRE_KEY_UNKNOWN` | warning | yes | `motionConnectorWire` | A key version 1 does not have, at any depth. |
| `WIRE_KEY_MISSING` | warning | yes | `motionConnectorWire` | A required key is absent. |
| `WIRE_TYPE_MISMATCH` | warning | yes | `motionConnectorWire` | A value has the wrong JSON type or array length; `null` is always one. |
| `WIRE_VALUE_UNKNOWN` | warning | yes | `motionConnectorWire` | A clock, source kind or contact word outside its vocabulary. |
| `WIRE_JOINT_UNKNOWN` | warning | yes | `motionConnectorWire` | A joint name the vocabulary does not have. |
| `WIRE_CONFIDENCE_MISMATCH` | warning | yes | `motionConnectorWire` | `confidence` and `joints` do not have the same keys. |
| `WIRE_NUMBER_OUT_OF_RANGE` | warning | yes | `motionConnectorWire` | A number overflows the `float` or `double` it is read into. |
| `WIRE_QUATERNION_INVALID` | warning | yes | `motionConnectorWire` | A rotation is zero or not unit length. |
| `WIRE_COUNTER_INVALID` | warning | yes | `motionConnectorWire` | A counter string is not a decimal `uint64` without a leading zero. |
| `WIRE_ID_DUPLICATE` | warning | yes | `motionConnectorWire` | An actor appears twice in a frame, or a tracker twice in an actor. |
| `WIRE_VALUE_NOT_FINITE` | warning | yes | `motionConnectorWire` | The writer was handed a non-finite number, which JSON cannot spell. |

## 3. Imported families

These families arrived with the connectors and now use the final names below.
The enum order and the stable strings are tested together in each adapter.

| Family | Codes | Raised by |
| --- | --- | --- |
| `VMC_*` | 8 | `motionConnectorVmc` |
| `MOCOPI_*` | 9 | `motionConnectorMocopi` |
| `VRCHAT_OSC_*` | 10 | `motionConnectorVrchatOsc` |

`liveTransport` and `osc` hold no code enum of their own: they report through
the connector that links them.

`motionConnectorWire` is not imported, and it is the one shared library with
a code set of its own, `WIRE_*` (16 codes): a refused frame message is the same
event whichever connector or tool decoded it, so the codec names it
([FRAME_WIRE_FORMAT §6](../design/FRAME_WIRE_FORMAT.md#6-what-a-reader-refuses)).
Its enum order and stable strings are tested together in
`motionConnectorWire_frameWire`, and every reader code has a corpus message.

`motionConnectorWebSocket` is not imported either. Its `WEBSOCKET_*` set (14
codes) was frozen in
[WEBSOCKET_CONNECTOR §8](../design/WEBSOCKET_CONNECTOR.md#8-diagnostics)
before the code existed, and both directions raise from it. A `WIRE_*` code
from the codec passes through it unchanged. Its enum order and stable strings
are tested together in `motionConnectorWebSocket_connector`; the framing
corpus raises every code a byte stream can, and the message corpus every code
of §7.

## 4. Resolved decisions

| Id | Decision | Resolved |
| --- | --- | --- |
| DIAG-O1 | Use `VMC_*`, `MOCOPI_*` and `VRCHAT_OSC_*` for source-owned connector diagnostics. Do not retain the imported `VRM_` prefix or add compatibility aliases; the stable strings are the public names and the enumerator spelling remains private. | 2026-09-21 |
