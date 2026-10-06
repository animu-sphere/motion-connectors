# Changelog

All notable changes to `motion-connectors` are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **mocopi acquisition boundary.** `MocopiFrameSource` retains native decode,
  source assembly, restart/rig facts, timestamps and diagnostic datagram identity.
  `MocopiConnector` consumes it without downstream motion intake. The former
  `MocopiLiveSource` public API moves to recorder-private composition; sampling
  and recording dependencies leave the library CMake links, package config,
  public headers and manifest. Recorder and consumer tests declare them directly.
  Acquisition tests compare every capture through the shared connector; installed
  consumption includes every header with all four downstream packages disabled.


- **`motionConnectorWebSocket`, `MotionFrame` over WebSocket.**
  `WebSocketConnector` receives `openstrata.motion.frame/v1` messages as an
  `IMotionConnector`, and `WebSocketFrameSender` sends them; either one
  listens or connects. WEBSOCKET_CONNECTOR.md is now binding. RFC 6455 is the
  library's own, so there is no third-party dependency: the opening handshake
  requires the subprotocol `openstrata.motion.frame.v1` and refuses an
  `Origin` that is not allow-listed, and every framing refusal in §5 is seen
  in the frame header, closes the connection with its status, and names its
  byte. One message is one frame. The connector rewrites `frameNumber` and
  `receiveTimestamp`, carries everything else verbatim, and reports a sender
  restart, a gap, a timestamp regression and a profile mismatch. A codec
  refusal drops one message and passes its `WIRE_*` code through. A listener
  holds one receiving peer and answers a second with 503. A sender serves up
  to 8 peers from one encoding, each with a bounded queue. A connect role
  retries at 0.5 s, doubling to 5 s. Received messages can be captured in the
  transport's format as `!websocket-packet-capture`. The 14 `WEBSOCKET_*`
  codes are in DIAGNOSTICS.md. Two generated corpora come with it: 56 byte
  streams for the handshake and framing, and 8 message captures for §7's
  rows. A loopback suite runs both directions in both roles. The library is
  built by default (`MOTIONCONNECTORS_BUILD_WEBSOCKET`), and is an `ost`
  member, a release artifact and an installed-consumer package.
- **`motionConnectorWire`, the `MotionFrame` wire codec.** `EncodeFrame` and
  `DecodeFrame` write and read `openstrata.motion.frame/v1`, one JSON message
  per frame, as FRAME_WIRE_FORMAT §3–§6 spell it; those sections are now
  binding. Decode after encode is the identity, and re-encoding a decoded
  message writes the same bytes. The reader refuses every §6 case as a
  `WIRE_*` code (16 codes, now in DIAGNOSTICS.md) whose subject is a path into
  the message, and leaves the frame untouched. The writer refuses what the
  reader would refuse, non-finite numbers included. The JSON parser is the
  library's own, so there is no third-party dependency. A generated corpus of
  59 messages (5 canonical, 3 accepted, 51 refused) is checked by
  `motionConnectorWire_corpus` and kept in step with its generator by
  `motionConnectorWire_messageGen`. The library is an `ost` member, a release
  artifact and an installed-consumer package. A counter string is now also
  refused with a leading zero, so it has one spelling (§4.3).

### Changed

- **VMC acquisition ends at `MotionFrame`.** `VmcConnector` now uses
  `VmcFrameSource` for decode, normalization, assembly and source diagnostics.
  The library, its installed package config and its manifest no longer depend
  on `motionSampling` or `motionRecording`. The former public
  `motionConnectorVmc/LiveSource.h` is removed; `VmcLiveSource` is now private
  to `vmc_record`, whose downstream dependencies are explicit. Existing
  sampling and restart-policy evidence remains as consumer integration tests.
  A hardware-independent acquisition test covers missing/stale/duplicate
  observations, unchanged restart timestamps, receive-clock fallback and
  diagnostic identity. The installed-consumer lane also builds and runs VMC
  with all four downstream motion packages disabled. Recorder reports and
  trace exports keep their existing behavior; raw/export separation remains
  Boundary Phase B work.
