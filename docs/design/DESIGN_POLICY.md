# motion-connectors — Design and Implementation Policy

> Status: **accepted** as the repository's design policy, 2026-09-19. This
> document defines intended boundaries; [reference/CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md)
> is the only document that states current implementation status.
> Repository: `animu-sphere/motion-connectors`  
> Scope: connectivity to external motion sources, and their normalization into the shared motion contract  
> Upstream: `usd-motion-plugins` (the motion values this repository produces)  
> Consumers: `usd-avatar-runtime`, and any application that wants live motion without an avatar format
>
> This is the canonical, long-form policy. Three focused documents own the
> detail of one area each, and **on their own area the focused document
> wins**: [CONNECTOR_CONTRACT.md](CONNECTOR_CONTRACT.md) (the connector
> interface, frames, state, time, buffering), [COORDINATE_SYSTEMS.md](COORDINATE_SYSTEMS.md)
> (source bases and their conversion) and [SOURCE_PROFILES.md](SOURCE_PROFILES.md)
> (profiles and joint naming). Component identities and dependency edges are
> [architecture/WORKSPACE.md](../architecture/WORKSPACE.md)'s. The motion values
> themselves — `MotionPose`, `RootMotion`, `MotionChannelSet`, `SourceMetadata`,
> `MotionStream` intake — are `usd-motion-plugins`' `MOTION_CONTRACT.md`'s, and
> this repository does not redefine them (§46.1).
>
> Section numbers are stable, so sibling repositories can cite them ("the
> connectors policy §42"). A revision adds subsections or appends sections, and
> never changes what a number means. §46 records how this policy was reconciled
> with the sibling repositories' contracts on adoption; where a sketch in §1–§45
> and §46 disagree, §46 is the decision and the sketch is the motivation.
> The accepted boundary clarification of 2026-10-06 is §47; where it narrows
> an earlier sketch or import decision, §47 governs the intended boundary.

---

## 1. Overview

`motion-connectors` is the integration layer that connects **external motion sources** to the OpenUSD-based avatar / motion runtime.

Its primary role is not to define motion representation itself, nor to own retargeting or avatar semantics.

Instead, `motion-connectors` should:

- connect to devices, browsers, SDKs, recorded transport captures, replay
  sources, streams and remote services;
- normalize their data into the shared, stage-independent motion contract;
- expose that data to `usd-motion-plugins`;
- avoid embedding VRM-, MMD-, or renderer-specific logic;
- remain usable independently from any single avatar format.

The intended architecture is:

```text
External World
    |
    |  devices / SDKs / Web APIs / network / sensors
    v
motion-connectors
    |
    |  MotionFrame (shared MotionPose, tracker observations)
    v
usd-motion-plugins
    |
    |  representation / retarget / recording / playback
    v
Avatar Semantics
    |
    +-- usd-vrm-plugins
    +-- usd-mmd-plugins
    +-- other avatar plugins
    |
    v
usd-avatar-runtime
```

The core principle is:

> `motion-connectors` owns connectivity and normalization.
> `usd-motion-plugins` owns motion semantics and transformation.
> Avatar plugins own avatar-format semantics.

---

## 2. Goals

### 2.1 Primary goals

`motion-connectors` should provide a common interface for motion sources such as:

- Sony mocopi
- MediaPipe
- WebXR
- OpenXR
- OSC
- VMC Protocol
- UDP / TCP / WebSocket streams
- recorded transport captures and replay sources
- camera-based body tracking
- hand tracking
- face tracking
- tracked spatial controllers and 6DoF controller poses
- IMU / sensor suits
- remote motion services

The same downstream runtime should be able to consume any connector without needing source-specific logic.

### 2.2 Non-goals

The repository should **not** become responsible for:

- USD FileFormat plugins;
- general motion-file parsing owned by `usd-motion-plugins`;
- skeleton retargeting;
- VRM humanoid semantics;
- MMD bone semantics;
- animation authoring UI;
- physics;
- rendering;
- avatar loading;
- scene graph ownership;
- permanent storage format design.

These belong in other layers.

---

## 3. Repository Boundary

A useful rule is:

> If the implementation talks to the outside world, it probably belongs in `motion-connectors`.

Examples:

| Concern | Repository |
|---|---|
| mocopi UDP / SDK integration | `motion-connectors` |
| MediaPipe browser integration | `motion-connectors` |
| WebXR joint acquisition | `motion-connectors` |
| OpenXR body / hand input | `motion-connectors` |
| VMC / OSC transport | `motion-connectors` |
| WebSocket motion transport | `motion-connectors` |
| MotionFrame envelope and connector contract | `motion-connectors` |
| MotionPose data contract | `usd-motion-plugins` `motionCore` |
| Skeleton retargeting | `usd-motion-plugins` |
| BVH representation | `usd-motion-plugins` |
| NPZ motion representation | `usd-motion-plugins` |
| VMD representation | `usd-motion-plugins` |
| VRM humanoid mapping | `usd-vrm-plugins` |
| PMX/MMD bone semantics | `usd-mmd-plugins` |
| Runtime composition | `usd-avatar-runtime` |

---

## 4. Architectural Principle

The connector should terminate at a **canonical motion boundary**.

The logical flow is:

```text
external source
    -> source acquisition / decode / assembly / normalization
    -> MotionFrame                         (connector library ends)
    -> downstream runtime intake adapter
    -> usd-motion-plugins MotionStream
```

`MotionFrame` is the connector boundary. `MotionPose` and `MotionStream` are
owned by `usd-motion-plugins`; this repository does not define a second pose or
stream type. The boundary is independent of stage authoring and avatar-format
semantics, even though `motionCore` may provide OpenUSD foundation value types.

USD stage authoring is integrated one layer later.

This avoids coupling device/network APIs to:

- `UsdStage`
- `UsdSkel`
- VRM schema
- MMD schema
- Hydra
- OpenExec

That separation is important for reuse in:

- native applications;
- browser/WASM environments;
- tests;
- recording tools;
- offline conversion tools;
- network services.

---

## 5. Canonical Data Model

### 5.1 MotionPose

`MotionPose` is the shared motion value owned by `usd-motion-plugins`
`motionCore`. This repository carries it inside `MotionFrame`; it does not
define a local `JointPose` or `MotionPose` substitute. The complete pose
semantics, vocabulary and stream intake rules are in
`usd-motion-plugins`' `MOTION_CONTRACT.md`.

The connector-side requirements are limited to declaring the source basis,
transform space, timestamp origin, confidence semantics and source profile, then
converting the observation before it enters `MotionFrame`. The focused
contracts own those details: [CONNECTOR_CONTRACT.md](CONNECTOR_CONTRACT.md),
[COORDINATE_SYSTEMS.md](COORDINATE_SYSTEMS.md) and
[SOURCE_PROFILES.md](SOURCE_PROFILES.md).

---

### 5.2 MotionStream

`MotionStream` and its intake semantics belong to `usd-motion-plugins`. A
connector exposes the connector-side pull operation over `MotionFrame`; the
stream decides how shared poses are admitted, ordered, resampled or consumed.
This repository does not prescribe a second stream API.

---

### 5.3 Additional Channels

Connectors may emit sparse shared poses for body, hands, eyes or root motion,
and namespaced expression channels through the shared motion contract. Tracker
observations remain beside the pose in `MotionFrame`; they are not body joints.
The focused connector contract owns the envelope and capability rules, while
`usd-motion-plugins` owns the motion value types.

---

## 6. Coordinate-System Policy

Coordinate normalization should happen before data enters the reusable motion layer.

Recommended canonical space:

```text
right-handed
Y-up
meters
quaternion rotation
```

This aligns well with the existing project convention:

```text
upAxis = Y
metersPerUnit = 1
```

Each connector must explicitly document its input space and conversion.

Example:

```text
MediaPipe space
      |
      | source adapter
      v
Canonical MotionPose
right-handed / Y-up / meters
```

Downstream systems should not need to know whether the pose came from MediaPipe, mocopi, or OpenXR.

---

## 7. Joint Naming

Raw source joint names should not leak deeply into the runtime.

Use two stages:

```text
source joint names
      |
      | connector normalization
      v
source-neutral canonical names
      |
      | usd-motion-plugins retargeting
      v
target avatar joints
```

Example canonical names:

```text
hips
spine
chest
upperChest
neck
head

leftShoulder
leftUpperArm
leftLowerArm
leftHand

rightShoulder
rightUpperArm
rightLowerArm
rightHand

leftUpperLeg
leftLowerLeg
leftFoot
leftToes

rightUpperLeg
rightLowerLeg
rightFoot
rightToes
```

However, this list should not be hard-coded as the only valid skeleton.

Use extensible string/token identifiers.

This keeps the system compatible with:

- VRM Humanoid;
- Mixamo-like rigs;
- MMD;
- mocopi;
- OpenXR;
- custom tracker rigs.

---

## 8. Source Profiles

Each connector may expose a **source profile**.

Examples:

```text
mediapipe.body.v1
mediapipe.hands.v1
mocopi.body.v1
openxr.hand.v1
vmc.v1
webxr.hand.v1
```

A profile describes:

- source joint set;
- hierarchy;
- coordinate convention;
- confidence semantics;
- optional capabilities.

This is preferable to embedding source behavior in retargeting code.

---

## 9. Connector Interface

A minimal common interface is recommended.

Conceptually:

```cpp
class IMotionConnector {
public:
    virtual Status Open(const ConnectorConfig&) = 0;
    virtual void Close() = 0;

    virtual ConnectorState GetState() const = 0;
    virtual ConnectorCapabilities GetCapabilities() const = 0;

    virtual bool Poll(MotionFrame&) = 0;
};
```

Optional capabilities may include:

```text
body
hands
face
blendshapes
root-motion
timestamps
confidence
multiple-actors
camera
controller
```

Avoid large inheritance hierarchies.

Prefer:

- small interfaces;
- composition;
- capability descriptors;
- plain data structures.

---

## 10. Push and Pull Models

Both should eventually be supported.

### Pull

```text
runtime
  |
  +--> connector.poll()
```

Useful for:

- OpenExec update loops;
- deterministic simulation;
- game loops;
- testing.

### Push

```text
connector
  |
  +--> callback(frame)
```

Useful for:

- WebSocket;
- OSC;
- device SDK callbacks;
- browser events.

Recommended internal architecture:

```text
source callback
      |
      v
bounded queue / ring buffer
      |
      v
canonical Poll()
```

This gives the runtime a predictable interface even when the source is asynchronous.

---

## 11. Timestamp Policy

Timestamp handling must be explicit.

A frame should preferably carry:

```text
sourceTimestamp
receiveTimestamp
sequenceNumber
```

Possible clock types:

```text
device clock
monotonic local clock
wall clock
network synchronized clock
```

Do not silently assume timestamps from different machines are directly comparable.

Future synchronization work may support:

- clock offset estimation;
- interpolation;
- resampling;
- jitter buffers;
- NTP/PTP-aware timestamps.

These mechanisms belong mostly in the stream/motion layer rather than individual source adapters.

---

## 12. Buffering and Real-Time Behavior

For live tracking, low latency is generally more important than preserving every frame.

Acquisition queue sketch (the concrete modes are
[CONNECTOR_CONTRACT §8](CONNECTOR_CONTRACT.md#8-buffering-push-and-pull)):

```text
producer
   |
   v
small bounded ring buffer
   |
   +--> counted queue replacement / overflow, by the selected delivery mode
   |
   v
Poll(MotionFrame&)
```

Modes may include:

```text
latest
ordered
lossless-recording
```

Recommended default for avatars:

```text
latest
```

Recommended default for capture / recording:

```text
ordered
```

These are caller-selected queue delivery modes, not a connector's semantic
stale/missing-pose policy. A connector reports source observations;
downstream intake decides hold, interpolation, semantic dropping and
continuity handling (§47.2).

---

## 13. Error Handling

Connectors must not terminate the whole runtime when an external source disappears.

Expected states:

```text
Disconnected
Connecting
Connected
Degraded
Error
```

Transient failures should be recoverable.

Examples:

- UDP source disappears;
- Bluetooth device disconnects;
- browser permissions change;
- WebSocket reconnects;
- XR session ends;
- tracking quality drops.

A connector should report state rather than throwing unrecoverable errors into the avatar runtime.

---

## 14. Runtime Metadata

Useful metadata includes:

```text
source
device
actor
profile
frame number
timestamp
tracking confidence
latency
version
```

Example:

```json
{
  "source": "mediapipe",
  "profile": "mediapipe.body.v1",
  "actor": "0",
  "sequence": 10234
}
```

Keep diagnostic metadata separate from core transform data where possible.

---

## 15. Multi-Actor Support

Do not assume one connector equals one avatar.

The canonical API should be able to represent:

```text
connector
   |
   +-- actor A
   +-- actor B
   +-- actor C
```

Possible key:

```text
ActorId
```

This matters for:

- camera-based tracking;
- network streaming;
- multi-user XR;
- mocap studios.

Initial implementations may support one actor, but the core API should avoid preventing multi-actor support.

---

## 16. Recommended Connector Modules

The repository structure follows the sibling workspaces and the binding layout
in [WORKSPACE.md](../architecture/WORKSPACE.md):

```text
motion-connectors/
├─ CMakeLists.txt
├─ README.md
├─ LICENSE
├─ docs/
│  ├─ design/
│  ├─ architecture/
│  ├─ reference/
│  └─ roadmap/
│
├─ libs/
│  ├─ motionConnectorCore/       reserved until the shared contract lands
│  ├─ motionConnectorTransport/
│  ├─ motionConnectorOsc/
│  ├─ motionConnectorTracking/
│  ├─ motionConnectorVmc/
│  ├─ motionConnectorMocopi/
│  └─ motionConnectorVrchatOsc/
│
├─ tools/
│  ├─ motionConnect/             reserved unified CLI
│  ├─ vmcRecord/
│  ├─ mocopiRecord/
│  └─ vrchatOscRecord/
│
├─ bindings/
│  ├─ python/
│  └─ js/
│
├─ examples/
│  ├─ dump_pose/
│  ├─ record_stream/
│  └─ usd_avatar_live/
│
└─ tests/
```

Web acquisition modules and language bindings have separate layout ownership;
they are not part of the native build by accident. Not every connector has to
be compiled into every runtime.

They should be optional modules.

---

## 17. Native and Web Separation

Native and browser integrations have very different dependency requirements.

Recommended conceptual grouping:

```text
motionConnectorCore
|
+-- native
|   +-- OpenXR
|   +-- mocopi
|   +-- OSC
|   +-- VMC
|   +-- UDP
|
+-- web
    +-- WebXR
    +-- MediaPipe
    +-- WebSocket
```

Do not force:

```text
OpenXR dependencies
MediaPipe dependencies
browser APIs
device SDKs
```

into the common core.

---

## 18. Web / WASM Strategy

Web support is particularly valuable for this repository.

A likely architecture:

```text
Browser APIs
    |
    +-- WebXR
    +-- MediaPipe
    +-- WebSocket
    |
    v
JavaScript connector
    |
    v
MotionFrame
    |
    +--> JS runtime
    |
    +--> WASM bridge
             |
             v
       usd-motion runtime
```

Do not require WebXR / MediaPipe to be implemented directly in C++.

For browser-native APIs, JavaScript / TypeScript adapters are often the natural integration layer.

Shared JS/TS consumer API sketch (not the acquisition provider interface):

```ts
const connector = await openConnector({
  type: "mediapipe"
});

for await (const frame of connector.frames()) {
  // MotionFrame
}
```

---

## 19. Python API

Python consumes the acquisition C ABI through a small, ownership-safe API,
not the native C++ class tree. Binding implementation and CPython `abi3`
packaging are separate choices; the public API must not depend on either.
Core surfaces are open/close/poll, frame iteration, capabilities, state,
diagnostics and source profile access.

Example:

```python
from motion_connectors import open_connector

connector = open_connector("vmc", port=39539)

for frame in connector.frames():
    # consume acquisition observations
    ...
```

Potential applications:

- debugging;
- recording;
- dataset generation;
- tests;
- ML pipelines;
- rapid prototyping.

---

## 20. Relationship with `usd-motion-plugins`

This boundary is critical.

`motion-connectors` produces and carries:

```text
MotionFrame
  ├─ shared MotionPose values from motionCore
  └─ TrackerObservation values where applicable
```

`usd-motion-plugins` consumes the frame and owns:

```text
MotionPose and MotionStream intake
retarget
resample
filter
semantic recording
playback
motion-file integration
USD animation bridging
```

Therefore:

```text
motion-connectors
    does NOT know target avatar

usd-motion-plugins
    does NOT know device transport
```

This split allows:

```text
mocopi -> VRM
mocopi -> MMD
MediaPipe -> VRM
MediaPipe -> MMD
WebXR -> custom USD skeleton
VMC -> arbitrary UsdSkel
```

without source-specific target implementations.

---

## 21. Relationship with `usd-vrm-plugins`

`usd-vrm-plugins` should provide VRM-side semantics such as:

```text
VRM humanoid joint mapping
VRM expressions
VRM spring-bone semantics
VRM metadata
```

A MediaPipe connector should therefore **not** directly implement:

```text
MediaPipe -> VRM bone mapping
```

Instead:

```text
MediaPipe
    |
motion-connectors
    |
  MotionFrame
    |
usd-motion-plugins
    |
retarget
    |
usd-vrm-plugins
```

This avoids duplicated retargeting implementations.

---

## 22. Relationship with `usd-mmd-plugins`

The same rule applies to MMD.

Avoid:

```text
mocopi -> PMX
MediaPipe -> PMX
WebXR -> PMX
```

direct implementations.

Use:

```text
source
  |
motion-connectors
  |
MotionPose
  |
usd-motion-plugins
  |
MMD retarget profile
  |
usd-mmd-plugins
```

VMD itself remains a motion representation concern and should live with the motion plugins rather than device connectors.

---

## 23. Relationship with OpenExec / Stage Runner

`motion-connectors` should work especially well with frame/update-loop systems.

Example:

```text
OpenExec / stage runner tick
         |
         v
connector.poll()
         |
         v
MotionPose
         |
         v
retarget
         |
         v
UsdSkel / avatar state update
```

The connector should not own the simulation/update loop.

This enables use from:

- OpenExec;
- `usd-stage-runner`;
- standalone apps;
- tests;
- web loops.

---

## 24. USD Integration Policy

Avoid making `UsdStage` the connector API.

Bad:

```cpp
connector.UpdateUsdStage(stage);
```

Preferred:

```cpp
connector.Poll(frame);
motionSemantics.Apply(frame, target);
```

USD is the scene/representation layer, not the external-device transport abstraction.

This keeps connectors usable outside USD and makes testing significantly simpler.

---

## 25. Recording

Recording is useful, but the connector repository should not invent another permanent animation format.

Recommended pipeline:

```text
connector
    |
raw packet/session capture or MotionFrame diagnostic capture
  |
usd-motion-plugins intake
    |
usd-motion-plugins recorder
    |
    +-- USD animation
    +-- NPZ
    +-- BVH
    +-- other supported formats
```

A connector may provide a raw diagnostic capture facility, but canonical recording belongs downstream.

---

## 26. Filtering and Smoothing

Generic tracking filters and smoothing belong in `usd-motion-plugins`;
connector libraries do not own them.

Examples:

```text
One Euro Filter
Kalman filter
EMA
joint confidence filtering
foot stabilization
IK correction
pose interpolation
```

These are not implemented in connector libraries, including private helpers.

The connector should emit the best faithful normalized observation it can.

Then:

```text
raw normalized pose
       |
       v
usd-motion-plugins filters
       |
       v
retargeted pose
```

---

## 27. Security

Network connectors must treat incoming motion data as untrusted.

Important requirements:

- packet size limits;
- bounded queues;
- parser validation;
- rate limits;
- invalid float rejection;
- sequence validation where useful;
- explicit remote binding configuration;
- avoid unsafe deserialization;
- optional TLS for network transports.

WebSocket and remote-stream integrations should not implicitly expose listening ports to public networks.

---

## 28. Dependency Policy

Keep `motionConnectorCore` very small.

A connector library may depend on `motionCore`, but must not depend on
`motionSampling`, `motionRecording`, `motionRetarget`, or `motionUsd`.
Tools, examples and runtime integration may consume those downstream
libraries; they must not introduce a reverse dependency from a connector.
The imported live-source composition is transitional, not an exception to
the target contract ([§47](#47-external-acquisition-boundary)).

`motionConnectorCore` may consume the foundation value types exposed by
`usd-motion-plugins` `motionCore` (`gf`, `tf`, `vt`), but it must not open or
mutate a stage. It should add only the connector contract, small buffering and
minimal threading primitives.

Connector-specific dependencies should remain isolated.

Examples:

```text
OpenXR -> OpenXR loader
OSC -> small OSC library
WebSocket -> optional transport library
MediaPipe -> browser/package dependency
mocopi -> protocol-specific module
```

A user requiring only VMC should not need to build OpenXR or MediaPipe.

---

## 29. Plugin / Adapter Model

A plugin-style connector registry may be useful.

Conceptual API:

```text
openConnector("vmc")
openConnector("mocopi")
openConnector("openxr")
openConnector("mediapipe")
```

Internally:

```text
ConnectorRegistry
    |
    +-- vmc
    +-- osc
    +-- mocopi
    +-- openxr
```

However, do not introduce dynamic plugin loading until the static module layout becomes restrictive.

Start simple.

---

## 30. Recommended Initial Implementation Order

Implementation order is a design constraint, not a second status or release
table. The current incomplete order is maintained in
[roadmap/current.md](../roadmap/current.md). The constraints are:

1. Fix the shared connector contract before adding another source.
2. Adapt the imported VMC, mocopi and VRChat OSC implementations without
   replacing their protocol-specific assembly or replay evidence.
3. Add transports, bindings, browser and XR sources only through the
   same `MotionFrame` boundary.

No source-specific implementation may define the shared core by accident.

---

## 31. Suggested First Release Scope

Release scope is owned by [roadmap/README.md](../roadmap/README.md). The
original recommendation for v0.1.0 was deliberately small; after the imports,
the accepted scope is shared-contract convergence around those existing
sources. This policy defines the boundary, not the checklist.

---

## 32. Suggested `v0.2.x`

Release ordering is maintained in the roadmap. WebSocket transport,
capture/bridge tooling and language bindings must continue to carry
`MotionFrame`; they do not move semantic recording or retargeting upstream.

---

## 33. Suggested `v0.3.x`

Release scope is owned by [the roadmap](../roadmap/README.md). Browser
acquisition providers own observations and normalization; a shared JS/TS
consumer API and a WASM data ABI are separate consumer contracts.

---

## 34. Suggested `v0.4.x`

Release scope is owned by [the roadmap](../roadmap/README.md). Integration
examples compose acquisition with downstream motion packages without adding
those dependencies to connector libraries.

---

## 35. Testing Strategy

Connector testing should be deterministic where possible.

Use recorded test streams.

Example:

```text
tests/data/
├─ vmc/
│  └─ basic-motion.capture
├─ websocket/
│  └─ body-pose.jsonl
└─ mediapipe/
   └─ pose-sequence.json
```

Tests should validate:

- parsing;
- coordinate transforms;
- unit transforms;
- joint naming;
- timestamps;
- malformed packets;
- disconnect/reconnect;
- dropped frames.

Hardware should not be required for normal CI.

---

## 36. Debugging Tools

The CLI contract is owned by [MOTION_CONNECT.md](MOTION_CONNECT.md): source
selection, bridge forwarding, raw recording and local diagnostics. Examples
and options live there rather than in a second command inventory here.
Semantic motion recording stays in `usd-motion-plugins`; a bridge forwards
acquisition frames without retargeting or avatar application. Current command
conformance and release availability belong to the
[capability matrix](../reference/CAPABILITY_MATRIX.md#3-tools-and-bindings).

---

## 37. Interchange over Network

[FRAME_WIRE_FORMAT.md](FRAME_WIRE_FORMAT.md) owns the serialized `MotionFrame`
contract (CC-O8): UTF-8 JSON, identified as `openstrata.motion.frame/v1`.
Debuggability and browser interoperability motivate this choice. A binary
encoding requires measured size/parse-cost evidence under FW-O1, rather than
another independently maintained format. Network serialization is distinct
from the in-process ABI (§38) and downstream semantic recording.

---

## 38. ABI Considerations

The language-neutral boundary is a minimal **acquisition C ABI** over
`MotionFrame`, not a full mirror of the C++ interface. CC-O7 owns the concrete
header/package and view decisions ([CONNECTOR §13](CONNECTOR_CONTRACT.md#13-open-questions));
release ordering belongs to the roadmap.

Its invariants are POD values and opaque handles, versioned structs, stable
integer widths, explicit ownership and lifetime, UTF-8 strings, explicit
nullable representation and errors that cannot throw across the ABI. No STL
or USD type crosses it. Views must specify frame/diagnostic validity and how
joint/tracker iteration, timestamps, actors, capabilities, state and source
profiles are accessed. Retargeting, filtering, runtime loops, USD bridges and
avatar APIs are downstream responsibilities.

Conceptual shape only; names and signatures remain a CC-O7 decision:

```c
motion_connector_t* motion_connector_open(...);
void motion_connector_close(...);
motion_poll_result_t motion_connector_poll(...);
motion_frame_view_t motion_frame_view(...);
motion_diagnostic_view_t motion_connector_last_diagnostic(...);
```

Bindings may layer Python, JavaScript/WASM, Rust or C# over this boundary.
Keep three representations distinct: the in-process ABI, JS object values and
serialized wire messages. WASM ownership/copy rules, 64-bit integers and string
lifetimes need a consumer contract; neither native structs nor wire JSON can
be assumed to provide it automatically.

---

## 39. Naming

Recommended repository name:

```text
motion-connectors
```

rather than:

```text
usd-motion-connectors
```

if the connector layer itself remains USD-independent.

This has an architectural advantage:

```text
motion-connectors
      |
      v
usd-motion-plugins
```

The absence of `usd-` clearly communicates that external-device connectivity is not an OpenUSD-specific concept.

If organizational consistency requires the prefix, `usd-motion-connectors` is still possible, but technically the USD-independent name is cleaner.

---

## 40. Runtime Composition

`usd-avatar-runtime` should compose the pieces rather than merge their responsibilities.

Example runtime:

```text
usd-avatar-runtime
|
+-- OpenUSD
+-- usd-motion-plugins
+-- usd-vrm-plugins
+-- usd-mmd-plugins
+-- motion-connectors
+-- usd-stage-runner / OpenExec integration
```

A runtime profile might enable only required connectors:

```text
avatar-runtime-native
avatar-runtime-xr
avatar-runtime-web
avatar-runtime-mocap
```

This fits well with `ost runtime composition`.

---

## 41. OST Integration

The repository should be designed as an independently composable workspace.

Recommended properties:

- connector modules separately selectable;
- no mandatory heavyweight SDK dependencies;
- clear package feature flags;
- reproducible builds;
- native and web profiles;
- tests that do not require physical devices.

Conceptually:

```text
ost compose
  + motionConnectorCore
  + motionConnectorVmc
  + usd-motion-plugins
  + usd-vrm-plugins
```

or:

```text
ost compose
  + motion-connectors-web
  + usd-motion-wasm
  + usd-vrm-wasm
```

---

## 42. Design Rules

The following rules should guide implementation.

### Rule 1

**Connectors describe observations, not avatar intent.**

### Rule 2

**Source-specific coordinate systems end at the connector boundary.**

### Rule 3

**Retargeting belongs in `usd-motion-plugins`.**

### Rule 4

**VRM/MMD semantics remain in their respective repositories.**

### Rule 5

**No connector should require a `UsdStage`.**

### Rule 6

**Live streams should tolerate disconnects and frame loss.**

### Rule 7

**Hardware-independent CI is mandatory for the core.**

### Rule 8

**Web and native connectors may use different implementation languages.**

### Rule 9

**The canonical motion contract must stay smaller than any individual SDK.**

### Rule 10

**Do not prematurely turn `motion-connectors` into a full motion framework.**

---

## 43. Recommended Architecture Summary

The recommended final shape is:

```text
                        External World
                              |
          +-------------------+-------------------+
          |                   |                   |
        mocopi            MediaPipe            WebXR
          |                   |                   |
          +-------------------+-------------------+
                              |
                       motion-connectors
                              |
                        MotionFrame
                              |
                       usd-motion-plugins
                      MotionPose / MotionStream intake
              +---------------+---------------+
              |               |               |
           filter          retarget          record
              |               |               |
              +---------------+---------------+
                              |
                      Avatar Semantics
                 +------------+------------+
                 |                         |
          usd-vrm-plugins            usd-mmd-plugins
                 |                         |
                 +------------+------------+
                              |
                      usd-avatar-runtime
                              |
                     OpenExec / Stage Runner
                              |
                           UsdSkel
```

---

## 44. Immediate Next Steps

The current incomplete tasks belong to
[roadmap/current.md](../roadmap/current.md), with the boundary implementation
sequence and completion criteria in
[roadmap/boundary-implementation.md](../roadmap/boundary-implementation.md).
§47 fixes the acquisition boundary that those tasks must preserve.

The most important architectural decision is to stabilize the **MotionFrame /
MotionPose intake boundary** before adding more source integrations.

Once that boundary is stable, new sources become relatively inexpensive adapters rather than new motion systems.

---

## 45. Final Direction

`motion-connectors` should remain a deliberately thin **external-world integration layer**.

Its job is:

```text
connect
normalize
timestamp
identify
deliver MotionFrame
```

Its job is not:

```text
retarget
author USD
interpret VRM
interpret MMD
render
simulate
```

That separation gives the avatar stack a clean and scalable architecture:

```text
External Source
      ↓
motion-connectors
      ↓
MotionFrame
      ↓
usd-motion-plugins
  ↓
MotionPose / MotionStream intake
      ↓
VRM / MMD / UsdSkel
      ↓
usd-avatar-runtime
```

This should be the architectural contract for the repository going forward.

---

## 46. Reconciliation with the sibling contracts

Taken on 2026-09-19, when this policy was adopted. `usd-motion-plugins`,
`usd-vrm-plugins` and `usd-mmd-plugins` had already fixed several things this
policy sketches, and `usd-vrm-plugins` holds working, measured code that is
destined here. Each decision below is recorded where it is binding; this
section is the index. Where a decision narrows a sketch above, the sketch
stays as the motivation and this section is the rule.

### 46.1 The pose is `usd-motion-plugins`' `MotionPose`

§3's table leaves the `MotionPose` contract "preferably `usd-motion-plugins`
or a tiny dependency-neutral core". It is `usd-motion-plugins`: its
`MOTION_CONTRACT.md` defines `MotionPose`, `RootMotion`, `MotionChannelSet`,
`SourceMetadata` and the `MotionStream` intake rules. A source in
`motion-connectors` delivers those shared values in a `MotionFrame`, and
its workspace contract records the edge `motion-connectors →
usd-motion-plugins`: connector libraries consume `motionCore`; semantic
recording dependencies belong to tools or downstream integration (§47).

So §5.1's `JointPose` / `MotionPose` sketch is **not a second pose type**. What
this repository defines is what lies around a pose: the connector interface,
the `MotionFrame` envelope, state, capabilities, source profiles, receive-side
time and buffering ([CONNECTOR_CONTRACT.md](CONNECTOR_CONTRACT.md)). Where §5.1
asks for more than `MotionPose` carries — string joint identifiers beyond the
humanoid vocabulary, per-joint translation and scale — the need is raised
upstream as evidence against `MC-O1` and `MC-O2`, not met by a local type
(`CC-O1`).

§28's "C++ standard library and a small math abstraction" therefore describes
what `motionConnectorCore` adds, not its whole closure: `motion-core` brings
OpenUSD's foundation value types (`gf`, `tf`, `vt`) and nothing that opens a
stage, so Rule 5 holds. WS-O4 retains that native closure and separates
browser acquisition via wire values ([WORKSPACE §1.2](../architecture/WORKSPACE.md#12-web-modules)).
The native WASM data ABI is a distinct consumer contract.

### 46.2 The canonical basis includes a forward axis

§6 names handedness, up axis and units. The forward axis is **+Z**, as
`usd-motion-plugins`' policy §42.5 records from `usd-vrm-plugins`'
measurements: the VMC adapter converts a left-handed, +Y-up, +Z-forward sender
by flipping X alone. Canonical is right-handed, +Y up, +Z forward, metres,
seconds, unit quaternions ([COORDINATE_SYSTEMS.md §1](COORDINATE_SYSTEMS.md#1-the-canonical-basis)).

### 46.3 The first connectors are imported from `usd-vrm-plugins`, not rewritten

`usd-vrm-plugins` built, tested and measured three live inputs and their
shared leaves: `liveTransport` (UDP receiver, packet capture, the diagnostic
vehicle), `osc` (the OSC 1.0 wire format), `vrmAdapterVmc`,
`vrmAdapterMocopi`, `vrmAdapterVrchatOsc`, their record tools, and
`motionTracking` (tracker regions, assignment, the tracker solve). Its
workspace contract §9.1 gives every one of them this repository as its
destination, under its moving rules §9.2 — history comes with the code, one
identity per move, the reverse edge refused — in its migration milestone
MIG-4, which serves **Migration Phase E**.

They were renamed on arrival and lost the `vrm` prefix
([WORKSPACE.md §3](../architecture/WORKSPACE.md#3-moving-code-in)). They
were imported without replacing their source-specific assembly. Import and
shared-interface adaptation are separate changes, preserving protocol replay
evidence. Their history is in the changelog; conformance is in the
[capability matrix](../reference/CAPABILITY_MATRIX.md).

### 46.4 A tracker observation is not a pose

§5.3 lists `tracker.*` among the channels. A tracker is not a channel of a
pose: `usd-vrm-plugins` measured that a tracker index is an index into
whatever the user calibrated, and `usd-motion-plugins`' motion contract §11
gives a tracker source "its own observation type, then a solve that produces a
sparse `MotionPose`". A `MotionFrame` therefore carries tracker observations
beside poses, never inside them, and the three decisions between a tracker
index and a joint — decode, assignment, solve — stay separate
([CONNECTOR_CONTRACT.md §4](CONNECTOR_CONTRACT.md#4-trackerobservation)).

### 46.5 Face and hands, as the shared vocabulary already has them

§5.3 sketches `BodyPose`, `HandPose[]` and `FacePose` as separate parts of a
frame. The shared joint vocabulary (`HumanJoint` version 1, 55 joints) already
contains three joints per finger, and face data is expression weights, which
`MotionChannelSet` carries with namespaced names (`arkit:jawOpen`). So hands
travel in the `MotionPose` and faces in its channels; a connector that only
sees hands emits a sparse pose. §5.3's partitions survive as **capabilities**
a connector declares (§9), not as types
([CONNECTOR_CONTRACT.md §3](CONNECTOR_CONTRACT.md#3-motionframe)).

### 46.6 Phases are always qualified

When implementation phases are named, they are qualified by their owner. The
roadmap owns the current delivery order; sibling repositories' migration and
motion phases are cited only when describing an import or dependency.

### 46.7 The release order follows the imports

§31–§34 originally put mocopi in v0.4.x. The mocopi and VRChat OSC connectors and
`motionTracking` are imports of measured code, not new designs, and
`usd-vrm-plugins` could not finish its migration (MIG-5) while they waited;
each remained frozen there until its move. They were first scheduled for v0.2.0,
after v0.1.0 had fixed the contract. On 2026-09-19 WS-O7 moved them into
v0.1.0, beside VMC: `liveTransport` and `osc` are linked by all three
connectors, so importing them ahead of mocopi and VRChat OSC would have left
`usd-vrm-plugins` either two copies for a release or an undeclared edge to
this repository. The contract documents came first, but
import and adaptation of `motionConnectorCore` are separate changes
([WORKSPACE.md §3](../architecture/WORKSPACE.md#3-moving-code-in)).
This history does not define current capability status.
So §30's concern, that mocopi should not define the core API, still holds. The
current release mapping is in the [roadmap status table](../roadmap/README.md#status-at-a-glance),
which is the single source of truth for incomplete delivery scope.

### 46.8 §16's documentation files take the sibling layout

§16 lists `docs/architecture.md`, `motion-contract.md`,
`coordinate-systems.md` and `connectors.md`. The documentation takes the
layout `usd-motion-plugins`, `usd-vrm-plugins` and `usd-mmd-plugins` share,
and those four live at:

| §16 | Here |
| --- | --- |
| `architecture.md` | [architecture/WORKSPACE.md](../architecture/WORKSPACE.md), [architecture/DEPENDENCIES.md](../architecture/DEPENDENCIES.md) |
| `motion-contract.md` | [design/CONNECTOR_CONTRACT.md](CONNECTOR_CONTRACT.md) — the connector side only (§46.1) |
| `coordinate-systems.md` | [design/COORDINATE_SYSTEMS.md](COORDINATE_SYSTEMS.md) |
| `connectors.md` | [design/SOURCE_PROFILES.md](SOURCE_PROFILES.md) for what each source is; [reference/CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md) for what is implemented |

### 46.9 Names and layout are the siblings'

§16 puts the native connectors under `src/motionConnector*`. They take the
layout `usd-vrm-plugins`, `usd-mmd-plugins` and `usd-motion-plugins` share
instead (WS-O1, decided 2026-09-19):

- `libs/motionConnectorVmc/`, not `src/motionConnectorVmc/`;
- a lower-camel identity that is also the CMake package and the exported
  target (`motionConnectorVmc::motionConnectorVmc`);
- snake_case CLI commands (`motion_connect`, `vmc_record`);
- the C++ namespace `openstrata::connectors`.

WS-O6 places acquisition providers in independent `web/<identity>/` npm
modules and reserves `bindings/js/` for the shared consumer API. Layout is
owned by [WORKSPACE §1.1–§1.3](../architecture/WORKSPACE.md#12-web-modules).

---

## 47. External acquisition boundary

Accepted on 2026-10-06. This section fixes `motion-connectors` as the
**external acquisition edge**. It defines ownership; current component
structure is in [WORKSPACE §2.1](../architecture/WORKSPACE.md#21-inside-the-repository),
and acquisition conformance/test evidence is in the
[capability matrix](../reference/CAPABILITY_MATRIX.md). Remaining work belongs
to [the roadmap](../roadmap/current.md).

> `motion-connectors` owns what was observed.
> `usd-motion-plugins` owns how that motion is treated.

### 47.1 Acquisition ownership and the stopping point

Connectors own connectivity (UDP, WebSocket, browser APIs and native/device
SDKs), source/protocol decode, frame assembly, source sequences, source and
receive clocks, restart/session detection, capabilities, connection state,
tracker observations and protocol diagnostics. VMC, mocopi, VRChat OSC,
OpenXR, WebXR and MediaPipe follow the same boundary, as do future IMU suits,
optical mocap, depth cameras and dedicated tracking devices.

```text
device / browser / SDK / protocol / network
    → decode
    → frame assembly
    → source coordinate / unit / joint-name normalization
    → MotionFrame
    → END of connector library
```

`MotionFrame` is an acquisition envelope. It carries optional shared
`MotionPose` values and observations, with timing, sequence and provenance;
connection state and diagnostics are reported alongside it under
[CONNECTOR_CONTRACT §3, §5, §6 and §9](CONNECTOR_CONTRACT.md#3-motionframe).
This does not add fields to the current frame or wire format.
`MotionPose` stays owned by
[`usd-motion-plugins`' motion contract](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/MOTION_CONTRACT.md).
A connector may construct it through `motionCore`, without deciding how it
is sampled, filtered, recorded or retargeted.

Source-native basis and units, and protocol joint names mapped to
`HumanJoint` where appropriate, are source interpretation. Their conversion
to right-handed, +Y up, +Z forward, metres and seconds remains here and is
performed once ([COORDINATE_SYSTEMS.md](COORDINATE_SYSTEMS.md)). Generic motion
transformation, target-skeleton mapping and rest-pose correction stay downstream.

### 47.2 Observations and temporal interpretation

> A connector emits observations. It does not own their temporal interpretation.
>
> A connector reports discontinuity. It does not invent continuity.

Report observed restarts, missing/stale/duplicate input and sender-clock
resets with source evidence. Do not turn those facts into hold,
interpolation, smoothing, semantic dropping, bind/unbind, continuity repair
or a semantic reset policy. Malformed-input refusal, source-specific frame
assembly and bounded acquisition queues still belong here; they are not a
`MotionStream` intake or semantic buffer.

Interpolation, smoothing, generic filtering, semantic buffering, intake
policy, semantic recording, `motion-capture-trace` implementation and generic
retargeting belong to `usd-motion-plugins`. Connectors own no UsdStage/UsdSkel
authoring, OpenExec motion evaluation, VRM/MMD semantics, physics or runtime
scheduling. These non-goals also exclude target-skeleton logic and a
connector-owned `MotionPose` type.

### 47.3 Library responsibilities and dependency invariants

| Layer | Owns | Must not own |
| --- | --- | --- |
| `motionConnectorCore` | `IMotionConnector`, `MotionFrame`, `TrackerObservation`, state, capabilities, timing, bounded acquisition queue if needed | filtering, sampling, recording, retargeting, USD, OpenExec, protocols or device SDKs |
| `motionConnectorTransport` | UDP receive, datagram queues, packet capture, transport diagnostics | VMC addresses, OSC semantics, `MotionFrame` semantics or motion policy |
| `motionConnectorOsc` | packets, bundles, type tags, arguments | VMC/VRChat addresses, socket ownership or motion semantics |
| `motionConnectorWire`, `motionConnectorWebSocket` | `MotionFrame` transport and `openstrata.motion.frame/v1` | canonical semantic motion recording |
| source adapters | protocol/SDK interpretation and normalized observations | downstream motion treatment or runtime composition |

The required invariants are:

1. A connector library may depend on `motionCore`, but must not depend on
   `motionSampling`, `motionRecording`, `motionRetarget`, or `motionUsd`.
2. A connector library must not open or author a `UsdStage`.
3. A connector reports source observations; temporal interpretation belongs
   downstream.
4. Protocol-specific code never enters `motionConnectorCore`.
5. Transport libraries never know protocol semantics.

Connector leaves (`motionConnectorCore`, `motionConnectorTransport`,
`motionConnectorOsc`, `motionConnectorWire`) and isolated OS/device/browser
dependencies are allowed according to
[WORKSPACE §2](../architecture/WORKSPACE.md#2-dependency-directions).

### 47.4 Capture and recorder integration

Raw datagram/packet/protocol/session capture, source-specific replay and
diagnostic capture remain here. Semantic `MotionPose` stream recording,
canonical traces, clips and semantic replay belong to `usd-motion-plugins`.
Do not duplicate its serializer or trace semantics here.

`vmc_record`, `mocopi_record` and `vrchat_osc_record` remain in this
repository for source-specific options, session provenance and raw capture.
Semantic export such as `--export-trace` is CLI integration:

```text
record tool → connector library
record tool → motionRecording       (semantic export only)
```

There must be no `connector library → motionRecording` edge. Acquisition
frame sources own decode, assembly, restart detection and source diagnostics.
`IMotionSource`, `LiveCaptureSource`, semantic buffers and sampling policy are
consumer composition. Runtime intake belongs downstream, with
`usd-avatar-runtime` the first choice; test-only intake composition does not
become an installed connector interface.

### 47.5 Tracking and placement decisions

Keep `TrackerObservation` and the existing `motionConnectorTracking` for
now; the 2026-09-24 import decision is not a permanent placement rule for
generic solve. Tracking-space normalization, source tracker IDs, raw
observations, availability and source hints stay here.

Reassess anatomical solve, pose reconstruction, confidence fusion and
multi-tracker motion synthesis for `usd-motion-plugins`
when the algorithm works without a source/device name, is reusable across
connectors and generically produces `MotionPose`. Any move must avoid the
reverse dependency and duplicate observation contracts noted in §46.4.
The ownership review in §47.5.1 settles assignment versus semantic generation
and retains the direct solve until its downstream type-contract prerequisites.

For each new feature, apply these placement rules:

- Protocol/SDK interpretation or source-native basis interpretation:
  `motion-connectors`.
- Generic motion logic meaningful without a network/device, including
  filtering, interpolation, retargeting and semantic recording:
  `usd-motion-plugins`.
- Connector-to-motion-runtime orchestration: `usd-avatar-runtime`.

OpenXR illustrates this boundary: loader acquisition of
head/controllers/hands/body observations, source normalization, then
`MotionFrame`, with no filter, retargeter, recorder, stage or avatar semantics.
WebXR and MediaPipe follow the same contract; generic body reconstruction
must not be fixed inside a connector.

### 47.5.1 Tracking ownership review

**Accepted, 2026-10-08.** The deciding distinction is observation organization
versus generating motion semantics. Operator assignment and its applicability
validation stay in `motionConnectorTracking`: they map opaque observation
identities to connector-owned `TrackerRegion` values, and consume no geometry,
joint vocabulary or pose. A region remains a placement hint, never a joint alias.

The direct `SolveTrackerPose` is motion-semantic generation. Its region-to-joint
choice, ancestor composition, sparse local rotations and hips/root authoring
operate without source/device names and belong in `usd-motion-plugins`.
Anatomical reconstruction, confidence fusion and multi-tracker semantic solve
share that owner. This ownership decision does not introduce a library edge or
move code: the existing direct solve remains a compatibility path until the
motion-owned input and component contract, validation/comparison/recording
obligations and parity fixtures are adopted there.

`MotionFrame` and its `core::TrackerObservation` stay connector-owned.
`TrackerRegion` and assignment stay here too. The legacy
`tracking::TrackerObservation` is only a projection for the direct solve, not
a shared acquisition type to move or copy. A downstream input must describe
motion semantics, without tracker IDs, regions or connector types; integration
maps an assignment into that input by value. Neither repository aliases or
copies the other's contract, and downstream libraries never include connectors.

OpenXR/WebXR viewer, controller, hand and body hints identify source observations,
not parent-local humanoid rotations. MediaPipe body/hand landmarks can be
position-only and require reconstruction that the direct orientation solve
cannot provide. VRChat OSC observations support the existing direct solve only
with explicit operator assignment. None authorizes a source-specific body solve
inside acquisition. The remaining motion-side contract and migration are scoped
in [the boundary roadmap](../roadmap/boundary-implementation.md).

