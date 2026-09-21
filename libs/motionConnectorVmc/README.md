# motionConnectorVmc

The VMC Protocol source adapter: OSC-over-UDP datagrams from any sender
application in, source-normalized motion observations out. Its current API is
source-specific; the v0.1.0 work adapts it to the shared `MotionFrame` contract.

```text
UDP datagram → OSC decode → VMC message decode → frame assembly
             → source joint mapping → shared MotionPose values → MotionFrame
```

**Status: imported source implementation with the first shared-contract adapter.**
The packet-capture format, OSC and VMC decoding, source joint map, frame
assembler, live receiver, [`vmc_record`](../../tools/vmcRecord) and
`VmcConnector` are tested in this repository. Replay and loopback evidence are
hardware-free. The source-specific path remains intact beneath the shared
adapter; mocopi and VRChat OSC adaptation are tracked in the [current
roadmap](../../docs/roadmap/current.md).

## What this is, structurally

A plain static CMake library with an `openstrata.library.yaml`, **not** a plugin
bundle. It registers
nothing with OpenUSD and ships no `plugInfo.json`, because
[WORKSPACE.md §2](../../docs/architecture/WORKSPACE.md) keeps it away from
`vrmSchema`, from every file-format bundle, and from OpenExec. Its manifest
declares the installed motion packages and the shared transport/wire leaves:

```text
motionConnectorVmc -> motionCore, motionSampling, motionRecording,
                      motionConnectorTransport, motionConnectorOsc
```

`tests/check_boundaries.py` is what makes that a fact rather than an intention.
It fails on a plugin manifest anywhere in the tree, on a stage/registration/exec
API in `include/` or `src/`, on a mention of a sibling adapter or a plugin
bundle, on a `target_link_libraries` naming anything but the two permitted
libraries, and on a binary whose imports leave the OpenUSD value-type layer.

That last one inspects the **test executable**, not the adapter's own archive.
A static `.lib`/`.a` records no imports at all — `dumpbin /dependents` on one
prints a section summary and nothing else — so a check pointed at the library
would be a gate that cannot fail. Pointed at the linked executable it has real
teeth: run against the linked connector test executable, it rejects stage-layer
imports such as `usd_sdf`, `usd_usd` and `usd_usdSkel`, which is exactly the
class of import this boundary exists to refuse.

## What it is not allowed to do

Target-skeleton discovery, retargeting, rest-pose correction, its own
interpolation, its own smoothing, `UsdSkelAnimation` authoring, stage authoring,
or a dependency on a sibling adapter. Every one of those already exists once in
this repository; a second copy inside an adapter is a forked pipeline that stays
invisible until two inputs disagree about the same avatar.

The adapter maps a VMC bone name to the shared motion vocabulary and stops. It
never resolves a joint index in a target skeleton; retargeting belongs to
`usd-motion-plugins` and avatar-format repositories downstream.

The recorder CLI captures raw packets and reports source evidence. It does not
retarget, author a stage or bind a capture to an avatar.

## Transport arrives last

The implementation order is deliberately backwards from the tempting one:
recorded-packet decoder → semantic mapping → frame assembly → live-source bridge
→ thin UDP receiver. Building the receiver first would make every subsequent
test require a live sender; building it last keeps the whole adapter verifiable
in CI from committed fixtures, with no hardware and no socket.

## Recorded input

`vmc-packet-capture` v1 — spec on
[`PacketCapture.h`](include/motionConnectorVmc/PacketCapture.h) — is what makes that
order possible: the datagrams a session delivered, verbatim, with the instant
each arrived. Line-oriented text, so a fixture diffs; hex with an ASCII gutter,
so an address pattern is legible without a decoder ring:

```text
d 0.000000 20
  2f 56 4d 43 2f 45 78 74 2f 54 00 00 2c 66 00 00  |/VMC/Ext/T..,f..|
  3d cc cc cd                                      |=...|
```

