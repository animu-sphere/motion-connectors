# `MotionFrame` wire format

> Status: **accepted**, 2026-10-04 (CC-O8); §3–§6 are **binding** since
> 2026-10-04, when `motionConnectorWire` landed with its suite and generated
> corpus. §7's half about the receiving connector binds when the WebSocket
> connector lands. The capability matrix says what is implemented.
>
> This document owns how a `MotionFrame` is **spelled as bytes** when it
> crosses a process boundary: over WebSocket, into a browser, and to any other
> consumer that does not link the C++ types. On that area it wins over
> [DESIGN_POLICY.md](DESIGN_POLICY.md) §37. What a frame *means* is
> [CONNECTOR_CONTRACT.md](CONNECTOR_CONTRACT.md)'s, and the pose inside it is
> `usd-motion-plugins`' `MOTION_CONTRACT.md`'s; neither is restated here.
> Section numbers are stable; open questions are `FW-O<n>` and never reused.

---

## 1. Scope

- **In:** the encoding of one `MotionFrame` as one message, its format
  identifier and versioning, the spelling of every field, and what a reader
  refuses.
- **Out:** the meaning of the fields
  ([CONNECTOR_CONTRACT.md](CONNECTOR_CONTRACT.md) §3–§6, the motion contract
  §5–§7); WebSocket framing, connection state and reconnects (the WebSocket
  connector's); a C ABI (`CC-O7`); a motion file format.

**The wire format is a message format, not a file format.** A WebSocket
session is kept for diagnosis and test corpora the way every other session is
kept, as a raw capture of what was received
([CONNECTOR_CONTRACT §12](CONNECTOR_CONTRACT.md#12-raw-capture)); canonical
motion is recorded as `usd-motion-plugins`' `motion-capture-trace`. The
design policy's §35 sketch of `tests/data/websocket/body-pose.jsonl` is
therefore a capture of messages, not a second recording format.

## 2. The decision

CC-O8 was decided on 2026-10-04: **version 1 is JSON**, one UTF-8 JSON object
per frame, identified as `openstrata.motion.frame/v1`.

- **Debuggable first** (design policy §37). A frame on the wire reads in a
  browser's network panel, in `websocat` and in a test assertion without a
  schema compiler.
- **The browser is the first non-native consumer** (design policy §18), and
  JSON needs no package there. MessagePack, CBOR, FlatBuffers and Protobuf each
  need one, and FlatBuffers and Protobuf add code generation to both sides.
- **No measured budget asks for binary yet.** Size and parse cost are
  estimates until a session measures them; a binary encoding is `FW-O1`, and
  it encodes this same logical message.
- **JSON is not the ABI.** The C ABI (`CC-O7`) and the WASM bridge (`WS-O4`)
  are decided separately. This format names no OpenUSD type, so it holds
  whichever way `WS-O4` goes.

The identifier follows the installed profiles'
`openstrata.motion.source-profile/v1`
([SOURCE_PROFILES §5](SOURCE_PROFILES.md#5-where-profiles-live)).

## 3. The message

```json
{
  "format": "openstrata.motion.frame/v1",
  "frameNumber": "1042",
  "sourceProfile": "vmc.v1",
  "timing": {
    "sourceTimestamp": 1789795161.0333333,
    "receiveTimestamp": 5123.481902,
    "sequence": "88213",
    "sourceClock": "device"
  },
  "actors": [
    {
      "actor": "0",
      "pose": {
        "timestamp": 1789795161.0333333,
        "root": {
          "worldPosition": [0, 0.9, 0],
          "worldOrientation": [1, 0, 0, 0]
        },
        "joints": {
          "hips": [1, 0, 0, 0],
          "spine": [0.9999619, 0.0087265, 0, 0]
        },
        "channels": { "vrm:aa": 0.25, "vrm:happy": 0.75 },
        "metadata": {
          "kind": "liveCapture",
          "provider": "example.sender",
          "protocol": "vmc",
          "sourceId": "Example Avatar",
          "sequenceNumber": "88213"
        }
      },
      "trackers": []
    }
  ]
}
```

### 3.1 The frame

| Key | Value | Required |
| --- | --- | --- |
| `format` | `"openstrata.motion.frame/v1"` | yes |
| `frameNumber` | `MotionFrame::frameNumber`, a decimal string (§4.3) | yes |
| `sourceProfile` | the profile id ([SOURCE_PROFILES §2](SOURCE_PROFILES.md#2-identifiers)) | yes |
| `timing` | §3.2 | yes |
| `actors` | an array of §3.3, possibly empty | yes |

### 3.2 `timing`

| Key | Value | Required |
| --- | --- | --- |
| `sourceTimestamp` | seconds on the source's clock | when the source stamped |
| `receiveTimestamp` | seconds on the encoding process's monotonic clock | yes |
| `sequence` | the source's counter, a decimal string | when the source numbered |
| `sourceClock` | `device`, `localMonotonic`, `wall`, `networkSynchronized` or `none` | yes |

### 3.3 An actor

| Key | Value | Required |
| --- | --- | --- |
| `actor` | the `ActorId`, a string (§4.4) | yes |
| `pose` | §3.4; absent when the actor carries no pose | no |
| `trackers` | an array of §3.5, possibly empty | yes |

### 3.4 A pose

Each key is a `MotionPose` field; the motion contract owns what it means.

| Key | Value | Required |
| --- | --- | --- |
| `timestamp` | seconds | yes |
| `root` | `worldPosition`, `worldOrientation`, `linearVelocity`, `angularVelocity`, each present only when its `has*` flag is set | yes, possibly `{}` |
| `joints` | an object from joint name to `[w, x, y, z]`, holding exactly the joints `validRotations` sets | yes, possibly `{}` |
| `confidence` | an object from joint name to a number, over the same keys as `joints` | when the pose carries confidence |
| `contacts` | `{ "leftFoot": c, "rightFoot": c }`, `c` one of `unknown`, `contact`, `free` | when the pose carries contacts |
| `lookAtTarget` | `[x, y, z]` | when the pose carries one |
| `channels` | an object from channel name to a number | yes, possibly `{}` |
| `metadata` | `kind` (`clip`, `liveCapture`, `generated`, `procedural`, `simulated`), `provider`, `protocol`, `sourceId`, and, when the sample carries them, `sourceTimestamp` and `sequenceNumber` (a decimal string) | yes |

### 3.5 A tracker observation

| Key | Value | Required |
| --- | --- | --- |
| `trackerId` | the source's identifier, verbatim | yes |
| `position` | `[x, y, z]`, canonical basis, metres | when `hasPosition` |
| `rotation` | `[w, x, y, z]`, canonical basis | when `hasRotation` |
| `confidence` | a number | when the observation carries one |

## 4. Spelling

### 4.1 Absence is an absent key

A value the frame does not carry is a key the message does not have, never
`null`, a zero or an identity. An absent joint is not an identity rotation
(the motion contract §5.2), an unreported channel is not a zero weight (its
§6), and a missing `has*` value is not a zero vector (its §5.3); a key that is
absent is how each of those reaches the wire. `null` appears nowhere in
version 1.

### 4.2 Names, not indices

Joints are keyed by name, spelled as `openstrata::motion::HumanJointName`
spells them, as the trace format spells them. A reader does not depend on the
order of an enum it may not share, and a message reads without a table. Contact
values are the trace's words. A channel name is carried verbatim, as any JSON
string: unlike the line-oriented trace, the format can carry a name with a
space in it.

### 4.3 Numbers

- **Rotations are `[w, x, y, z]`**, as in the trace and in `GfQuatf`'s
  constructor.
- **A number round-trips exactly.** A writer emits the shortest decimal that
  reads back to the same `float` or `double`, so decode after encode returns a
  frame equal under the `==` the motion contract defines for every aggregate
  that crosses a boundary (its §5.2). Unlike the trace's six decimals, there is
  no fixed precision to quantise a timestamp.
- **A 64-bit counter is a decimal string** — `frameNumber`, `sequence` and
  `sequenceNumber`. A JavaScript reader parses JSON numbers as doubles and
  would round a counter past 2^53 without saying so. It is spelled one way:
  digits only, with no sign and no leading zero, so a decoded counter
  re-encodes to the string it came from.
- **Non-finite values cannot be spelled.** JSON has no `NaN` or `Infinity`, so
  a writer that is handed one refuses the whole frame rather than emitting a
  file its reader refuses; a frame carrying one would already be a connector
  bug (CONNECTOR_CONTRACT §10).

### 4.4 `ActorId`

`ActorId` travels as a string. `CC-O5` is still open; a string carries an
integer, a name or a source-scoped pair spelled into one, so deciding it does
not change version 1's shape.

### 4.5 Order

A writer emits keys in a fixed order — the tables' order, joints in enum
order, channels and confidence in name order — so two runs of one pipeline
produce the same bytes and a golden message can be compared rather than merely
parsed. A reader does not depend on key order.

## 5. Versions

A version is a claim about content and is checked in both directions, as the
trace's is:

- `format` comes first in every message, and a reader refuses a message whose
  `format` it does not know.
- **A version 1 reader refuses an unknown key**, at any depth. Adding a field
  is a new version, never a silent extension; a reader that skipped unknown
  keys would read a version 2 frame as a different, plausible version 1 frame.
- A reader reads every version it was built for; a writer writes only the
  current one.
- **Anything added to `MotionFrame`, or to the `MotionPose` it carries, is
  added here in the same change**, as the motion contract requires of its
  trace (its §10). A field that reaches the C++ type and not the wire is a
  field a bridged session silently loses.

## 6. What a reader refuses

Every message is untrusted (CONNECTOR_CONTRACT §10). A reader:

- checks a declared maximum message size before parsing;
- refuses invalid UTF-8, a duplicate key, nesting deeper than the format has,
  and a value of the wrong JSON type;
- refuses an unknown joint name — a typo must not read as a missing limb —
  and a `confidence` whose keys are not exactly `joints`' keys, in either
  direction;
- refuses a number that overflows its C++ type, a zero or non-unit quaternion
  (by the motion contract's tolerance, as the trace's reader does: a squared
  length within 1e-3 of one), and a counter string that is not a decimal
  `uint64` spelled as §4.3 spells it;
- refuses a duplicate `trackerId` within one actor and a duplicate `actor`
  within one frame.

A refused message is a diagnostic in the codec's own `WIRE_*` namespace
([DIAGNOSTICS.md](../reference/DIAGNOSTICS.md)) and a dropped message, never a
crash and never a partly filled frame. Its subject is a path into the message
(`$.actors[0].pose.joints.leftHand`), or `byte N` when the text is not JSON.
The generated corpus tests each refusal.

The writer refuses the same things rather than emitting them: a non-finite
number (§4.3), a zero or non-unit quaternion, a repeated actor, tracker or
channel, and a string that is not UTF-8. A message this codec wrote is one it
reads.

## 7. Encoding is not receiving

The codec is a pure function both ways: it reads and writes every field above,
`receiveTimestamp` and `frameNumber` included, and decode after encode is the
identity. **The WebSocket connector is still a connector**, so the frame it
delivers follows CONNECTOR_CONTRACT §3 and §6: its `receiveTimestamp` is read
from the receiving process's clock at receipt, and its `frameNumber` is its
own. The sender's values arrive as the input to that connector's frame
assembly (§7), which decides — with the connector, and tested — how a sender
restart or a gap in the sender's `frameNumber` is reported. Timestamps from
two machines are not compared without an estimated offset, and the format does
not pretend otherwise.

## 8. Where the codec lives

The codec is the reserved library `motionConnectorWire`
([WORKSPACE §1.1](../architecture/WORKSPACE.md#11-native-libraries)), for the
reason `motionConnectorOsc` is separate from `motionConnectorTransport`: a wire
format needs no socket. The WebSocket connector, `motion_connect bridge` and
a test harness each encode or decode a frame, and only the first of them opens
a socket. The JS / TS package decodes the same message natively
([WORKSPACE §1.3](../architecture/WORKSPACE.md#13-tools-examples-bindings-and-data)).

## 9. Open questions

CC-O8 is resolved by §2 ([CONNECTOR_CONTRACT §13](CONNECTOR_CONTRACT.md#13-open-questions)).

| Id | Question | Resolve by |
| --- | --- | --- |
| FW-O1 | A binary encoding of the same logical message (MessagePack, CBOR, FlatBuffers, Protobuf or a custom layout), negotiated beside version 1 rather than replacing it | a measured session whose size or parse cost JSON does not meet |
| FW-O2 | Whether connector state and diagnostics cross the wire, as messages of their own beside frames, so a bridged consumer sees a `Degraded` source as the sender did | `motionConnectorWebSocket` and `motion_connect bridge` (v0.2.0) |
