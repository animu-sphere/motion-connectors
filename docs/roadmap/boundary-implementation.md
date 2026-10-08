# Generic tracking solve migration

This page holds the remaining type-contract and migration work under
[DESIGN_POLICY §47.5.1](../design/DESIGN_POLICY.md#4751-tracking-ownership-review).
Current structure and dependency enforcement belong to
[WORKSPACE §2](../architecture/WORKSPACE.md#2-dependency-directions);
acquisition and test evidence belong to the
[capability matrix](../reference/CAPABILITY_MATRIX.md). Historical Boundary
Phases A–E are recorded in the [changelog](../../CHANGELOG.md).

## Prerequisites

Retain the direct solve in `motionConnectorTracking` until these prerequisites
are adopted in `usd-motion-plugins` and consumer composition:

- Define a motion-owned semantic input, its component identity and dependency
  edges, without connector observations, tracker identities or region types.
- Specify sparse position/orientation availability, confidence handling,
  validation, comparison and recording implications. Do not treat MediaPipe
  position-only landmarks as a rig the orientation solve can reconstruct.
- Adopt structural contract changes in their own PR before moving code.
- Add consumer-owned adaptation from installed acquisition/assignment packages
  to the motion input, retaining acquisition metadata outside the solve.
- Prove parity for current direct orientation composition, hips/root handling,
  partial rigs, silent ancestors, unused positions and refusal behavior.

## Migration gate

Only after motion-owner algorithm tests and installed consumer parity pass,
move the direct solve and remove the legacy input projection. Keep acquisition,
regions, operator assignment and identity applicability here; introduce no
reverse dependency and no copied observation contract. Coordinate with
`usd-motion-plugins`' MC-O8. The acquisition C ABI can use the core observation
boundary independently of this migration
([current.md](current.md#v020-execution-order)).
