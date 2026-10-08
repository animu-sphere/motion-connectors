# Workspace contract

The binding contract for how `motion-connectors` is laid out: component
identities, their kinds and directories, the dependency directions between
them and to the rest of the ecosystem, and the invariants every change keeps.
**A structural change that contradicts this document changes this document
first, in its own pull request**. It is never made through a README, a roadmap
entry or code.

Status (2026-09-21): **contract adopted; the shared core and all three imported
source adapters are implemented, with their source-specific assembly retained
behind the shared boundary.**
The build and CI tree holds the transport, OSC, tracking, VMC, mocopi and
VRChat OSC implementations, along with their record tools. Unimplemented identities below remain
*reserved* until the change that creates them lands, and their rows say so.
Boundary revision (2026-10-06): **target dependency contract accepted**.
§2.1 records the implemented acquisition graph. VMC and mocopi use
`VmcFrameSource` / `MocopiFrameSource`; their former motion source compositions
are private to consumer integration tests.
The shape follows
the design
policy's §16 and §17, and the workspace discipline the sibling repositories
share: plain libraries, a manifest beside each component, every connector an
optional module, and two build modes, `ost` and plain CMake.

## 1. Identities

### 1.1 Native libraries

| Identity | Directory | Role | Arrives from | Status |
| --- | --- | --- | --- | --- |
| `motionConnectorCore` | `libs/motionConnectorCore/` | `IMotionConnector`, `MotionFrame`, `TrackerObservation`, state, capabilities, timing, the bounded frame buffer ([CONNECTOR_CONTRACT.md](../design/CONNECTOR_CONTRACT.md)) | new | implemented 2026-09-21 |
| `motionConnectorTransport` | `libs/motionConnectorTransport/` | UDP receiver, the optional datagram queue, the packet-capture file format, the diagnostic vehicle; knows no protocol | `usd-vrm-plugins` `liveTransport` | imported 2026-09-19, with its history; namespace `openstrata::connectors::transport` |
| `motionConnectorOsc` | `libs/motionConnectorOsc/` | the OSC 1.0 wire format: packets, bundles, type tags, arguments; knows no address semantics | `usd-vrm-plugins` `osc` | imported 2026-09-19, with its history; namespace `openstrata::connectors::osc` |
| `motionConnectorVmc` | `libs/motionConnectorVmc/` | VMC Protocol decode, frame assembly, `vmc.v1`, `IMotionConnector` adapter | `usd-vrm-plugins` `vrmAdapterVmc` | imported 2026-09-21, with its history; namespace `openstrata::connectors::vmc`; its recorder is `tools/vmcRecord/` |
| `motionConnectorMocopi` | `libs/motionConnectorMocopi/` | mocopi native UDP decode, frame assembly, `mocopi.body.v1`, `IMotionConnector` adapter | `usd-vrm-plugins` `vrmAdapterMocopi` | imported and adapted 2026-09-21, with its history; namespace `openstrata::connectors::mocopi`; its recorder is `tools/mocopiRecord/` |
| `motionConnectorVrchatOsc` | `libs/motionConnectorVrchatOsc/` | VRChat OSC Trackers decode, tracking-space normalization, tracker frames, `vrchat-osc.trackers.v1`, `IMotionConnector` adapter | `usd-vrm-plugins` `vrmAdapterVrchatOsc` | imported and adapted 2026-09-21, with its history; namespace `openstrata::connectors::vrchatOsc`; its recorder is `tools/vrchatOscRecord/` |
| `motionConnectorTracking` | `libs/motionConnectorTracking/` | tracker regions, assignment, the tracker solve ([CONNECTOR_CONTRACT.md §4](../design/CONNECTOR_CONTRACT.md#4-trackerobservation)) | `usd-vrm-plugins` `motionTracking` | imported 2026-09-20, with its history; namespace `openstrata::connectors::tracking`; consumes `usd-motion-plugins` `motionCore` |
| `motionConnectorWire` | `libs/motionConnectorWire/` | the `MotionFrame` wire format: encode and decode `openstrata.motion.frame/v1` ([FRAME_WIRE_FORMAT.md](../design/FRAME_WIRE_FORMAT.md)); knows no socket | new | implemented 2026-10-04; namespace `openstrata::connectors::wire` |
| `motionConnectorWebSocket` | `libs/motionConnectorWebSocket/` | `MotionFrame` over WebSocket, both directions, listening or connecting; RFC 6455 implemented inside it ([WEBSOCKET_CONNECTOR.md](../design/WEBSOCKET_CONNECTOR.md)) | new | implemented 2026-10-04; namespace `openstrata::connectors::websocket` |
| `motionConnectorOpenXR` | `libs/motionConnectorOpenXR/` | caller-driven OpenXR spaces, EXT hand and FB upper-body tracking-space observations → `MotionFrame` | new | implemented 2026-10-07; namespace `openstrata::connectors::openxr`; optional SDK module |

Names follow the siblings' workspace discipline (WS-O1, decided 2026-09-19;
[DESIGN_POLICY.md §46.9](../design/DESIGN_POLICY.md#469-names-and-layout-are-the-siblings)):

- **A native library lives under `libs/`**, as `usd-vrm-plugins`',
  `usd-mmd-plugins`' and `usd-motion-plugins`' do, rather than under the
  design policy's `src/`. A connector is a plain library and registers
  nothing, so `libs/` is the right kind.
- **An identity is lower-camel**, and it is also its directory, its CMake
  package and its exported target
  (`motionConnectorCore::motionConnectorCore`). Each connector is its own
  package, so a consumer finds exactly the connectors it asked for.
- **A CLI's command is snake_case** in a lower-camel directory (`motion_connect`
  in `tools/motionConnect/`). An imported record tool keeps its command, so
  `vmc_record` is still `vmc_record`.
- **The C++ namespace is `openstrata::connectors`**, with one level for each
  library (`openstrata::connectors::transport`), as `usd-motion-plugins` nests
  `openstrata::motion::bvh`. The include root is the identity
  (`#include "motionConnectorVmc/Decoder.h"`).

### 1.2 Web modules

| Identity | Directory | Role | Status |
| --- | --- | --- | --- |
| `motionConnectorMediaPipe` | `web/motionConnectorMediaPipe/` | caller-driven MediaPipe Tasks body/hand metric world landmarks and face blendshape results, to the frame wire representation | implemented 2026-10-08; independent npm module |
| `motionConnectorWebXR` | `web/motionConnectorWebXR/` | caller-driven WebXR viewer, controller grip and hand tracking-space observations, to the frame wire representation | implemented 2026-10-07; independent npm module |

Web modules are JavaScript / TypeScript (design policy §18, Rule 8). They are
not compiled into any native build, and a native build never needs a browser
dependency (§17).

Browser boundary decision (2026-10-07, WS-O4 and WS-O6): source modules live
under `web/<identity>/`, each with its own npm package, declarative profiles and
hardware-independent tests. `bindings/js/` remains reserved for a future shared
consumer API; acquisition modules are not bindings over the native libraries.
They use the existing `openstrata.motion.frame/v1` plain JavaScript value shape
and carry 64-bit counters as decimal strings. Native core retains its installed
`motionCore` / OpenUSD closure. No native core WASM build or data ABI is promised
by this choice, and browser acquisition imports no native library.

The caller owns the WebXR session, requested features, reference space and
animation loop. Within an active XR frame, acquisition reads viewer, configured
controller grip spaces and hand joints relative to that reference space. Poll
only drains a bounded queue. Tracking-space observations are not parent-local
humanoid rotations; anatomical assignment and generic pose reconstruction stay
downstream. The browser path never opens a camera, XR session or socket as a
side effect of acquiring or polling. MediaPipe consumes completed caller-owned Tasks results, with no inference or
camera side effect. CC-O2 and SP-O3 use existing position-only tracker values
and declarative source index/name hints; face scores use shared pose channels.
Single-actor acquisition performs no cross-task association. The source-relative
origins and assumed world axes are declared in
[COORDINATE_SYSTEMS §5](../design/COORDINATE_SYSTEMS.md#5-known-sources).

### 1.3 Tools, examples, bindings and data

| Identity | Kind | Directory | Role | Arrives from | Status |
| --- | --- | --- | --- | --- | --- |
| `motion_connect` | CLI | `tools/motionConnect/` | `list`, `dump`, `record`, `bridge`, `inspect` over `MotionFrame` (design policy §36; `bridge` and `record` in [MOTION_CONNECT.md](../design/MOTION_CONNECT.md)) | new | `list`, `dump` and `inspect` implemented 2026-09-21; `record` and `bridge` reserved |
| `vmc_record`, `mocopi_record`, `vrchat_osc_record` | CLI | `tools/<name>Record/`, as §2.1's diagram puts every tool | record a live session to a packet capture; transcribe a saved capture to a trace (`--inspect --export-trace`) | `usd-vrm-plugins`, with each connector | all three imported 2026-09-21, with their history |
| examples | programs | `examples/dump_pose/`, `examples/record_stream/`, `examples/usd_avatar_live/` | the design policy §16's examples | new | reserved |
| Python bindings | binding | `bindings/python/` | `open_connector`, frame iteration (design policy §19) | new | reserved |
| JS / TS package | binding | `bindings/js/` | `openConnector`, `frames()` async iterator, the WASM bridge (design policy §18) | new | reserved |
| test corpus | data | `tests/data/<connector>/` | generated captures and recorded-session manifests (§5) | `usd-vrm-plugins`, with each connector | reserved |

The three record tools remain, one per connector (WS-O3, decided
2026-10-04). They share transport and session flags, not behaviour: each
tool's own options (`--staleness`, `--silence-timeout`, `--assign`,
`--unplaced`), session report and trace transcription belong to its
CLI integration layer, and a recorded-session manifest names the tool that made it
(`"tool": "mocopi_record"`), so the command is provenance. `motion_connect
record`, when it lands, does not replace them: it captures through the shared
connector contract, takes no connector's own options and writes no
connector's session report, and it stays a capture/replay tool rather than a
semantic motion recorder ([CONNECTOR_CONTRACT §12](../design/CONNECTOR_CONTRACT.md#12-raw-capture)).

### 1.4 Reserved, later

| Identity | Role | Why later |
| --- | --- | --- |
| a generation adapter (ARDY) | a motion generator behind `usd-motion-plugins`' generator interface | that interface does not exist yet (`usd-vrm-plugins` Motion Phase F, moved here) |
| a C ABI | `motion_connector_t`, `motion_frame_t`, `motion_connector_poll` (design policy §38) | CC-O7 |
| advanced devices | IMU suits, optical mocap, depth cameras, dedicated face trackers | Connector Phase 8 |

### 1.5 Deliberately not here

| Not here | Where it lives | Why |
| --- | --- | --- |
| `MotionPose`, `RootMotion`, `MotionChannelSet`, `SourceMetadata`, `MotionStream` intake | `usd-motion-plugins` `motionCore`, `motionRecording` | the shared motion values ([DESIGN_POLICY.md §46.1](../design/DESIGN_POLICY.md#461-the-pose-is-usd-motion-plugins-motionpose)) |
| retargeting, filtering, smoothing, resampling, canonical recording, BVH / NPZ / VMD | `usd-motion-plugins` | design policy §20, §25, §26 |
| VRM humanoid mapping, expressions, spring bones | `usd-vrm-plugins` | design policy §21 |
| PMX / MMD bone semantics | `usd-mmd-plugins` | design policy §22 |
| scheduling, the update loop, runtime profiles | `usd-avatar-runtime`, `usd-stage-runner` | design policy §23, §40 |
| a USD file-format plugin, stage authoring | the USD layers above | design policy §2.2, §24 |

## 2. Dependency directions

### 2.1 Inside the repository

**Target library edges, accepted 2026-10-06.** Reserved components remain
reserved under §1; this diagram does not claim that they exist.

```text
motionConnectorCore ───────→ usd-motion-plugins motionCore
motionConnectorTransport ──→ (standard library, OS sockets)
motionConnectorOsc ────────→ (standard library)
motionConnectorVmc ────────→ motionConnectorCore, usd-motion-plugins motionCore; motionConnectorTransport, motionConnectorOsc
motionConnectorMocopi ─────→ motionConnectorCore, usd-motion-plugins motionCore; motionConnectorTransport
motionConnectorVrchatOsc ──→ motionConnectorCore, usd-motion-plugins motionCore; motionConnectorTransport, motionConnectorOsc (its CLI adds motionConnectorTracking, motionRecording)
motionConnectorTracking ───→ usd-motion-plugins motionCore
motionConnectorWire ───────→ motionConnectorCore, usd-motion-plugins motionCore
motionConnectorWebSocket ──→ motionConnectorCore, motionConnectorWire, motionConnectorTransport (diagnostics, capture format); no third-party library
motionConnectorOpenXR ─────→ motionConnectorCore, the OpenXR loader
tools/*, examples/* ───────→ the connectors they name; usd-motion-plugins libraries
bindings/python ───────────→ motionConnectorCore, the connectors it exposes
web modules ───────────────→ browser APIs, the MediaPipe package; no native library
```

`motionConnectorCore` provides the shared connector contract; source adapters
emit `MotionFrame` and end there. Only tools, examples and runtime integration
may add `motionSampling` / `motionRecording` for downstream motion treatment.

OpenXR borrows the caller's instance, base/input spaces and optional hand/body
trackers. The caller owns extensions, session events, action sync and the frame
loop. `Acquire` locates one SDK time; `Poll` drains the shared bounded queue.
Its only direct links are the core and `OpenXR::openxr_loader`.
`MOTIONCONNECTORS_BUILD_OPENXR` defaults OFF and finds SDK ≥1.1.36 only when
selected. Other connectors require no OpenXR installation.

The three source recorders stay here, with this target composition:

```text
vmc_record / mocopi_record / vrchat_osc_record
    → their connector library             (raw capture / source replay)
    → motionRecording                     (semantic export only)
```

**Acquisition split, verified in the tree on 2026-10-06:**

VMC and mocopi libraries have only the target edges above. Their frame sources
own decode, source assembly, restart/session facts and diagnostic datagram
identity; their connectors own bounded acquisition queues. Former
intake/sampling/restart-policy compositions live in each connector's
`tests/consumer/LiveSource.*`, are not installed, and consume the same
library-owned acquisition paths. Consumer integration tests declare their
sampling/recording dependencies explicitly. Recorder raw capture and inspection
construct no downstream intake. Each recorder invokes its private
`TraceExport.cpp` only for `--inspect --export-trace`, using `motionRecording`'s
writer; VMC and mocopi replay through their acquisition frame sources, and
VRChat OSC performs the operator-configured tracker solve in the export path.
VMC reports acquisition facts and omits the former downstream `intake:` line.
The packaged `motionRecording` dependency requires `motionSampling` in the
artifact closure; this is a packaging pin, not a direct recorder CMake link.

### 2.2 Forbidden

| Edge | Why |
| --- | --- |
| any component → `usd-vrm-plugins`, `usd-mmd-plugins`, `usd-avatar-runtime` | the ecosystem's direction is one way ([§2.3](#23-the-ecosystem)) |
| any library → OpenUSD `usd`, `sdf`, `usdGeom`, `usdSkel`, Hydra, OpenExec | no connector requires a `UsdStage` (design policy Rule 5) |
| any connector library → `motionSampling`, `motionRecording`, `motionRetarget`, `motionUsd`, directly or transitively | acquisition ends at `MotionFrame` |
| a connector → another connector | a runtime route is not a build dependency: a mocopi app can send VMC, and that creates no edge between the two (measured: `usd-vrm-plugins` adapter plan §2.1) |
| `motionConnectorTransport` ↔ `motionConnectorOsc` or `motionConnectorWire`, in either direction | a wire format needs no socket, and a socket knows no wire format |
| `motionConnectorCore` → any connector, transport, network, device or browser dependency, or protocol implementation | the core contains only contract values, state, capabilities, timing and bounded acquisition queues (design policy §28, Rule 9) |
| `motionConnectorOsc` → a protocol address literal (`/VMC/…`, `/tracking/…`) | OSC wire format is a library; address semantics are a connector's |
| `motionConnectorTransport` → protocol or `MotionFrame` semantics | transport owns bytes, queues, capture and diagnostics, not motion policy |
| `motionConnectorTracking` → a tracker region aliased to a `HumanJoint` | an alias turns assignment into a lookup and leaves the solve nothing to do (measured: `usd-vrm-plugins` OSC track §5.1) |
| any library → a filter, retargeter or recorder of its own | these exist once, downstream |
| a component → a sibling's source tree | siblings are consumed as installed packages |

### 2.3 The ecosystem

```text
motion-connectors ─→ usd-motion-plugins ←─ usd-vrm-plugins
                              ↑
                       usd-mmd-plugins
usd-avatar-runtime ─→ all of the above
```

This is the same diagram as `usd-motion-plugins` WORKSPACE §2.3.

`motion-connectors` depends on `usd-motion-plugins` and on nothing else in the
ecosystem. Nothing in `usd-motion-plugins` may depend on it
(`usd-motion-plugins` WORKSPACE §2.2). `usd-mmd-plugins` refuses the edge to it
(its WORKSPACE §2.2), and `usd-avatar-runtime` composes all of them.

### 2.4 Enforcement

Gates, added with the code they guard, as in the sibling repositories: every
edge declared in the component's manifest and validated by
`ost plugin test --workspace --graph-only`; a link-line check per library
(`motionConnectorCore` links nothing beyond `motionCore`); an include and
literal scan refusing OpenUSD stage headers everywhere, and protocol address
literals in `motionConnectorOsc` and `motionConnectorTransport`.

The target gates additionally reject `motionSampling`, `motionRecording`,
`motionRetarget` and `motionUsd` in every connector library's dependency
closure, and stage/UsdSkel headers (`pxr/usd/usd/`, `pxr/usd/usdSkel/`) or
OpenExec includes in library code. `motionConnectorCore` must contain no
protocol implementation; transport must contain no protocol semantics.
`scripts/check_boundaries.py` scans every library's production sources, CMake
files, package configs and manifest, and repeats the header/config scan on the
clean installed prefix. `cmake/MotionConnectorsBoundaries.cmake` traverses actual
target closures, including private static links, aliases, imported interfaces,
configuration-specific imported library paths and generator-expression edges.
It runs on standalone component builds even with tests disabled, at the end of
workspace composition, and on each installed-consumer target. All configurations
are checked conservatively; a forbidden edge in an inactive branch is refused.
The installed consumer compiles every installed library header with discovery
of all four downstream packages disabled. VMC and mocopi also retain their
acquisition runtime probes. `workspace_boundaries`, `_selftest` and the SDK-free
`boundaries-check` workflow check the scans and injected forbidden cases.
Apply dependency restrictions to libraries, not recorder tools or consumer
integration tests that deliberately exercise downstream intake/export.

The source scan also checks browser production imports and npm runtime/peer
dependencies. WebXR imports only files inside its own module; MediaPipe may
add `@mediapipe/tasks-vision`. Native libraries, Node transports and downstream
motion packages are forbidden on this path. Browser acquisition fixtures run
in `web-check`; the native wire corpus reader checks the browser output fixture
without adding a Node dependency to native builds.

## 3. Moving code in

Code arrives from `usd-vrm-plugins` under that repository's moving rules (its
WORKSPACE.md §9.2), which this repository keeps from the receiving side:

1. **History comes with the code.**
2. **Every live input arrives in one release, one identity per change**, in
   dependency order: the transport and the wire format, then the tracker
   library, then the three connectors with their record tools. All of them are
   in v0.1.0, and `usd-vrm-plugins` drops them all in one change. Moving the
   shared leaves first would leave that repository two copies for a release, or
   an edge to this one that no contract declares (WS-O7, decided 2026-09-19;
   its WORKSPACE.md §9.2 rule 7).
3. **Renamed on arrival**, once:

   | `usd-vrm-plugins` | Here |
   | --- | --- |
   | `liveTransport` | `motionConnectorTransport` |
   | `osc` | `motionConnectorOsc` |
   | `vrmAdapterVmc` | `motionConnectorVmc` |
   | `vrmAdapterMocopi` | `motionConnectorMocopi` |
   | `vrmAdapterVrchatOsc` | `motionConnectorVrchatOsc` |
   | `motionTracking` | `motionConnectorTracking` |
   | source-owned `VMC_*`, `MOCOPI_*`, `VRCHAT_OSC_*` diagnostic codes | resolved 2026-09-21 |

4. **Tests, the generated corpus and the recorded-session manifests come with
   it.** The replay evidence named for the move is reproduced here before the
   sender deletes its copy.
5. **The contract documents come first.** The imported source leaves arrived
   after the connector contract was documented, but before
   `motionConnectorCore` was implemented. They remain source-specific until a
   separate convergence change adapts them to the shared interface, so a move
   stays a move.
6. **Nothing VRM-specific arrives.** A header that names VRM vocabulary is a
   boundary defect and stays behind.

## 4. Versioning, build and distribution

- One `VERSION` at the root, mirrored by the tag, the changelog and every
  manifest.
- OpenUSD is pinned exactly, to the release `usd-motion-plugins` pins, because
  `motionCore` is built against it
  ([DEPENDENCIES.md §1](DEPENDENCIES.md#1-openusd)).
- **Every connector is optional.** Each is a separate CMake option and a
  separate `ost` component. A build that asks only for VMC configures no
  OpenXR, WebSocket or browser dependency (design policy §28, §41).
- Both build modes, `ost` and plain CMake, are kept working, and every package
  is consumed from a clean installed prefix in CI, where the installed
  `motion_connect` also runs.
- How connectors are distributed is §4.1 (WS-O5, decided 2026-10-04).
  `usd-vrm-plugins` left it open as BND-2 and handed it here.

### 4.1 Distribution

The release follows `usd-motion-plugins`' release workflow, so a consumer
pins this repository the way it already pins that one.

- **One version, separate artifacts.** Every library and CLI is released at
  the root `VERSION`, and each is its own artifact, so a consumer takes
  exactly the connectors it names. A version per connector would buy a
  compatibility matrix nobody needs (BND-2's recommendation).
- **One release per tag.** Pushing `vX.Y.Z`, equal to `VERSION` and with a
  finalized changelog section, builds and tests the workspace on the PR lane's
  targets and digest-pinned runtimes, then packages every member and creates
  one GitHub release.
- **One OCI repository, tagged per member.** Each member is pushed to
  `ghcr.io/animu-sphere/motion-connectors` as
  `<member>-<version>-<target>`. The release notes and a release asset carry
  the pin table: one row per member per target with its archive digest and
  OCI source, generated from what was pushed. A consumer names those rows
  in its `requires.libraries` / `requires.tools`. A library's installed
  source profiles travel inside its artifact (`ost library package` puts
  `share/motion-connectors/profiles/` in the archive).
- **The libraries are published first; the CLIs follow `ost`.** `ost`
  packages a workspace tool only through `ost plugin package --workspace`,
  which refuses a workspace with no plugin bundle (`ost` 0.23.14:
  `no plugin bundles found in the workspace member set`), and this repository
  has none. Until `ost` packages a tool in a bundle-free workspace, a release
  publishes the libraries, and the CLIs ship as source in the release's
  source archive. Each tool keeps its `openstrata.tool.yaml` so that it
  publishes unchanged when `ost` can package it.
- **A vendor SDK is declared, never discovered.** It appears in its
  connector's manifest and nowhere else, and only that connector's artifact
  requires it. None of the v0.1.0 connectors uses one; the first that does
  fixes the manifest field.
- **Device validation stays an operator's run.** Recorded sessions are
  replayed locally with `scripts/check_recorded_sessions.py` (§5), not in a
  release or capability lane, because CI holds no device bytes. A capability
  is claimed only from tests CI can run
  ([CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md)).

## 5. Test data

```text
tests/data/<connector>/
├─ generated/                 protocol shapes, committed, CI-runnable, no hardware
└─ recorded/
   ├─ redistributable/        real sessions cleared for publication
   └─ manifests/              everything else, as measured facts
```

A session that cannot be redistributed leaves **no bytes** in the repository.
It leaves a manifest: capture hash, recording tool version, sender and device
identity and version, the measured statistics, expected diagnostics and
counts, validation date, redistribution status. Generated captures are
reproduced by committed code, and a `--check` mode fails when a committed
capture no longer matches its generator. An operator who kept a session's
bytes replays the manifest rows against them with
`scripts/check_recorded_sessions.py`, which is never part of CI
([building guide](../guides/building.md#replaying-the-recorded-device-sessions)). This is `usd-vrm-plugins`' corpus rule
(adapter plan §9.2), carried unchanged.

## 6. Invariants

1. The edges are §2's, declared in manifests and gated in CI.
2. No component depends on a consumer.
3. No product, device or protocol name controls behaviour outside its own
   connector.
4. Every library builds and tests without hardware, without a network peer
   and without a stage (design policy Rule 7).
5. A capability is claimed only with a test behind it
   ([CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md)).
6. The design policy's §42 rules hold.
7. A connector library may depend on `motionCore`, but must not depend on
   `motionSampling`, `motionRecording`, `motionRetarget` or `motionUsd`;
   §2.1 records the current violations to remove.
8. A connector emits observations and reports discontinuity. Temporal
   interpretation and continuity policy belong downstream.
9. A connector library never opens or authors a `UsdStage`; protocol-specific
   code never enters core, and transport never knows protocol semantics.

## 7. Open questions

WS-O1, the names and layout, and WS-O7, the import order, were decided on
2026-09-19 (§1.1 and §3). WS-O2 was decided on 2026-09-24: tracker assignment
and the direct solve remain together in `motionConnectorTracking`
([CONNECTOR_CONTRACT §4](../design/CONNECTOR_CONTRACT.md#4-trackerobservation)).
The 2026-10-06 boundary clarification retains them for now and requires a
later placement review for generic algorithms under
[DESIGN_POLICY §47.5](../design/DESIGN_POLICY.md#475-tracking-and-placement-decisions).
WS-O3 was decided on 2026-10-04: the three record tools remain, one per
connector (§1.3). WS-O5 was decided on 2026-10-04: one version, one release
per tag, and one artifact per member in one OCI repository, the CLIs once
`ost` can package them (§4.1).

WS-O4 and WS-O6 were decided on 2026-10-07 (§1.2): retain the native
OpenUSD closure and use the existing frame wire representation in independent
`web/<identity>/` npm modules. A native WASM ABI remains future work rather
than a prerequisite for browser acquisition. There are no remaining workspace
questions; source-specific observation questions remain in their owning
contracts.
