# Changelog

All notable changes to `motion-connectors` are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **The connector-to-motion-stream boundary is settled (CC-O3).** A consumer
  polls `MotionFrame`, routes each actor's pose to a `LiveCaptureSource`, and
  samples it through `IMotionSource`. The VMC connector test now exercises that
  handoff and checks that the source timestamp survives intake.

### Added

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
