# Changelog

All notable changes to `motion-connectors` are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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