- **`motion_connect bridge` is designed, and `FW-O2` is decided.** The new
  `docs/design/MOTION_CONNECT.md` owns what the shared-contract CLI does
  beyond reading one connector. `websocket` becomes a source of `list`,
  `dump` and `inspect`. `bridge` polls one source connector, live or from a
  capture, and hands each frame unchanged to `WebSocketFrameSender`, which
  listens on loopback or connects. It sends nothing it did not poll. A
  replayed capture waits for a peer and keeps its recorded pacing. Connector
  state and diagnostics do not cross the wire in version 1 (`FW-O2`): a
  receiver's state is its link's, a code belongs to the layer that raised it,
  and silence and sparse frames already reach the receiver. The bridge prints
  the upstream state and every diagnostic to its operator. How `record`
  captures a UDP source is the new `CLI-O1`.
- **The WebSocket connector is designed.** The new
  `docs/design/WEBSOCKET_CONNECTOR.md` owns how
  `openstrata.motion.frame/v1` messages travel over WebSocket. RFC 6455 is
  implemented inside `motionConnectorWebSocket`, with no third-party library,
  so the connector stays caller-driven and its framing refusals stay
  properties of its own parse. An endpoint listens or connects, and receives
  or sends, independently. The subprotocol `openstrata.motion.frame.v1` is
  required. A request carrying `Origin` is refused unless allow-listed,
  because a loopback bind does not stop a web page. One message is one frame:
  the connector rewrites `frameNumber` and `receiveTimestamp`, reports a
  sender restart, a gap and a timestamp regression, and carries everything
  else verbatim. The `WEBSOCKET_*` diagnostic set (14 codes) is frozen there.
  Received messages are captured in the transport's packet-capture format.
  TLS (`WSC-O1`) and several receiving peers (`WSC-O2`) stay open, and
  `FW-O2` moves to `motion_connect bridge`.
- **CC-O8 settles the `MotionFrame` wire representation.** Version 1 is JSON,
  one object per frame, identified as `openstrata.motion.frame/v1`. Joints
  are keyed by name, an absent value is an absent key, numbers round-trip
  exactly, and 64-bit counters are decimal strings. A reader refuses unknown
  keys, so adding a field is a new version. It is a message format, not a
  file format, and it is not the ABI. A binary encoding (`FW-O1`) and state
  and diagnostics on the wire (`FW-O2`) stay open. The new
  `docs/design/FRAME_WIRE_FORMAT.md` owns it, and WORKSPACE reserves
  `motionConnectorWire` for the codec.

## [0.1.0] - 2026-10-04

### Changed

- **WS-O5 settles distribution.** Every member is released at the root
  `VERSION` as its own artifact. A `vX.Y.Z` tag creates one GitHub release
  and pushes each member per target to
  `ghcr.io/animu-sphere/motion-connectors` as `<member>-<version>-<target>`.
  The release carries a generated digest pin table, as `usd-motion-plugins`
  does. The seven libraries publish first. The CLIs ship as source until
  `ost` packages a workspace tool without a plugin bundle; it currently
  refuses. A vendor SDK is declared in its connector's manifest; none is
  used. Recorded-session replay stays an operator's local run. WORKSPACE
  §4.1 records it.
- **The installed `motion_connect` runs from a clean prefix.**
  `workspace_installed_consumer` now runs `motion_connect`'s `list`, `dump`,
  `inspect` and argument checks against the copy in the install prefix, with
  one capture per connector copied out of the repository, whenever the build
  has `motion_connect`. Before, the lane consumed only the libraries, and the
  v0.1.0 criterion that the CLI works from an installed prefix had no test.