It is not a `motion-capture-trace`, and the difference is the adapter's two
ends: a trace records what an adapter *produced*, a capture what it was *given*.
Only the second can represent a truncated datagram, a duplicate delivery, or a
restart mid-frame — which is to say, only the second can test a decoder.

The corpus lives in [`tests/corpus/`](tests/corpus/) and is generated, never
recorded off a commercial sender, because a fixture carrying someone's avatar is
one CI cannot redistribute. Two tests hold it: `motionConnectorVmc_corpus` re-emits
every committed capture through the C++ writer and compares bytes, and
`motionConnectorVmc_packetGen` re-runs the generator and compares against that. A
hand-edited fixture that is still canonical fails the second, not the first.

## OSC, and only OSC

[`OscPacket.h`](include/motionConnectorVmc/OscPacket.h) decodes a datagram into
messages: addresses, type tags, arguments, bundles flattened into wire order. It
does not know that `/VMC/Ext/Bone/Pos` means anything, and that is what makes
both layers testable — OSC has its own malformed-input cases, and a decoder that
mixed the two could only ever be tested end to end.

Three rules are decisions rather than details:

- **A datagram decodes entirely or not at all.** A bundle whose third element is
  malformed yields no messages, not two. A half-decoded frame is worse than a
  refused one: the assembler cannot tell which half it got.
- **Every OSC 1.0 and 1.1 type tag is understood**, including the fourteen VMC
  never sends. Skipping an argument requires knowing its size, so a decoder that
  handled only `i`, `f` and `s` would have to refuse a valid message the moment
  a sender attached a `d` — blaming the sender for the decoder's gap.
- **The only refusal is `VRM_VMC_PACKET_MALFORMED`.** This layer cannot tell an
  unimplemented address from any other one; `/foo/bar` and `/VMC/Ext/Midi/Note`
  both decode cleanly here. `VRM_VMC_UNSUPPORTED_MESSAGE` belongs one layer up,
  where addresses have meanings.

Diagnostics carry the offending address as their subject and a byte offset in
their detail — inside a bundle, an offset into the *datagram*, so it can be
found in a committed capture rather than bisected.

## VMC, and not yet a humanoid

[`VmcMessage.h`](include/motionConnectorVmc/VmcMessage.h) is the layer where an
address means something. Seven messages decode; everything else is reported and
skipped:

```text
kind          | address              | known form | fields
--------------+----------------------+------------+---------------------
Availability  | /VMC/Ext/OK          | ,i[ii]     | availability
Time          | /VMC/Ext/T           | ,f         | seconds
Model         | /VMC/Ext/VRM         | ,ss        | name (path), title
RootTransform | /VMC/Ext/Root/Pos    | ,sfffffff  | name, transform
BoneTransform | /VMC/Ext/Bone/Pos    | ,sfffffff  | name, transform
BlendValue    | /VMC/Ext/Blend/Val   | ,sf        | name, value
BlendApply    | /VMC/Ext/Blend/Apply | ,          | —
```

A bone name stays plain text and a quaternion stays in the sender's
`(x, y, z, w)` order. Nothing is converted, normalised, or resolved against a
rig — handedness, up axis, units, and the map from "LeftUpperArm" to a
`openstrata::motion::HumanJoint` belong to the next layer, which is the one that knows what
the numbers are for.

Four more decisions, each written down where it is enforced:

- **A message is refused, never a packet.** The opposite of the OSC layer's rule,
  for the opposite reason: the framing is already established here, so one
  malformed `/VMC/Ext/Bone/Pos` costs that bone and not the twenty-one that came
  with it. A frame missing one bone is the assembler's ordinary business.
- **An unimplemented address is not a defect.** Every sender emits a headset
  transform, a camera, a MIDI note. `VRM_VMC_UNSUPPORTED_MESSAGE` is info and
  recoverable, `DecodeVmcPacket` still returns true, and the mixed-traffic
  capture is in the corpus to hold the two codes apart — ten of its ninety-three
  messages take that path.
