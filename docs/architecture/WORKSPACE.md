# Workspace contract

The binding contract for how `motion-connectors` is laid out: component
identities, their kinds and directories, the dependency directions between
them and to the rest of the ecosystem, and the invariants every change keeps.
**A structural change that contradicts this document changes this document
first, in its own pull request**. It is never made through a README, a roadmap
entry or code.

Status (2026-09-19): **contract adopted; the two leaf libraries imported.**
The build and CI tree holds `motionConnectorTransport` and
`motionConnectorOsc`. Every other identity below
is *reserved* until the change that creates it lands, and its row then says
so. The shape follows
the design
policy's §16 and §17, and the workspace discipline the sibling repositories
share: plain libraries, a manifest beside each component, every connector an
optional module, and two build modes, `ost` and plain CMake.

## 1. Identities

### 1.1 Native libraries

| Identity | Directory | Role | Arrives from | Status |
| --- | --- | --- | --- | --- |
| `motionConnectorCore` | `libs/motionConnectorCore/` | `IMotionConnector`, `MotionFrame`, `TrackerObservation`, state, capabilities, timing, the bounded frame buffer ([CONNECTOR_CONTRACT.md](../design/CONNECTOR_CONTRACT.md)) | new | reserved |
| `motionConnectorTransport` | `libs/motionConnectorTransport/` | UDP receiver, the optional datagram queue, the packet-capture file format, the diagnostic vehicle; knows no protocol | `usd-vrm-plugins` `liveTransport` | imported 2026-09-19, with its history; namespace `openstrata::connectors::transport` |
| `motionConnectorOsc` | `libs/motionConnectorOsc/` | the OSC 1.0 wire format: packets, bundles, type tags, arguments; knows no address semantics | `usd-vrm-plugins` `osc` | imported 2026-09-19, with its history; namespace `openstrata::connectors::osc` |
| `motionConnectorVmc` | `libs/motionConnectorVmc/` | VMC Protocol decode, frame assembly, `vmc.v1` | `usd-vrm-plugins` `vrmAdapterVmc` | imported 2026-09-21, with its history; namespace `openstrata::connectors::vmc`; its recorder is `tools/vmcRecord/` |
| `motionConnectorMocopi` | `libs/motionConnectorMocopi/` | mocopi native UDP decode, frame assembly, `mocopi.body.v1` | `usd-vrm-plugins` `vrmAdapterMocopi` | reserved |
| `motionConnectorVrchatOsc` | `libs/motionConnectorVrchatOsc/` | VRChat OSC Trackers decode, tracking-space normalization, tracker frames | `usd-vrm-plugins` `vrmAdapterVrchatOsc` | reserved |
| `motionConnectorTracking` | `libs/motionConnectorTracking/` | tracker regions, assignment, the tracker solve ([CONNECTOR_CONTRACT.md §4](../design/CONNECTOR_CONTRACT.md#4-trackerobservation)) | `usd-vrm-plugins` `motionTracking` | imported 2026-09-20, with its history; namespace `openstrata::connectors::tracking`; the first member here to consume `usd-motion-plugins` |
| `motionConnectorWebSocket` | `libs/motionConnectorWebSocket/` | `MotionFrame` over WebSocket, both directions | new | reserved |
| `motionConnectorOpenXR` | `libs/motionConnectorOpenXR/` | OpenXR head, controllers, hands, body-tracking extensions | new | reserved |

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
| `motionConnectorMediaPipe` | decided by WS-O6 | MediaPipe pose, hands and face in the browser, to `MotionFrame` | reserved |
| `motionConnectorWebXR` | decided by WS-O6 | WebXR viewer, controllers and hand input, to `MotionFrame` | reserved |

Web modules are JavaScript / TypeScript (design policy §18, Rule 8). They are
not compiled into any native build, and a native build never needs a browser
dependency (§17).

### 1.3 Tools, examples, bindings and data

| Identity | Kind | Directory | Role | Arrives from | Status |
| --- | --- | --- | --- | --- | --- |
| `motion_connect` | CLI | `tools/motionConnect/` | `list`, `dump`, `record`, `bridge`, `inspect` over `MotionFrame` (design policy §36) | new | reserved |
| `vmc_record`, `mocopi_record`, `vrchat_osc_record` | CLI | `tools/<name>Record/`, as §2.1's diagram puts every tool | record a live session to a packet capture and a trace | `usd-vrm-plugins`, with each connector | reserved |
| examples | programs | `examples/dump_pose/`, `examples/record_stream/`, `examples/usd_avatar_live/` | the design policy §16's examples | new | reserved |
| Python bindings | binding | `bindings/python/` | `open_connector`, frame iteration (design policy §19) | new | reserved |
| JS / TS package | binding | `bindings/js/` | `openConnector`, `frames()` async iterator, the WASM bridge (design policy §18) | new | reserved |
| test corpus | data | `tests/data/<connector>/` | generated captures and recorded-session manifests (§5) | `usd-vrm-plugins`, with each connector | reserved |

Whether the three record tools remain or fold into `motion_connect record`
is WS-O3.

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

```text
motionConnectorCore ───────→ usd-motion-plugins motionCore
motionConnectorTransport ──→ (standard library, OS sockets)
motionConnectorOsc ────────→ (standard library)
motionConnectorVmc ────────→ usd-motion-plugins motionCore, motionSampling, motionRecording; motionConnectorTransport, motionConnectorOsc
motionConnectorMocopi ─────→ motionConnectorCore, motionConnectorTransport
motionConnectorVrchatOsc ──→ motionConnectorCore, motionConnectorTransport, motionConnectorOsc
motionConnectorTracking ───→ usd-motion-plugins motionCore (motionConnectorCore is still reserved)
motionConnectorWebSocket ──→ motionConnectorCore, an optional WebSocket library
motionConnectorOpenXR ─────→ motionConnectorCore, the OpenXR loader
tools/*, examples/* ───────→ the connectors they name; usd-motion-plugins libraries
bindings/python ───────────→ motionConnectorCore, the connectors it exposes
web modules ───────────────→ browser APIs, the MediaPipe package; no native library
```

The imported connectors link `usd-vrm-plugins`' `motionRuntime` today, for its
live-source bridge. After the import they link `usd-motion-plugins`'
`motionSampling` or `motionRecording` instead, whichever holds that bridge
when it moves. That edge is recorded here when the import lands.

### 2.2 Forbidden

| Edge | Why |
| --- | --- |
| any component → `usd-vrm-plugins`, `usd-mmd-plugins`, `usd-avatar-runtime` | the ecosystem's direction is one way ([§2.3](#23-the-ecosystem)) |
| any library → OpenUSD `usd`, `sdf`, `usdGeom`, `usdSkel`, Hydra, OpenExec | no connector requires a `UsdStage` (design policy Rule 5) |
| a connector → another connector | a runtime route is not a build dependency: a mocopi app can send VMC, and that creates no edge between the two (measured: `usd-vrm-plugins` adapter plan §2.1) |
| `motionConnectorTransport` ↔ `motionConnectorOsc`, in either direction | a wire format needs no socket, and a socket knows no wire format |
| `motionConnectorCore` → any connector, transport, network, device or browser dependency | the core stays smaller than any SDK (design policy §28, Rule 9) |
| `motionConnectorOsc` → a protocol address literal (`/VMC/…`, `/tracking/…`) | OSC wire format is a library; address semantics are a connector's |
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
   | `VRM_VMC_*`, `VRM_MOCOPI_*` diagnostic codes | DIAG-O1 |

4. **Tests, the generated corpus and the recorded-session manifests come with
   it.** The replay evidence named for the move is reproduced here before the
   sender deletes its copy.
5. **The contract comes first.** No connector is imported before
   [CONNECTOR_CONTRACT.md](../design/CONNECTOR_CONTRACT.md)'s interface exists
   in `motionConnectorCore`; an import adapts to the contract in a change of its
   own after the move, so a move stays a move.
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
  is consumed from a clean installed prefix in CI.
- How connectors are distributed — one release with separate artifacts, or
  separate downloads — is WS-O5. `usd-vrm-plugins` left it open as BND-2 and
  handed it here.

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
capture no longer matches its generator. This is `usd-vrm-plugins`' corpus rule
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

## 7. Open questions

WS-O1, the names and layout, and WS-O7, the import order, were decided on
2026-09-19 (§1.1 and §3).

| Id | Question | Resolve by |
| --- | --- | --- |
| WS-O2 | `motionConnectorTracking`'s two halves. Assignment is a connector-side policy. The solve produces a `MotionPose` from observations, which `usd-vrm-plugins` called "the motion layer's" but still sent here. Keep both here, or send the solve to `usd-motion-plugins` | the VRChat OSC import |
| WS-O3 | The three imported record tools: keep them, or fold them into `motion_connect record` after the move (they share argument-parsing idiom, not behaviour, as measured in `usd-vrm-plugins`' OSC track §3.4) | after the last import |
| WS-O4 | `motionConnectorCore`'s closure. Through `motionCore` it links OpenUSD's `gf`, `tf` and `vt`, which conflicts with design policy §28 ("C++ standard library, small math") and with a WASM build. Options: accept it natively and keep the web path on the wire format only (CC-O8); ask `usd-motion-plugins` for a foundation-free value layer; or put a C ABI (CC-O7) between them | Connector Phase 3 (WebSocket), before any WASM work |
| WS-O5 | Distribution (`usd-vrm-plugins` BND-2): one GitHub release carrying per-connector artifacts, or separate downloads; one version for all connectors (recommended there) or one each; how a vendor SDK dependency is declared rather than discovered; whether hardware validation becomes a capability lane | the first release |
| WS-O6 | Web module layout: under `src/` as design policy §16 lists them, or under `bindings/js/` as one npm package with the JS API | Connector Phase 4 |
