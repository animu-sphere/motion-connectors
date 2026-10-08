# motionConnectorWire

`motionConnectorWire` spells a `MotionFrame` as bytes and reads it back: one
UTF-8 JSON message per frame, identified as `openstrata.motion.frame/v1`
([FRAME_WIRE_FORMAT.md](../../docs/design/FRAME_WIRE_FORMAT.md)). It is the codec
the WebSocket connector, `motion_connect bridge` and a test harness share, and
it opens no socket: a wire format needs none
([WORKSPACE.md §2.2](../../docs/architecture/WORKSPACE.md#22-forbidden)).

```cpp
#include "motionConnectorWire/FrameWire.h"

namespace wire = openstrata::connectors::wire;

std::string message;
wire::WireError error;
if (!wire::EncodeFrame(frame, &message, &error)) { /* error.code, error.subject */ }

openstrata::connectors::core::MotionFrame received;
if (!wire::DecodeFrame(message, &received, &error)) { /* dropped; frame untouched */ }
```

Current capability status, test evidence and release availability are in the
[capability matrix](../../docs/reference/CAPABILITY_MATRIX.md).

## What it guarantees

- **Decode after encode is the identity** (§7), under the motion contract's
  `==` for the pose and field by field for the connector's types, and encoding
  the decoded frame writes the same bytes. The output is deterministic: keys in
  the format's order, joints in vocabulary order, channels and confidence in
  name order, every number in the shortest form that reads back exactly.
- **A message decodes entirely or not at all.** A refusal leaves the output
  frame untouched.
- **The writer refuses what the reader would refuse**: a non-finite number, a
  zero or non-unit quaternion, a repeated actor, tracker or channel, a string
  that is not UTF-8, and an enum value outside its vocabulary. It reads only
  what a value says it carries, so an unset slot is never refused.
- **Numbers are locale-independent.** A host process that set a
  comma-decimal `LC_NUMERIC` reads and writes the same bytes.

## Refusals

A refusal is a `WireError`: a `WIRE_*` code from the codec's own namespace
([DIAGNOSTICS.md](../../docs/reference/DIAGNOSTICS.md)), a subject and a detail.
The subject is a path into the message (`$.actors[0].pose.joints.leftHand`,
or `$.actors[0].pose.channels["mouth:aa"]` for a key that is not an
identifier), or `byte N` when the text is not JSON. Tests assert the code and
the subject, never the detail.

Before parsing, a message larger than `DecodeOptions::maxMessageBytes` (1 MiB
by default) is refused. The parser then refuses invalid UTF-8, a duplicate
key, and nesting deeper than the format's six levels, all before any field is
read.

## The JSON layer is here

[DEPENDENCIES.md §4](../../docs/architecture/DEPENDENCIES.md#4-per-connector-dependencies)
left the JSON parser open. It is `src/Json.cpp`, private to this library,
because §6's refusals are properties of the parse. A duplicate key has to be
seen before a DOM keeps one of the two. Nesting has to be bounded before the
recursion. A number has to stay text until the field it lands in says whether
it is a `float` or a `double`. Numbers are written with `std::to_chars` and
read with `strtof` / `strtod`, which are correctly rounded on all three lanes.

## Tests

| Test | Checks |
| --- | --- |
| `motionConnectorWire_frameWire` | round trips built in code (every field, both ends of the `float` and `double` range, `UINT64_MAX`), absence as an absent key, output order, every encoder refusal, the size bound, atomicity, a comma-decimal locale |
| `motionConnectorWire_corpus` | the generated corpus ([tests/corpus/](tests/corpus/README.md)): canonical messages re-encode byte for byte, accepted spellings decode, and each refusal reports its code and subject |
| `motionConnectorWire_messageGen` | the committed corpus is what `tools/generate_messages.py` writes |
| `motionConnectorWire_boundaries` | no other workspace library, socket, stage API, producer name or connector code in the sources; no stage or socket library in the test executable's imports |