- **WS-O3 keeps the three record tools.** `vmc_record`, `mocopi_record` and
  `vrchat_osc_record` remain, one per connector, and are not folded into
  `motion_connect record`. They share transport and session flags, but their
  own options, session reports and trace transcriptions are source-specific,
  and recorded-session manifests name the tool that made them. A later
  `motion_connect record` captures through the shared connector contract
  without replacing them.
- **`vmc_record` exports a trace only from a saved capture.** `--export-trace`
  and `--sender-session` now go with `--inspect` alone, as in `mocopi_record`
  and `vrchat_osc_record`; a live session given `--export-trace` is refused
  with exit 2. `--max-frames` and its `--max-frames reached` stop reason are
  removed, because a recording no longer holds poses. Record first, then run
  `vmc_record --inspect session.vmcpackets --export-trace session.trace`. The
  tool also refuses an empty `--export-trace` and a trace path that names the
  capture being read, however it is spelled. CONNECTOR_CONTRACT §12 records
  offline transcription through `motionRecording` as the one place
  capture-to-trace conversion is invoked.
- **VMC and VRChat OSC convert through `motionCore`'s basis operation
  (CS-O1).** Every `usd-motion-plugins` pin — `motionCore`, `motionSampling`
  and `motionRecording`, in all five libraries and `vrchat_osc_record` — moves
  to the digests in v0.5.2's pin table. `VmcBasis` and `TrackingSpaceBasis`
  state each connector's measured reflection through X as a
  `SignedPermutationBasis`, and `ToCanonicalPosition` / `ToCanonicalRotation`
  apply it with `ApplyBasisToPosition` / `ApplyBasisToRotation` instead of
  local sign flips. VMC's output is unchanged; VRChat OSC's rotation is now
  normalised in double rather than float. Euler composition stays in the
  VRChat OSC connector.
- **The documentation states ownership instead of status.** The root README
  follows the shared shape (Scope, Architecture, Components, Documentation,
  Build, License) and no longer carries a current milestone. The
  documentation guidelines add the cross-repository rule and the root README
  rules. The operator evidence `usd-vrm-plugins` carried for these connectors
  — a VMC sender session, a mocopi recovery, a rolled VRChat OSC take, a
  redistributable mocopi capture, the live path from release artifacts — is
  on this roadmap now. Links into `usd-vrm-plugins`' motion plans point at its
  archive, the canonical basis at `usd-motion-plugins`' contract, and three
  mocopi headers lose a malformed URL.
- **CS-O1 has a shared library home.** `usd-motion-plugins` 0.5.2 source adds
  the signed-permutation operation to `motionCore`.
- **CS-O3 fixes the conversion policy for the imported connectors.** Their
  protocol decoders choose the measured basis in code. Installed profiles
  describe the same basis, with stronger validation for axes, unit and
  rotation form; they are not runtime conversion programs.
- **WS-O2 keeps tracker assignment and the direct solve together.**
  `motionConnectorTracking` owns both generic operations: the solve needs
  assigned tracker regions, while the motion layer owns only the pose it
  returns. The existing tests and dependency boundary support this placement.
- **CC-O4 corrects the mocopi tracking-state claim.** The measured native
  grammar has neither a per-joint state nor confidence. Missing or refused
  bones already become absent rotations with an incomplete-frame diagnostic;
  the reserved `MOCOPI_TRACKING_LOST` code remains unraised.
- **The source move was checked against `usd-vrm-plugins`.** Its clean local
  checkout at `9191fbb` contains none of the imported connector library
  directories or active CMake links to them; remaining names occur in
  historical comments and boundary checks.
- **The imported trace export paths were audited.** Every tool delegates trace
  serialization to `motionRecording`; mocopi and VRChat OSC export from a
  saved capture, while VMC can also hold and export poses during live capture.
  VMC now accumulates poses only when `--export-trace` is requested; removing
  that live semantic path and deciding where offline conversion is invoked
  remain on the current roadmap.

