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

## 4. Resolved decisions

| Id | Decision | Resolved |
| --- | --- | --- |
| DIAG-O1 | Use `VMC_*`, `MOCOPI_*` and `VRCHAT_OSC_*` for source-owned connector diagnostics. Do not retain the imported `VRM_` prefix or add compatibility aliases; the stable strings are the public names and the enumerator spelling remains private. | 2026-09-21 |
