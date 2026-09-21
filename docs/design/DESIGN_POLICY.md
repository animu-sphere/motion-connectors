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
MotionSource
    -> MotionFrame
  -> usd-motion-plugins MotionPose intake
  -> MotionStream
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

Default policy:

```text
producer
   |
   v
small bounded ring buffer
   |
   +--> drop stale frames if consumer falls behind
   |
   v
latest usable pose
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

Web modules and language bindings remain later modules with their own layout;
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

Recommended JS API style:

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

Python should expose a similarly small interface.

Example:

```python
from motion_connectors import open_connector

connector = open_connector("vmc", port=39539)

for frame in connector:
    print(frame.timestamp)
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

Tracking filters should generally live in the motion layer.

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

These should not be reimplemented independently in every connector.

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

1. Complete the shared connector contract before adding another source.
2. Adapt the imported VMC, mocopi and VRChat OSC implementations without
   replacing their protocol-specific assembly or replay evidence.
3. Add later transports, bindings, browser and XR sources only through the
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

The later release sequence is maintained in the roadmap. WebSocket transport,
capture/bridge tooling and language bindings must continue to carry
`MotionFrame`; they do not move semantic recording or retargeting upstream.

---

## 33. Suggested `v0.3.x`

The roadmap schedules browser tracking and the JS/WASM boundary here. The
browser adapter still owns acquisition and normalization; avatar semantics
remain downstream.

---

## 34. Suggested `v0.4.x`

The roadmap schedules WebXR and integration examples here. mocopi is already
part of the v0.1.0 convergence scope; it is not a future source addition.

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

Small diagnostic tools will be highly valuable.

Recommended utilities:

```text
motion_connect list
motion_connect dump
motion_connect record
motion_connect bridge
motion_connect inspect
```

Examples:

```bash
motion_connect dump --source vmc
motion_connect bridge --source vmc --output websocket
motion_connect record --source mediapipe --output capture.jsonl
```

The v0.1.0 commands are `dump`, `list` and `inspect`. `record` captures raw
transport/session data or diagnostic `MotionFrame` data; semantic motion
recording remains in `usd-motion-plugins`. `bridge` may forward a
`MotionFrame`, but it must not retarget it to an avatar.

---

## 37. Interchange over Network

Eventually define a compact wire representation for canonical motion.

Candidates:

```text
JSON
MessagePack
CBOR
FlatBuffers
Protobuf
custom binary
```

Start with debuggable JSON if needed.

Do not make the JSON representation the permanent ABI too early.

The logical contract is more important than the first serialization format.

---

## 38. ABI Considerations

If multiple languages consume the runtime, avoid exposing complex C++ STL structures as the stable ABI.

Potential stable boundary:

```text
C ABI
```

Example conceptual shape:

```c
motion_connector_t*
motion_frame_t*
motion_connector_poll(...)
```

Bindings can then be layered for:

```text
Python
JavaScript/WASM
Rust
C#
```

This matches the broader runtime goal of language-neutral reusable components.

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

Recommended next implementation tasks:

1. Implement `motionConnectorCore` and resolve its poll, buffer and state semantics.
2. Freeze the source profile and diagnostic identifier rules.
3. Adapt VMC, mocopi and VRChat OSC to the shared contract.
4. Add `motion_connect dump`, `list` and `inspect`.
5. Re-run the imported replay corpus through the unified path.
6. Verify clean installed-package consumption and sibling cleanup.
7. Add WebSocket transport, bindings and later browser/XR sources.

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
its workspace contract already records the edge `motion-connectors →
usd-motion-plugins` (`motion-core`, `motion-recording`).

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
stage, so Rule 5 holds. Whether that closure is acceptable for the web and
WASM path of §18 is `WS-O4`.

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
arrived after the connector contract was documented, but before the planned
`motionConnectorCore` was implemented. Their source-specific APIs therefore
remain separate until the current v0.1.0 convergence work adapts them to one
shared connector interface.

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
`usd-vrm-plugins` cannot finish its migration (MIG-5) while they wait here;
each is frozen there until it moves. They were first scheduled for v0.2.0,
after v0.1.0 had fixed the contract. On 2026-09-19 WS-O7 moved them into
v0.1.0, beside VMC: `liveTransport` and `osc` are linked by all three
connectors, so importing them ahead of mocopi and VRChat OSC would have left
`usd-vrm-plugins` either two copies for a release or an undeclared edge to
this repository. The contract documents came first, but
`motionConnectorCore` is still reserved: the import PRs did not implement it.
Each connector now needs a separate adaptation to that core
([WORKSPACE.md §3](../architecture/WORKSPACE.md#3-moving-code-in)).
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

The web modules' layout stays open (WS-O6), because it is a JavaScript
package's shape and not a CMake one. Binding in
[WORKSPACE.md §1.1](../architecture/WORKSPACE.md#11-native-libraries).