- **The connector-to-motion-stream boundary is settled (CC-O3).** A consumer
  polls `MotionFrame`, routes each actor's pose to a `LiveCaptureSource`, and
  samples it through `IMotionSource`. The VMC connector test now exercises that
  handoff and checks that the source timestamp survives intake.

### Added

- **A release workflow.** `.github/workflows/release.yml` runs on a `vX.Y.Z`
  tag equal to `VERSION`. It builds and tests on the PR lane's three cells,
  then builds, tests and packages each of the seven libraries with
  `ost library`. Each library is pushed per target to
  `ghcr.io/animu-sphere/motion-connectors`, and the workflow drafts one GitHub
  release. The release carries the archives, their manifests, a source
  archive, `SHA256SUMS` and a pin table generated from what was pushed.
  `workflow_dispatch` runs the same lanes as a dry run that pushes nothing.
  `scripts/make_release_notes.py` renders the notes from the changelog and
  `docs/contributing/RELEASE_NOTES_TEMPLATE.md`, and refuses a heading not
  written as `## [X.Y.Z] - YYYY-MM-DD`. `scripts/check_docs.py` fails when
  the workflow's hand-written `ost` pin drifts from `openstrata.ci.yaml`.
- **The recorded device sessions can be replayed against their manifests.**
  `scripts/check_recorded_sessions.py` finds each mocopi and VRChat OSC
  manifest row's capture by sha256 in an operator's directory. It checks the
  row's receive statistics, length census, per-address counts and diagnostics
  through the record tool's `--inspect`, and any saved `.inspect.txt`. For
  mocopi it also checks each session's frames, sender rate and hips path
  through `--export-trace`, and that `motion_connect inspect` delivers the same
  frames. Setting `MOTIONCONNECTORS_RECORDED_SESSIONS` registers it as
  `workspace_recorded_sessions`; CI has no bytes and never runs it. All
  eleven sessions pass on the current tree.
- **`motionConnectorCore` and the first shared-contract VMC and mocopi adapters.** The new
  core provides `IMotionConnector`, `MotionFrame`, timing, actor and tracker
  values, capability descriptors, and the thread-safe bounded
  `Latest`/`Ordered`/`Lossless` frame buffer. `VmcConnector` and
  `MocopiConnector` wrap their existing tested assemblies and expose the same
  frames through non-blocking `Poll`. New focused CTest names:
  `motionConnectorCore_frameBuffer`, `motionConnectorVmc_connector` and
  `motionConnectorMocopi_connector`.

- **`motionConnectorVrchatOsc` and `vrchat_osc_record`, imported from
  `usd-vrm-plugins`' `vrmAdapterVrchatOsc` with their history** (that
  repository's MIG-4, and the last connector it had to send). VRChat OSC
  Trackers decode, the address inventory, tracking-space normalization and
  tracker frames, under `openstrata::connectors::vrchatOsc`, with the recorder
  at `tools/vrchatOscRecord/`. New CTest names:
  `motionConnectorVrchatOsc_unit`, `_trackerMessage`, `_addressInventory`,
  `_trackingSpace`, `_frameAssembler`, `_packetCapture`, `_udpReceiver`,
  `_udpReceiverTruncation`, their corpus readings, `_boundaries`, `_packetGen`,
  and `vrchat_osc_record_inspect`, `_loopback`, `_ipv6`, `_export`.
  - **The edge set is the point: this connector is not a pose source.** The
    library pins `motionCore` alone from `usd-motion-plugins`, for the value
    types the canonical basis is expressed in, beside the two leaves already
    here. A tracker observation is pre-IK, so neither sampling nor recording is
    a library edge — the CLI takes `motionConnectorTracking` for the solve and
    `motionRecording` for the trace it writes.
  - That makes `vrchat_osc_record` the first tool here whose descriptor
    declares a digest-pinned external artifact of its own.
  - **The VRChat OSC -> rig end-to-end leg is deliberately not here**, as
    neither sibling recorder's is. What is here is the export, which is the name
    that calls the humanoid solve: the link the leg was waiting for is
    exercised, the bake is not.
  - The corpus comes with it — 16 generated captures and the recorded-session
    manifests, whose bytes were never redistributable and still are not.