- **A known address with the wrong arguments is malformed.** OSC puts an `f` and
  a `d` in the same field, so a decoder reading values without checking tags
  would accept `,sddddddd` as a bone pose and pin nothing about the wire format.
  The refusal quotes both tag strings.
- **Arguments past the known form are counted, never interpreted.** Longer forms
  are in the wild: a third string on `/VMC/Ext/VRM`, further status integers on
  `/VMC/Ext/OK`, more floats after `/VMC/Ext/Root/Pos`'s quaternion. Refusing
  those blames a sender for being newer; decoding them would invent a meaning for
  bytes no fixture here pins — which is why they are described by shape and not
  by what they are believed to mean. Each moves into the table above when a
  capture of it exists.

`motionConnectorVmc_vmcCorpus` runs both layers over all seven recorded captures —
568 decoded messages, twelve ignored, eight refused and nine arguments counted
but not read — and checks three things counts cannot:

- the neutral capture's rotations are all identity with its root at the origin,
  and its sender clock starts at 12.5 s where the receive clock starts at 0;
- the sender-restart capture's backwards clock decodes without complaint,
  because `VRM_VMC_TIMESTAMP_REGRESSION` needs a memory of the previous frame
  and this layer has none;
- the malformed-forms capture's bad bone costs **that bone** — the datagram
  carrying it still yields the twenty-two messages that arrived with it, which
  is the first rule above stated as a number.

## VMC's names and VMC's axes, into the shared motion vocabulary

[`SkeletonMap.h`](include/motionConnectorVmc/SkeletonMap.h) is the one conversion the
adapter exists to perform, and the first layer that knows the shared motion
vocabulary exists. It converts and it does not decide: frame boundaries,
missing bones and sender restarts belong to the assembler, and resolving a
target joint belongs to downstream retargeting.

**The vocabulary is Unity's `HumanBodyBones`, not VRM 1.0's.** A sender is a
Unity application and writes PascalCase where VRM 1.0 writes lowerCamel. The two
have the same 55 bones — but for the thumb they disagree about more than case,
because VRM 1.0 renamed the chain one joint down:

```text
Unity / VRM 0.x            VRM 1.0 / openstrata::motion::HumanJoint
LeftThumbProximal      ->  leftThumbMetacarpal
LeftThumbIntermediate  ->  leftThumbProximal
LeftThumbDistal        ->  leftThumbDistal
```

A map that lowercased the first letter would land every thumb rotation one joint
out while every other bone in the hand arrived correctly, so the table is
written out rather than derived and the tests state the thumb twice. The
spelling is matched exactly: an unrecognised name is
`VRM_VMC_UNSUPPORTED_MESSAGE` — info, recoverable, that bone ignored and the
frame kept — for the same reason an unimplemented address is.

**The basis change is VRM 1.0's, not VRM 0.x's.** Unity is left-handed with the
avatar facing +Z; the canonical contract is glTF's right-handed, Y-up, metre
basis. VRM 1.0 defines that conversion as a reflection through X where VRM 0.x
used Z, and this adapter follows VRM 1.0 because the vocabulary it converts into
is VRM 1.0's:

```text
position   (x, y, z)     ->  (-x, y, z)
rotation   (x, y, z, w)  ->  (w, (x, -y, -z))
```

The two sign flips on the imaginary part are one from the axis and one from the
reversed sense of rotation, which is why a rotation about +X survives unchanged
and one about +Z comes out about −Z. Quaternions are normalised on the way
through — senders emit un-normalised ones and a retarget composing them would
skew a joint. A zero-length or non-finite one is refused as
`VRM_VMC_PACKET_MALFORMED` instead: it names no orientation, and the value that
would have to be invented to carry on is exactly the identity a reader could not
tell from a real sample.

