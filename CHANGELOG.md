# Changelog

All notable changes to `motion-connectors` are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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

- **The `ost` pin is 0.23.0**, re-pinned across the ecosystem together with
  `usd-motion-plugins` and `usd-vrm-plugins`, and the workflow re-rendered from
  it. `requires.libraries` can name a digest-pinned library artifact from
  another repository now, and every rendered job runs `ost library pull` before
  it builds. This repository's remaining imports — `motionTracking` and the
  three adapters — all link `usd-motion-plugins`' `motionCore`, so that edge is
  what they were waiting for (`usd-vrm-plugins`' ost report 41). The pinned
  runtime leaves do not move.

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