- **`motionConnectorMocopi` and `mocopi_record`, imported from
  `usd-vrm-plugins`' `vrmAdapterMocopi` with their history** (that repository's
  MIG-4). Native mocopi UDP decode -- the container grammar, the packet chunks,
  the joint map and basis change, frame assembly and `mocopi.body.v1` -- under
  `openstrata::connectors::mocopi`, with the recorder at `tools/mocopiRecord/`.
  New CTest names: `motionConnectorMocopi_unit`, `_packetCapture`,
  `_motionPacket`, `_skeletonMap`, `_frameAssembler`, `_liveSource`,
  `_udpReceiver`, `_udpReceiverTruncation`, `_corpus`, `_motionPacketCorpus`,
  `_skeletonMapCorpus`, `_frameAssemblerCorpus`, `_liveSourceCorpus`,
  `_loopbackCorpus`, `_boundaries`, `_packetGen`, and `mocopi_record_inspect`,
  `_loopback`, `_export`, `_ipv6`.
  - **It links the transport leaf and not the wire format**, which is the one
    structural difference from the sibling connector: this protocol is not OSC,
    and WORKSPACE.md §2 forbids reaching another protocol's decoder. The three
    `usd-motion-plugins` packages are the same three the sibling pins, by the
    same digests from v0.5.0's pin table.
  - The C++ namespace of a leaf is not its package name: the transport
    library's is `transport`, so a `using` that named `motionConnectorTransport`
    compiled nowhere. Measured, not predicted -- it is what the first build of
    this import failed on.
  - Nine committed captures come with it, and they stay byte-compared:
    `.gitattributes` already carried the `*.mocopipackets binary` rule the
    sibling's import declared for this one.
  - **The mocopi -> rig end-to-end leg is deliberately not here**, for the
    reason `vmc_record`'s was not: its last step bakes a session onto an avatar
    with a retarget CLI, and §2.3 points every edge at `usd-motion-plugins` and
    none at a consumer.
  - `MOTIONCONNECTORMOCOPI_BUILD_TOOL` did not travel. The option existed to
    keep a library's install free of its CLI's edges; here the recorder is a
    workspace member of its own and the installed-consumer lane answers that
    question instead.