Two things this layer deliberately does not answer. **What a VMC bone rotation
means relative to rest** — it is the sender's local rotation, which equals the
rotation away from rest only when the sender's humanoid rest is identity; a
sender where it is not needs a downstream source-rest pose policy, and
manufacturing one from the first frame seen would be a guess. And **where a bone's position
goes** — the canonical pose carries rotations plus one `RootMotion`, so a bone's
local offset is the sender's rig geometry rather than motion. It is converted
and handed on unread, because whether the hips offset composes with
`/VMC/Ext/Root/Pos` needs the frame the assembler owns.

`motionConnectorVmc_skeletonMapCorpus` maps every transform in all seven captures —
493 bones and 24 roots, none unsupported and none refused, 232 of them reflected
off the X axis — and checks two claims counts cannot: a neutral pose is still a
neutral pose after the basis change, and the arm-raise capture's five rotations
about Unity's −Z come out about the canonical +Z at 0°, 15°, 30°, 45° and 60°.
That capture raises a *left* arm, which Unity puts at −X and glTF at +X, so the
sign flips with the basis and both describe the same arm going up — a conversion
that dropped the flip would lower it.

## Where a frame begins

[`FrameAssembler.h`](include/motionConnectorVmc/FrameAssembler.h) is the first layer
that *decides* rather than converts, and the decision the protocol forces is
where a frame begins — VMC promises nothing about one datagram being one frame.
The corpus already holds two sender shapes that disagree about it:

```text
bundled     | T(12.500) root Hips … UpperChest | T(12.533) root Hips …
unbundled   | root Hips … RightToes T(20.000)  | root Hips … T(20.033)
```

In one the clock *opens* the frame and in the other it *closes* it, so either
convention read as a rule produces one frame per two on the other sender — off
by half a frame, with every rotation in it still individually correct. Two rules
cover both: **a second clock ends the frame**, and **a repeat ends it unless it
arrived in the same datagram**, where the same repetition is
`VRM_VMC_DUPLICATE_BONE` instead. That exception is the only place a datagram
boundary is load-bearing anywhere in the adapter, and it is why the assembler
consumes packets rather than a flattened message stream.

A backwards clock means three things, told apart by one comparison against the
last accepted frame: **equal or slightly earlier** is
`VRM_VMC_TIMESTAMP_REGRESSION` and the frame is refused, which is what stops a
duplicated datagram from becoming a duplicated pose; **earlier by more than the
restart threshold** is `VRM_VMC_SOURCE_RESTARTED`; anything later is accepted. A
restart is reported and not repaired — offsetting the stream to keep timestamps
rising would manufacture continuity out of a discontinuity.

The assembler **holds nothing forward**: a bone the session has observed and this
frame did not carry is reported missing and the frame is still emitted, because
`MissingJointPolicy` is the intake's answer. A bone missing past the staleness
horizon is additionally `VRM_VMC_STALE_JOINT`, raised once per crossing rather
than per frame. Both are measured against the rig the session has actually
observed — a sender that solves no fingers is complete, not incomplete forty
times a second.

**Blend shapes are assembled exactly as bones are.** `/VMC/Ext/Blend/Val`
becomes `MotionPose::expressions` under the sender's own name — the same
repeat rule, the same duplicate reading, the same one-per-frame slot. Giving
expressions a rule of their own would be a third framing convention in a layer
whose argument is that two are already one too many. Two consequences are
deliberate: a frame carrying *only* expressions is emitted, because a pose can
hold them and refusing it would discard motion this layer can represent; and
`/VMC/Ext/Blend/Apply` is still only counted, because reading it as "the set is
complete" would make it a frame boundary that neither sender shape justifies.

There is no missing/stale reporting for expressions. That is measured against a
rig the session learned, and the expression vocabulary is the sender's model's
rather than a fixed one — "the sender stopped reporting `Joy`" is not the same
claim as "this rig solves no fingers", and inventing the stronger one here is
the kind of decision this adapter does not take.

`motionConnectorVmc_frameAssemblerCorpus` assembles all seven captures and makes the
claim this layer exists for: **both sender shapes yield five frames at the same
30 Hz cadence**, with the unbundled one's arm rising 15° per frame in the order
it was sent. It also pins the blend shapes the mixed-traffic capture carries —
`Joy`, `Blink`, and `A`, the last sent as `0.0` on every frame, which is the
corpus's record of the difference between a weight that is zero and a name that
was never sent.

