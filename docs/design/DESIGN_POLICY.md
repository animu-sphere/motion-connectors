# motion-connectors — Design and Implementation Policy

> Status: **accepted** as the repository's design policy, 2026-09-19. Nothing
> it describes is implemented yet; [reference/CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md)
> is the only document that says what is.  
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

- connect to devices, browsers, SDKs, files, streams, and remote services;
- normalize their data into a small runtime-neutral motion contract;
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
    |  MotionPose / MotionStream
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
- BVH live sources
- camera-based body tracking
- hand tracking
- face tracking
- game controllers
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
| MotionPose data contract | shared contract, preferably `usd-motion-plugins` or a tiny dependency-neutral core |
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

Recommended logical contracts:

```text
MotionSource
    -> MotionFrame
    -> MotionPose
    -> MotionStream
```

These should remain independent from USD where possible.

USD should be integrated one layer later.

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

`MotionPose` represents one logical pose.

Example conceptual shape:

```cpp
struct JointPose {
    std::string joint;
    Vec3 translation;
    Quat rotation;
    Vec3 scale;
    float confidence;
};

struct MotionPose {
    double timestamp;
    std::string skeletonProfile;
    std::vector<JointPose> joints;
};
```

The actual API may differ, but the important properties are:

- named joints;
- local transforms by default;
- explicit timestamp;
- optional confidence;
- optional skeleton profile / source profile;
- deterministic coordinate-system conversion.

#### Required behavior

A connector must state:

- source coordinate system;
- handedness;
- up axis;
- length unit;
- transform space;
- timestamp origin;
- tracking confidence semantics.

The connector is responsible for converting these to the canonical contract.

---

### 5.2 MotionStream

`MotionStream` represents an ordered flow of poses.

Conceptually:

```cpp
class MotionStream {
public:
    bool Poll(MotionPose& pose);
};
```

Potential future forms:

```cpp
subscribe(callback)
async iterator
generator
ring buffer
shared-memory stream
```

The abstraction should not require a particular threading model.

---

### 5.3 Additional Channels

The system should allow optional channels beyond skeletal body motion.

Possible channels:

```text
body
hand.left
hand.right
face
eyes
camera
controller
root
tracker.*
blendshape.*
```

Do not force every connector into a single monolithic skeleton.

A packet may therefore contain:

```text
MotionFrame
 ├─ BodyPose
 ├─ HandPose[]
 ├─ FacePose
 ├─ RootTransform
 └─ Metadata
```

This becomes especially important for:

- MediaPipe;
- WebXR;
- OpenXR;
- VRM expressions;
- Apple / ARKit-style blendshapes;
- full-body tracker systems.

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

Initial repository structure:

```text
motion-connectors/
├─ CMakeLists.txt
├─ README.md
├─ LICENSE
├─ docs/
│  ├─ architecture.md
│  ├─ motion-contract.md
│  ├─ coordinate-systems.md
│  └─ connectors.md
│
├─ src/
│  ├─ motionConnectorCore/
│  │  ├─ include/
│  │  └─ src/
│  │
│  ├─ motionConnectorOsc/
│  ├─ motionConnectorVmc/
│  ├─ motionConnectorWebSocket/
│  ├─ motionConnectorMocopi/
│  ├─ motionConnectorMediaPipe/
│  ├─ motionConnectorOpenXR/
│  └─ motionConnectorWebXR/
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
├─ tests/
└─ third_party/
```

Not every connector has to be compiled into every runtime.

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

`motion-connectors` produces:

```text
MotionFrame
MotionPose
MotionStream
```

`usd-motion-plugins` consumes them and provides:

```text
retarget
resample
filter
record
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
MotionPose
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
motionRuntime.Apply(frame, target);
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
MotionStream
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

Prefer:

```text
C++ standard library
small math abstraction
minimal threading primitives
```

Connector-specific dependencies should remain isolated.

Examples:

```text
OpenXR -> OpenXR loader
OSC -> small OSC library
WebSocket -> optional transport library
MediaPipe -> browser/package dependency
mocopi -> SDK or protocol-specific module
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

### Phase 1 — Contract

Implement:

```text
MotionFrame
MotionPose
JointPose
ConnectorCapabilities
IMotionConnector
```

plus:

- timestamp rules;
- coordinate convention;
- joint naming rules;
- unit tests.

This is the most important step.

### Phase 2 — Simple network connector

Implement:

```text
OSC / VMC
```

Reasons:

- relatively simple;
- easy to test;
- no hardware requirement;
- useful for existing VTuber tooling.

### Phase 3 — WebSocket

Implement:

```text
MotionFrame <-> JSON / binary WebSocket bridge
```

This creates a useful transport between:

```text
browser
native runtime
remote mocap source
```

### Phase 4 — MediaPipe

Implement browser-first MediaPipe integration.

Targets:

```text
body
hands
face
```