- **`motionConnectorVmc` and `vmc_record`, imported from `usd-vrm-plugins`'
  `vrmAdapterVmc` with their history** (that repository's MIG-4). VMC Protocol
  decode, frame assembly and `vmc.v1`, under `openstrata::connectors::vmc`,
  with the recorder at `tools/vmcRecord/` — the root `tools/`, where
  [WORKSPACE.md §2.1](docs/architecture/WORKSPACE.md#21-inside-the-repository)'s
  diagram puts every tool. New CTest names: `motionConnectorVmc_vmcMessage`,
  `_frameAssembler`, `_liveSource`, `_skeletonMap`, `_oscPacket`,
  `_packetCapture`, `_udpReceiver`, `_corpus`, `_boundaries`, and
  `vmc_record_inspect`, `_loopback`.
  - **What was one `motionRuntime` is two packages here.** The frame assembler
    reads `motionSampling`'s source interface and the live source writes
    `motionRecording`'s capture trace, so the descriptor pins both beside
    `motionCore` — three digest-pinned artifacts and the two leaves already
    here.
  - The pose's provenance changed shape with the move, not meaning: an absent
    optional said "this stamped nothing" and the default `SourceMetadata` says
    it now.
  - **The VMC → rig end-to-end leg is deliberately not here.** Its last step
    bakes a session onto an avatar with a retarget CLI, and this repository has
    neither and will have neither — §2.3 points every edge at
    `usd-motion-plugins` and none at a consumer. The leg stays in
    `usd-vrm-plugins`, as `motion_record_replay`'s bake did.
  - `cmake/MotionConnectorsUtf8CodePage.cmake` arrives with the recorder: a
    Windows CLI handed a non-ASCII path needs its process code page to be
    UTF-8, or it passes OpenUSD bytes it cannot decode.
  - `.gitattributes` gains `*.trace binary`, beside the `*.vmcpackets` and
    `*.mocopipackets` rules already here. A byte-compare fixture must not be
    normalized on a Windows checkout, and `binary` is what says so: a later
    `text eol=lf` rule overrides it and puts CRLF → LF back on `git add`, which
    strips a 0x0D out of any datagram carrying the pair 0x0D 0x0A.

### Fixed

- **The imported boundary check had a hole, and it was measured.** Its
  forbidden-neighbour rule asked for `mocopi`, so a symbol named
  `motionConnectorMocopiProbe` — word characters on both sides — walked
  straight through it. A forbidden name matches as a prefix now, and the
  mutation that exposed it fails the check by file.

- **`motionConnectorTracking`, imported from `usd-vrm-plugins`' `motionTracking`
  with its history** (that repository's MIG-4). Tracker regions, assignment and
  the tracker solve, under `openstrata::connectors::tracking`. New CTest names:
  `motionConnectorTracking_trackerAssignment`, `_trackerSolve`, `_boundaries`.
  - **It is the first member here to consume `usd-motion-plugins`**, and the
    first declared cross-repository edge in this repository: its
    `requires.libraries` pins `motionCore >=0.5,<0.6` by archive digest per
    target, with the `oci://` source from that release's generated pin table.
    In `usd-vrm-plugins` the same edge existed in CMake and in no descriptor,
    so the declaration is a correction as well as a move.
  - One assertion changed meaning rather than spelling: the solve stamps no
    provenance, which used to be an absent optional and is now the default
    `SourceMetadata`, because the consumed `motionCore` makes a pose's
    `metadata` always present.
  - The boundary check is rewritten for this repository's edges: it refuses
    every sibling connector and every `usd-motion-plugins` library except
    `motionCore`. Its exception for this library's own name ends in
    `(?![A-Za-z0-9])` rather than ``, because under `IGNORECASE` a word
    boundary lets `MOTIONCONNECTORTRACKING_API` through it.

### Fixed

- **`scripts/check_docs.py` no longer reads an external pin as a sibling's
  version.** Every required range had to admit this repository's `VERSION`,
  which is right for a sibling and wrong for a library from another
  repository: `motionCore >=0.5,<0.6` is `usd-motion-plugins`' version while
  this workspace is 0.1.0, and both are correct. Ranges inside a dependency
  carrying an `artifact:` pin are skipped; sibling ranges are checked exactly
  as before, which a mutation confirms.

- **`motionConnectorTransport`, imported from `usd-vrm-plugins`' `liveTransport`
  with its history** (that repository's MIG-4, its first identity). The UDP
  receiver, the opt-in datagram queue, the packet-capture file format and the
  diagnostic vehicle, unchanged in behaviour, under
  `openstrata::connectors::transport`. Its edge set is empty, and its boundary
  check refuses this repository's other libraries, every `usd-motion-plugins`
  library, producer names, address literals and diagnostic codes. New CTest
  names: `motionConnectorTransport_pollTimeout`, `_diagnostics`,
  `_packetCapture`, `_boundaries`. The package is the installed-consumer lane's
  first row.
- **`motionConnectorOsc`, imported from `usd-vrm-plugins`' `osc` with its
  history** (MIG-4, its second identity). The OSC 1.0 wire format, unchanged
  in behaviour, under `openstrata::connectors::osc`. Its edge set is empty, the
  transport's included, and its boundary check refuses address literals in its
  tests as well as its sources. New CTest names: `motionConnectorOsc_oscPacket`,
  `_boundaries`. The package is the installed-consumer lane's second row.
- **The rendered `ost` workflow and the graph cell**, which the scaffold could
  not carry: the first member is what lets `ost` 0.22.10's graph step pass.
  Under the 0.23.0 pin below an explicitly empty workspace renders too, so the
  next repository to start empty does not repeat it.

### Changed

- **The CMake tree is split by responsibility.** The root `CMakeLists.txt`
  composes the workspace and nothing else; shared policy lives in
  `cmake/MotionConnectorsProject.cmake` (the `VERSION` read, the C++ baseline,
  each component's `<NAME>_BUILD_TESTS` default and `enable_testing()`
  ownership, the test interpreter) and `cmake/MotionConnectorsPackage.cmake`
  (`motionconnectors_install_package()`: install, export, config and version
  files). Every `add_library`, `find_package` and `target_link_libraries` stays
  in its component, where the boundary checks read it. Exported target names,
  namespaces and install locations are unchanged.
  - **C++20 everywhere.** Each target now requires `cxx_std_20`; the components
    used to ask for `cxx_std_17` under a root that set C++20.
  - **Every package is `SameMinorVersion`.** The connectors wrote
    `SameMajorVersion`, which let a pre-1.0 0.2 satisfy a 0.1 consumer.
  - **Connector options.** `MOTIONCONNECTORS_BUILD_VMC`, `_MOCOPI` and
    `_VRCHAT_OSC` (default ON) and `MOTIONCONNECTORS_BUILD_TOOLS` (default ON at
    the top level). With every connector off, configure asks for no OpenUSD and
    builds the transport and the wire format alone. `motion_connect` is built
    only when all three connectors are. The installed-consumer lane checks the
    packages the build actually installed (`--package`).
  - **No build output in the source tree.** The tools no longer write their
    executables to `tools/*/bin/`; they stay in the build tree, `ost` (0.23.5
    and later) stages them from there, and `cmake --install` is the runtime
    layout. A stale
    `tools/*/bin/` from an older build can be deleted.
  - **Single-config builds default to Release, as intended.** The old check
    ran after `project()`, where MSVC had already set `CMAKE_BUILD_TYPE` to
    Debug, so a plain Ninja configure on Windows linked the debug CRT against
    the Release-only OpenUSD and motionCore (LNK2038). `ost` always passed
    Release, so no CI lane saw it.
  - The tool projects are named after their directories (`vmcRecord`,
    `mocopiRecord`, `vrchatOscRecord`, `motionConnect`), which keeps their test
    options' existing names. Each tool's test option now defaults from
    `MOTIONCONNECTORS_BUILD_TESTS` like every other component, not from its
    connector's option.

- **The `ost` pin is 0.23.6.** 0.23.5 stages bundle and tool outputs per
  target instead of in the source tree (`usd-mmd-plugins`' ost report 01):
  `ost build` stages each tool from the build tree into its member's
  `.strata/targets/<target>/tool-stage/bin`. It also exposes a published
  bundle's or tool's pinned paths to the root CMake suites (`usd-vrm-plugins`'
  ost report 46), which nothing here pins yet. 0.23.6 fixes 0.23.5's
  workspace bundle packaging (report 47); this repository ships no bundle.
  Re-pinned with the ecosystem.

- **The `ost` pin was 0.23.4.** 0.23.4 applies the runtime check to
  `ost library build` and `ost plugin build` as well, so a member build tree
  configured against another runtime is discarded there too. It also adds pins
  for a published bundle and a published test tool (`usd-vrm-plugins`' ost
  report 45), which nothing here uses yet. Re-pinned with the ecosystem.

- **The `ost` pin was 0.23.3.** 0.23.3 pulls, graphs and validates an
  external library artifact only a tool declares, as `vrchat_osc_record`'s
  descriptor does, and discards a build tree whose cache was configured
  against another runtime (`usd-vrm-plugins`' ost report 44). Re-pinned with
  the ecosystem.

- **The `ost` pin is 0.23.2**, re-pinned across the ecosystem together with
  `usd-motion-plugins` and `usd-vrm-plugins`, and the workflow re-rendered from
  it. `requires.libraries` can name a digest-pinned library artifact from
  another repository now, and every rendered job runs `ost library pull` before
  it builds. This repository's remaining imports — `motionTracking` and the
  three adapters — all link `usd-motion-plugins`' `motionCore`, so that edge is
  what they were waiting for (`usd-vrm-plugins`' ost report 41). The pinned
  runtime leaves do not move.

  0.23.1 and not 0.23.0: 0.23.0's new `consumer-link` claim probed a
  materialized runtime before the relocation `ost configure` and
  `ost plugin build` apply to that same prefix, so this repository's hosted
  Linux and Windows lanes went red on the pin bump alone. Measured, reported
  as `usd-vrm-plugins`' ost report 42 and fixed upstream the same day.

  0.23.2 then came out of **this repository's own first import**: the root
  `ost build` did not compose the external library artifacts a member
  declares, so `motionConnectorTracking` built through `ost library build` and
  the workspace could not configure. Reported as ost report 43 and fixed
  upstream; this repository is the first that needed it.

- **The documentation baseline**, with no code:
  - the design policy, accepted on 2026-09-19. Its §46 records how it was
    reconciled with `usd-motion-plugins`, `usd-vrm-plugins` and
    `usd-mmd-plugins` on adoption: the pose is `usd-motion-plugins`'
    `MotionPose`, the basis has a +Z forward axis, the first connectors are
    imported from `usd-vrm-plugins`, and a tracker observation is not a pose;
  - three proposed contracts: `CONNECTOR_CONTRACT.md`,
    `COORDINATE_SYSTEMS.md` and `SOURCE_PROFILES.md`;
  - the workspace contract and the external dependencies, with every identity
    reserved and its source named;
  - the capability matrix and the diagnostics catalog, both empty;
  - the roadmap, which maps releases v0.1.0–v0.4.0 onto Connector Phase 1–8
    and onto `usd-vrm-plugins`' MIG-4 imports.
- **The scaffold, with no component in it.**
  - WS-O1 is decided. Native libraries go under `libs/`, not `src/`. A
    lower-camel identity is also the library's CMake package and exported
    target (`motionConnectorVmc::motionConnectorVmc`), CLI commands are
    snake_case (`motion_connect`, `vmc_record`), and the C++ namespace is
    `openstrata::connectors`. That is the siblings' discipline, recorded as
    design policy §46.9.
  - WS-O7 is decided. Every live input `usd-vrm-plugins` holds arrives in
    v0.1.0, one identity per change: `liveTransport`, `osc`, `motionTracking`,
    the three adapters and their record tools. So mocopi and VRChat OSC
    Trackers move from v0.2.0 to v0.1.0 (design policy §46.7, the roadmap
    table, the capability matrix).
  - The root project reads `VERSION` (0.1.0) and builds as C++20.
    `cmake/MotionConnectorsOpenUsd.cmake` refuses any OpenUSD but 26.08.
    `CMakePresets.json` covers plain CMake, and `openstrata.toml` and
    `openstrata.ci.yaml` carry the siblings' three runtime digests and `ost`
    0.22.10.
  - `scripts/check_docs.py` checks links, anchors and every version and pin
    mirror, both in CTest and in the hand-written `docs-check` workflow.
  - `workspace_installed_consumer` installs the tree into a clean prefix and
    builds a consumer copied outside the repository against every package in
    `tests/installed_consumer/packages.json`. The list is empty until the
    first import.
  - The community files (with the network-exposure rules in `SECURITY.md`),
    `THIRD_PARTY_NOTICES.md`, and a building guide.
  - The rendered `ost` workflow is **not** included, because `ost` 0.22.10
    refuses a workspace graph with no member
    ([roadmap](docs/roadmap/current.md)).
