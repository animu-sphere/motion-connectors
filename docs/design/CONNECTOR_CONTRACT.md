# Connector contract

> Status: **core and source convergence implemented, release evidence in
> progress**, 2026-09-21. This document defines the shared connector contract.
> VMC, mocopi and VRChat OSC now have `IMotionConnector` adapters, and every
> committed capture corpus is replayed through those adapters; the capability
> matrix says what is implemented.
> A section becomes **binding** when the code it describes lands here with its
> tests.
>
> This document owns what lies **around** a pose: the connector interface,
> `MotionFrame`, tracker observations, state, capabilities, receive-side time,
> frame assembly, buffering and actors. On that area it wins over
> [DESIGN_POLICY.md](DESIGN_POLICY.md) §5 and §9–§15. The pose itself —
> `MotionPose`, `RootMotion`, `MotionChannelSet`, `SourceMetadata` and the
> `MotionStream` intake rules — is `usd-motion-plugins`' `MOTION_CONTRACT.md`'s
> and is not restated here ([DESIGN_POLICY.md §46.1](DESIGN_POLICY.md#461-the-pose-is-usd-motion-plugins-motionpose)).
> Bases are [COORDINATE_SYSTEMS.md](COORDINATE_SYSTEMS.md)'s; profiles and joint
> names are [SOURCE_PROFILES.md](SOURCE_PROFILES.md)'s. Section numbers are
> stable; open questions are `CC-O<n>` and never reused.

---

## 1. Scope

- **In:** `IMotionConnector`, `ConnectorConfig`, `ConnectorState`,
  `ConnectorCapabilities`, `MotionFrame`, `TrackerObservation`, `ActorId`,
  receive-side time, frame assembly, the connector-side buffer, raw packet
  capture.
- **Out:** the pose values (`usd-motion-plugins`), retargeting, filtering and
  smoothing, canonical recording (`usd-motion-plugins`' `motionRecording`),
  anything that opens a `UsdStage`, avatar-format semantics, scheduling
  (`usd-avatar-runtime`).

Every connector ends where the design policy's §45 says it ends:

```text
connect → decode → normalize (basis, units, names) → timestamp → identify → MotionFrame
```

and does **not** implement, even privately: target-skeleton discovery,
retargeting, rest-pose correction, its own interpolation, smoothing or
filtering, USD authoring, binding to an avatar, or a dependency on a sibling
connector. Every one of those exists once, downstream; a connector that grows a
second copy has forked the pipeline, and the fork is invisible until two
sources disagree about one avatar (measured as a rule in `usd-vrm-plugins`'
adapter plan §2).

## 2. `IMotionConnector`

```cpp
class IMotionConnector {
public:
    virtual ~IMotionConnector() = default;

    virtual Status Open(const ConnectorConfig&) = 0;
    virtual void Close() = 0;

    virtual ConnectorState GetState() const = 0;
    virtual ConnectorCapabilities GetCapabilities() const = 0;

    // Non-blocking. True when a frame was written to `out`.
    virtual bool Poll(MotionFrame& out) = 0;
};
```

Rules:

- **Small interface, no hierarchy.** A connector is one class implementing
  this, composed from the shared leaves (transport, wire format). No
  intermediate base classes per transport family (design policy §9).
- **No `UsdStage`, ever** (design policy Rule 5, §24). A connector is usable
  from a test, a CLI and a browser with no stage in the process.
- **The connector does not own the loop** (design policy §23). `Poll` never
  blocks; the caller's tick decides when to ask.
- **Configuration is explicit, never inferred.** Bind address, port, profile
  and coordinate conversion are stated in `ConnectorConfig`; a connector does
  not guess a sender from its traffic (measured: `usd-vrm-plugins` adapter plan
  §5.2).
- **`Open` failing is a state, not an exception.** A bind failure is reported
  through `Status` and `ConnectorState::Error` with a diagnostic (§9), and the
  caller may retry.

### 2.1 Capabilities

`ConnectorCapabilities` is a set of flags a connector declares and its tests
hold it to; it is a descriptor, not a class hierarchy (design policy §9).

| Capability | Meaning |
| --- | --- |
| `body` | torso, head and limb joints in the pose |
| `hands` | finger joints in the pose (design policy §5.3's `hand.left`, `hand.right`) |
| `face` | expression channels in the pose (§5.3's `face`, `blendshape.*`) |
| `eyes` | eye joints or gaze |
| `root-motion` | `RootMotion` position or orientation |
| `trackers` | `TrackerObservation`s (§4) |
| `controllers` | controller poses and inputs |
| `source-timestamps` | a source clock (§6) |
| `confidence` | per-joint or per-frame confidence |
| `multiple-actors` | more than one `ActorId` (§11) |

A capability says what a connector *can* fill; any single frame may be
sparser.

## 3. `MotionFrame`

A `MotionFrame` is everything one connector observed for one instant, for one
or more actors:

```cpp
struct ActorFrame {
    ActorId actor;
    std::optional<openstrata::motion::MotionPose> pose;   // usd-motion-plugins
    std::vector<TrackerObservation> trackers;             // §4
};

struct MotionFrame {
    std::vector<ActorFrame> actors;
    FrameTiming timing;                                   // §6
    std::string sourceProfile;                            // SOURCE_PROFILES.md
    std::uint64_t frameNumber;                            // this connector's, monotone
};
```

- **The pose is the shared type.** Body joints, finger joints, root motion and
  expression weights all travel in `MotionPose` exactly as the motion contract
  defines them; hands are joints of the shared vocabulary and faces are
  namespaced channels
  ([DESIGN_POLICY.md §46.5](DESIGN_POLICY.md#465-face-and-hands-as-the-shared-vocabulary-already-has-them)).
  A connector that observes only hands emits a sparse pose; an absent joint is
  absent, never identity.
- **`MotionPose::metadata` carries provenance** (`provider`, `protocol`,
  `sourceId`, `sourceTimestamp`, `sequenceNumber`), and provenance is never a
  branch condition downstream.
- **Diagnostics are not in the frame.** They are reported beside it (§9), so a
  frame is the same value whether or not anything went wrong producing it
  (design policy §14).
- **A frame is not a packet.** Frame assembly (§7) decides what one frame is.

## 4. `TrackerObservation`

A tracker source reports numbered positions and rotations; **a tracker index
is not a body role**. The mapping from one to the other is a solve that needs
the user's calibration, and a decoder that maps `tracker 1` onto `hips` has
invented a calibration and hidden it (measured: `usd-vrm-plugins`' OSC track
§5, report `motion/02`).

```cpp
struct TrackerObservation {
    std::string trackerId;          // as the source names it: "1", "head"
    GfVec3f position;               // canonical basis, metres
    GfQuatf rotation;               // canonical basis
    bool hasPosition, hasRotation;
    std::optional<float> confidence;
};
```

Three decisions sit between a tracker index and a joint, and each has one
owner:

| Decision | Owner | May know |
| --- | --- | --- |
| **Decode** — bytes to `TrackerObservation` | the connector | addresses, type tags, argument order; no body roles |
| **Assignment** — which tracker is which body region | a generic policy (`motionTracking`) | tracker count, rest geometry, an operator's explicit statement; never a protocol address literal |
| **Solve** — assigned observations to a sparse `MotionPose` | the motion layer | the shared joint vocabulary; never an avatar |

Explicit assignment by an operator is the default; automatic assignment is a
later aid over the same contract. Where assignment and solve live across the
repository boundary is `WS-O2`.

## 5. State

```cpp
enum class ConnectorState { Disconnected, Connecting, Connected, Degraded, Error };
```

| State | Meaning | `Poll` |
| --- | --- | --- |
| `Disconnected` | not opened, or closed | false |
| `Connecting` | opened; nothing received yet, or reconnecting | false |
| `Connected` | receiving frames at the expected shape | frames |
| `Degraded` | receiving, but frames are incomplete, late or low-confidence beyond the connector's stated thresholds | frames |
| `Error` | cannot receive (bind failed, device gone, permission revoked); recoverable by `Close` / `Open` or by the connector's own retry | false |

- **An external source going away never terminates the runtime** (design
  policy §13, Rule 6). A disconnect, a sender restart, a revoked browser
  permission and an ended XR session are transitions, each with a diagnostic.
- **A sender restart is detected, not assumed away**: a sequence or timestamp
  reset is a stated event of frame assembly (§7), measured in
  `usd-vrm-plugins` for both VMC and mocopi.
- `ConnectorState` is the connection's state, not the tracking's. Whether a
  frame can say *tracking lost for this joint* without using an absent joint or
  a low confidence is `usd-motion-plugins`' MC-O6, and mocopi's native stream
  is the first producer that reports it (`CC-O4`).

## 6. Time

```cpp
struct FrameTiming {
    std::optional<double> sourceTimestamp;   // seconds, in the source's clock
    double receiveTimestamp;                 // seconds, local monotonic clock
    std::optional<std::uint64_t> sequence;   // as the source numbers it
    ClockDomain sourceClock;                 // Device, LocalMonotonic, Wall, NetworkSynchronized, None
};
```

- **Two clocks, never mixed silently** (design policy §11). The source's
  clock is what the source said; the receive clock is this process's monotonic
  clock, read once, at receipt, by the transport. Timestamps from two machines
  are not comparable without an offset someone estimated.
- **`MotionPose::timestamp` is in the producer's clock**, as the motion
  contract's §4 and §9 require, so a recorded session replays byte for byte.
  A connector whose source has no clock stamps the pose with
  `receiveTimestamp` and says so with `ClockDomain::None` on `sourceClock`.
- **Timestamps into a stream strictly increase.** A connector that sees its
  source clock go backwards reports it (a restart or a fault) rather than
  passing it on; the stream's intake would refuse it anyway, but the connector
  is the only layer that can tell a restart from a fault.
- Clock-offset estimation, resampling and jitter buffers belong to the stream
  layer, not to a connector (design policy §11).

## 7. Frame assembly

Many protocols make no promise that one datagram is one frame. Each connector
therefore states and tests its frame-assembly policy rather than inheriting
whatever its receive loop happens to do (measured: `usd-vrm-plugins` adapter
plan §5.2). The policy covers, at least:

```text
packet arrival order      duplicate packet        sender restart
frame boundary            missing joint           sequence reset
late packet               stale joint value       source clock drift
body / expression synchronization                 root update rate
```

**Frame assembly is per connector and is not shared.** Three protocols with one
shared assembler would have one protocol's frame policy (measured:
`usd-vrm-plugins`' OSC track §3.4). What is shared is the transport and the
wire format ([WORKSPACE.md §1](../architecture/WORKSPACE.md#1-identities)).

## 8. Buffering, push and pull

```text
source callback / socket
        │
        ▼
bounded buffer (per connector)     ← drops are counted, never silent
        │
        ▼
Poll(MotionFrame&)                 ← the only public read
```

- **Bounded, with a declared capacity**, and a drop is a counted, reported
  event (design policy §12, §27; the motion contract's §9).
- **Modes:** `Latest` keeps the newest frame and discards the rest — the
  default for driving an avatar; `Ordered` delivers every frame in order until
  the buffer overflows — the default for capture; `Lossless` refuses to drop
  and reports back-pressure instead — for recording from a replayed capture.
- The core's `Poll` removes at most one frame. `Latest` replaces the pending
  frame and increments `droppedFrames` and `skippedFrames`; `Ordered` drops the
  oldest frame on overflow and increments the same counters; `Lossless` leaves
  the buffer unchanged and returns back-pressure from `Push`. The counters are
  cumulative buffer statistics, so a caller can report skipped frames without
  changing the `Poll(MotionFrame&)` shape.
- **Push is a wrapper over pull.** A callback source fills the buffer; `Poll`
  drains it. A runtime that wants push registers a callback that polls, so
  every connector has one read path to test (design policy §10).
- The connector buffer is **not** the stream. `usd-motion-plugins`' stream
  intake still applies its own ordering, confidence and bounds; the connector
  buffer exists so that a socket thread and a tick never share a frame.
  Whether the public `MotionStream` is pull, push or both is
  `usd-motion-plugins`' MC-O5, and it names this repository's first consumer
  as the evidence (`CC-O3`).

## 9. Diagnostics

A diagnostic is a **value** reported beside frames, never an exception thrown
into the runtime: a code, a severity from one table, a subject (a joint, an
address, a packet sequence, a byte offset), a detail sentence, and a
**recoverable** flag. The flag matters more here than anywhere: a session that
continues through a dropped packet is not reported like one that cannot
(measured: `usd-vrm-plugins` adapter plan §8). Tests assert the code and the
subject, never the prose.

Each connector owns its code namespace, and the transport and the wire format
own theirs; a decode failure and a motion-contract violation are different
namespaces. The catalog is [DIAGNOSTICS.md](../reference/DIAGNOSTICS.md).

## 10. Untrusted input

Everything a connector receives is untrusted (design policy §27):

- a declared maximum packet size, checked before parsing;
- parsers that validate lengths, type tags and counts before reading, and
  never deserialize into code;
- **non-finite floats and zero or non-finite quaternions are refused**, not
  normalized into a plausible pose (the motion contract's §5.2);
- bounded buffers (§8) and a rate limit per source where the transport allows
  a flood;
- **the default bind is loopback.** Listening on another interface is an
  explicit configuration value, and a connector never opens a port to a public
  network implicitly; TLS is optional per transport.

A malformed packet is a diagnostic and a dropped packet, never a crash, and
the generated corpus tests each refusal
([roadmap](../roadmap/README.md), design policy §35).

## 11. Actors

One connector is not one avatar (design policy §15). Every `ActorFrame` has an
`ActorId`, and a single-actor connector uses one stable id. Which actor drives
which avatar is the runtime's binding, never the connector's. The id type is
`CC-O5`.

## 12. Raw capture

A connector may record what it **received**, below decoding, for diagnosis and
for building a test corpus: `usd-vrm-plugins`' `liveTransport` packet-capture
format, which replays a session through the unchanged decoder. That is the
whole of recording here. Recording **canonical motion** — the
`motion-capture-trace` format, clips, USD — is `usd-motion-plugins`'
`motionRecording` (design policy §25), and this repository invents no motion
file format.

## 13. Open questions

| Id | Question | Resolve by |
| --- | --- | --- |
| CC-O1 | What design policy §5.1 asks for beyond `MotionPose` — string joint identifiers outside the shared vocabulary, per-joint translation and scale — and which source first needs it. Raised upstream as evidence for MC-O1 and MC-O2, never met with a local pose type | a source whose data does not fit `HumanJoint` version 1 (Connector Phase 4, MediaPipe, at the latest) |
| CC-O2 | Landmark sources: MediaPipe reports joint **positions**, not rotations. Is a landmark set an observation like a tracker (§4), solved downstream, or does the connector solve rotations itself? Design policy §26 says a connector emits "the best faithful normalized observation", which argues for the former | Connector Phase 4 |
| CC-O3 | The public `MotionStream` shape (`usd-motion-plugins` MC-O5): this repository's proposal is pull over a bounded buffer, push as a wrapper (§8) | the first connector consumed through `motionCore` |
| CC-O4 | Per-joint tracking loss (`usd-motion-plugins` MC-O6): mocopi's native stream reports it; VMC does not | v0.1.0 convergence |
| CC-O5 | `ActorId`: an integer, a string, or a source-scoped pair | the first multi-actor source |
| CC-O7 | A stable C ABI (design policy §38) over this interface, and when | the first non-C++ consumer of the native connectors (Python bindings, v0.2.0) |
| CC-O8 | The wire representation of `MotionFrame` for WebSocket and JS (design policy §37): JSON first for debuggability, with the ABI left open | v0.2.0 |