Convert to canonical MotionFrame.

### Phase 5 — WebXR

Support:

```text
head
controllers
hands
reference spaces
```

This is especially useful for browser-based avatar interaction.

### Phase 6 — mocopi

Add mocopi source integration once the canonical contracts are stable.

Avoid allowing mocopi-specific assumptions to define the core API.

### Phase 7 — OpenXR

Add native XR integration.

Possible sources:

```text
head
controllers
hands
trackers
body tracking extensions
```

### Phase 8 — advanced devices

Examples:

```text
IMU suits
optical mocap
depth-camera tracking
specialized face tracking
```

---

## 31. Suggested First Release Scope

A realistic `v0.1.0` should stay small.

Recommended scope:

```text
motionConnectorCore
motionConnectorOsc
motionConnectorVmc
```

with:

- canonical MotionFrame;
- body joint transforms;
- timestamps;
- confidence;
- source profile;
- example CLI;
- unit tests.

Example CLI:

```bash
motion-connect dump --source vmc --port 39539
```

Output:

```text
frame=1024
timestamp=...
actor=0
hips ...
head ...
leftHand ...
rightHand ...
```

This validates the architecture without requiring USD integration.

---

## 32. Suggested `v0.2.x`

Add:

```text
WebSocket transport
record-stream example
Python bindings
```

This enables:

```text
browser/service
      |
   websocket
      |
native connector
      |
MotionStream
```

---

## 33. Suggested `v0.3.x`

Add:

```text
MediaPipe connector
JS/TS package
WASM-friendly data ABI
```

Then browser tracking becomes a first-class source.

---

## 34. Suggested `v0.4.x`

Add:

```text
WebXR
mocopi
```

and integration examples with `usd-motion-plugins`.

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
motion-connect list
motion-connect dump
motion-connect record
motion-connect bridge
motion-connect inspect
```

Examples:

```bash
motion-connect dump --source vmc
motion-connect bridge --source vmc --output websocket
motion-connect record --source mediapipe --output capture.jsonl
```

These tools should operate on MotionFrame rather than avatar-specific data.

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
  + motion-connectors-core
  + motion-connectors-vmc
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
                         MotionPose
                         MotionStream
                              |
                       usd-motion-plugins
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

1. Define `MotionFrame`, `MotionPose`, and `JointPose`.
2. Freeze canonical coordinate and unit conventions.
3. Define timestamp semantics.
4. Define source profile metadata.
5. Implement a minimal `IMotionConnector`.
6. Implement OSC/VMC as the first real connector.
7. Add a deterministic packet corpus for CI.
8. Add `motion-connect dump`.
9. Define the bridge API consumed by `usd-motion-plugins`.
10. Add WebSocket transport.
11. Add MediaPipe browser adapter.
12. Add WebXR adapter.
13. Add mocopi.
14. Add OpenXR.

The most important architectural decision is to stabilize the **MotionPose / MotionStream boundary before adding many device integrations**.

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
stream
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
MotionPose / MotionStream
      ↓
usd-motion-plugins
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
`SourceMetadata` and the `MotionStream` intake rules, its §11 says a live pose
sender in `motion-connectors` stops at `MotionPose` pushed into a stream, and
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

They are renamed on arrival and lose the `vrm` prefix
([WORKSPACE.md §3](../architecture/WORKSPACE.md#3-moving-code-in)). They
arrive **after** the connector contract of §30 Connector Phase 1 is written,
so that none of them defines the core API by having been first — which is §30
Phase 6's concern about mocopi, applied to all three.

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

This policy's §30 phases are written **Connector Phase 1–8**, because the
ecosystem already has `usd-motion-plugins`' Migration Phase A–F,
`usd-vrm-plugins`' Motion Phase A–H and `usd-mmd-plugins`' Phase 0–9.

### 46.7 The release order follows the imports

§31–§34 put mocopi in v0.4.x. The mocopi and VRChat OSC connectors and
`motionTracking` are imports of measured code, not new designs, and
`usd-vrm-plugins` cannot finish its migration (MIG-5) while they wait here;
each is frozen there until it moves. They were first scheduled for v0.2.0,
after v0.1.0 had fixed the contract. On 2026-09-19 WS-O7 moved them into
v0.1.0, beside VMC: `liveTransport` and `osc` are linked by all three
connectors, so importing them ahead of mocopi and VRChat OSC would have left
`usd-vrm-plugins` either two copies for a release or an undeclared edge to
this repository. The contract still comes first. `motionConnectorCore` exists
before any import, and each connector adapts to it in a change of its own
after it moves ([WORKSPACE.md §3](../architecture/WORKSPACE.md#3-moving-code-in)).
So §30's concern, that mocopi should not define the core API, still holds. The
mapping, and every other departure from
§31–§34, is in the [roadmap status table](../roadmap/README.md#status-at-a-glance),
which is the single source of truth for which release carries what.

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