## Source-specific live path

[`LiveSource.h`](include/motionConnectorVmc/LiveSource.h) is the source-specific
bridge at the end of this adapter. It forwards assembled source observations to
the installed motion packages; it does not own retargeting, filtering,
resampling, avatar binding or stage authoring. Those responsibilities remain
downstream, and the shared `motionConnectorCore` adapter is still pending.

The live-source corpus replays all committed captures without hardware and
checks the source-specific ordering, restart behavior and provenance. The
future `MotionFrame` adapter must preserve that evidence rather than introduce
a second VMC frame-assembly path.

## The socket, and the thread it does not create

[`UdpReceiver.h`](include/motionConnectorVmc/UdpReceiver.h) is the last layer written
and the first one a live session touches. It owns a socket, a bind address, a
receive clock and a size limit, and it owns no decoding at all: `Receive` hands
back the bytes exactly as they arrived, including the ones the layers above will
refuse, because a receiver that filtered its own input would make the corpus a
record of what the receiver let through rather than of what a sender sent.

**The thread boundary sits before the decoder, not after it.** Motion policy
§11.4 put a network thread on one side of a *thread-safe* pose buffer, and the
downstream motion intake has none — so rather than locking the buffer the whole
pipeline reads through, the hand-off moved one layer earlier:

```text
network thread → [ DatagramQueue ] → consumer thread → decode → pose
```

`DatagramQueue` is the only synchronised object in this adapter. Everything
downstream of `Drain` — all five layers above and the downstream motion intake — runs
on one thread exactly as its tests do. Overflow drops the **oldest** datagram,
because for live motion the alternative is indefensible: holding a stale frame
and refusing a fresh one adds latency the session never gets back.

A consumer that already has a tick needs no second thread at all, and should not
take one. `Receive` with a zero timeout is a true poll:

```cpp
ReceivedDatagram datagram;
while (receiver.Receive(&datagram) == ReceiveStatus::Received) {
    source.PushDatagram(datagram.bytes, datagram.receiveTime, &log);
}
```

Four more decisions are written down where they are enforced:

- **Every wait has a timeout**, for cancellation rather than latency. A thread
  parked in `recvfrom` can only be woken by closing the socket underneath it,
  which races the descriptor's reuse on every platform this repository builds
  for. A bounded wait turns stopping into a flag the loop already checks.
- **Nothing arrives truncated.** The buffer is `MaxDatagramBytes`, so truncation
  is impossible rather than configurable — a decision about blame, since a
  truncated datagram is indistinguishable at the OSC layer from a malformed one
  and a smaller buffer would let the receiver manufacture
  `VRM_VMC_PACKET_MALFORMED` against a sender that did nothing wrong.
- **The clock is monotonic**, which the capture format requires rather than
  prefers: it forbids backwards receive times because arrival order is the whole
  point of it, and a wall clock steps backwards for reasons that have nothing to
  do with the session. Counting from `Open` is also the origin
  `vmc-packet-capture` records against, so a recording tool copies the number
  instead of rebasing it.
- **One transport code, because one transport failure is fatal.**
  `VRM_VMC_SOCKET_BIND_FAILED` is the only socket failure a session cannot
  continue past, so the frozen set needed no ninth code: a lost datagram, a
  transient error and an empty poll are counts in `UdpReceiverStats`, not
  diagnostics that would say "recoverable" on every line.
- **The receive buffer is a request, and what was granted is read back.** Every
  platform may clamp `receiveBufferBytes` and Linux doubles it, so
  `GetReceiveBufferBytes()` reports what the socket actually has. Asking for four
  megabytes, silently getting the default, and then losing datagrams between two
  slow ticks is the hardest failure in this class to see from the outside.

`motionConnectorVmc_loopbackCorpus` replays all seven captures **through a real
socket** — 168 datagrams sent to a bound port and read back off it — and makes
the claim this layer exists for: the 22 poses that come out are `operator==`
identical to the ones the same bytes produce read from the file, with the arrival
clock the only thing the wire is allowed to have changed. One buffer is reused
for the whole replay, so the bridge's lifetime claim is checked by the poses
matching rather than by an assertion about bytes.

These are the only tests here that open a socket, which is why they are their own
CTest names (`motionConnectorVmc_udpReceiver`, `motionConnectorVmc_loopbackCorpus`): a
runner that forbids one excludes two names and loses no coverage of the decode
path. They bind loopback on an OS-assigned port, never 39539 — a suite that
claimed the real VMC port would fight a developer's own sender for it.

## The CLI, and what it is for

[`tools/vmcRecord/`](../../tools/vmcRecord) is `vmc_record`: the one part of this
adapter that meets a real sender.

```sh
vmc_record --output walk-01.vmcpackets --duration 10 --sender vseeface
vmc_record --inspect tests/corpus/arm-raise-30hz.vmcpackets
```

Everything above it is verifiable from committed bytes, which is this adapter's
build order and also its limit: the corpus is *generated*, so it reproduces the
protocol's shapes and not what any real application emits. Every item still open
in Milestone B is that shape — two senders validated, a device through a relay,
a recorded corpus — and none of them closes by writing more code. So the tool
turns one real session into the two things this repository can keep: a capture
file, and a statement of what was in it.

**The datagram reaches the file before the decoder sees it.** A recorder whose
decoder could refuse a datagram would record what this adapter already
understands, and the sessions worth recording are exactly the ones it might not.
The decode still runs in the same loop, because an operator with a sender open
needs to know now whether the session is worth keeping — what it produces is a
report, and a report is not a filter.

The report reads the layers' tallies together, in the order the questions are
asked when a session is not working. Two of its lines are not statistics:
`hips offset` and `root` are the evidence for the two questions the frame
assembler left to a real sender, reported as how far each value moved and never
as what it means. `--inspect` prints the same block from a file with no socket,
which is what makes the CLI testable in CI — and `vmc_record_loopback` raises
`motionConnectorVmc_loopbackCorpus`'s claim to the artifact an operator keeps: what
comes off the socket is byte-identical to what went in, and reports the same
motion as the file it was replayed from.

## Diagnostics

Eight codes, frozen in `include/motionConnectorVmc/Diagnostics.h` before the first
decoder exists so that the set describes the protocol rather than whichever bug
was chased last:

```text
VRM_VMC_PACKET_MALFORMED        VRM_VMC_UNSUPPORTED_MESSAGE
VRM_VMC_TIMESTAMP_REGRESSION    VRM_VMC_DUPLICATE_BONE
VRM_VMC_INCOMPLETE_FRAME        VRM_VMC_SOURCE_RESTARTED
VRM_VMC_SOCKET_BIND_FAILED      VRM_VMC_STALE_JOINT
```

`VRM_MOTION_*` is the canonical layer's namespace, not this one's: a reader can
tell a decode failure from a motion-contract violation without knowing which
adapter produced it. Exactly one code is non-recoverable — a receiver that never
bound has nothing to recover into.

## Building and testing

Composed with the rest of the workspace:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=<usd-install>
cmake --build build --config Release
ctest --test-dir build -R motionConnectorVmc
```

Or through the runtime `ost` resolves for the workspace:

```sh
ost build && ost test
```

Standalone — this directory is its own CMake project, resolving the installed
motion packages rather than in-tree targets:

```sh
cmake -S libs/motionConnectorVmc -B build/vmc \
      -DCMAKE_PREFIX_PATH="<usd-install>;<workspace-prefix>"
cmake --build build/vmc
```

`ost plugin build` is not the standalone route here: it takes a *bundle*
directory and refuses anything without an `openstrata.plugin.yaml`, which an
adapter does not have and must not grow.
