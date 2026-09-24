# Coordinate systems

> Status: **proposed**, 2026-09-19. The canonical basis and the conversion
> rules are `usd-motion-plugins`' `MOTION_CONTRACT.md` §3, carried rather than
> re-derived; this document adds what is a connector's: how a source's basis is
> declared, who converts it, and what each known source was measured to be.
> The rules are proposed. The known-source rows include measured evidence from
> the imported source implementations; the capability matrix states whether a
> source is integrated with the shared connector contract.
>
> This document owns source bases and their conversion. On that area it wins
> over [DESIGN_POLICY.md](DESIGN_POLICY.md) §5.1 ("Required behavior") and §6.
> Section numbers are stable; open questions are `CS-O<n>` and never reused.

---

## 1. The canonical basis

**Right-handed, +Y up, +Z forward, metres, seconds**, rotations as unit
quaternions, joint rotations local to the semantic parent, root motion separate
from the hips.

This is `usd-motion-plugins`' canonical basis
([DESIGN_POLICY.md §46.2](DESIGN_POLICY.md#462-the-canonical-basis-includes-a-forward-axis)),
and it agrees with the ecosystem's stage convention (`upAxis = Y`,
`metersPerUnit = 1`). The forward axis was recorded, not chosen: the avatars
motion is retargeted onto face +Z by their own specification, and anything else
already shows as a character walking backwards.

## 2. Conversion happens once, in the connector

Design policy Rule 2: source-specific coordinate systems end at the connector
boundary. Nothing downstream knows whether a pose came from mocopi, VMC or
OpenXR, so nothing downstream can correct a basis — the only place that knows
the source is the only place that can convert it.

A connector states its conversion as a **signed permutation** `M` of the axes
(plus a unit scale), whose determinant is the handedness:

- a position converts as `s · M v`;
- a rotation converts as `(w, det(M) · M v)` — the vector part is mirrored
  with the space;
- **Euler angles compose into a quaternion by the right-hand rule, always**,
  from the raw numbers and without reference to the source's handedness;
  applying handedness in the composition *and* in `M` is correct in every
  axis-aligned test and wrong the moment anything turns;
- a named Euler order composes **intrinsically**: `ZXY` is `qZ * qX * qY`;
- a quaternion's component order (scalar-first or scalar-last) is part of the
  declaration, not a guess.

These rules are `usd-motion-plugins`' motion contract §3. `CS-O1` selected
its `motionCore/BasisConversion.h` as the home for signed-permutation
arithmetic on 2026-09-24. Each connector chooses its source basis and applies
the operation at its boundary; the shared core never guesses a sender's axes.

## 3. What a connector declares

Every connector, in its source profile ([SOURCE_PROFILES.md](SOURCE_PROFILES.md)),
states all of these — the design policy's §5.1 list, made concrete:

| Field | Values |
| --- | --- |
| Handedness | right / left |
| Up axis | ±X, ±Y, ±Z |
| Forward axis | ±X, ±Y, ±Z |
| Length unit | metres per source unit |
| Rotation form | quaternion (component order), Euler (order, degrees or radians), matrix |
| Transform space | local to parent, or world / tracking space |
| Timestamp origin | the source clock ([CONNECTOR_CONTRACT.md §6](CONNECTOR_CONTRACT.md#6-time)) |
| Confidence semantics | none, per joint, per frame; range and meaning |
| Evidence | *measured* (a report), *documented* (a vendor page), or *assumed* |

**A basis taken from documentation is a claim, not a fact.** `usd-vrm-plugins`
took one adapter's handedness from documentation once; a mirrored conversion
passes a unit suite, a corpus and a review, and shows up only when a person
turns their head. A basis is *measured* only from a **labelled** session — an
operator turning a known way, recorded — and until then the profile says
*documented* or *assumed*.

## 4. Testing a conversion

Conversions are tested **physically**: a direction is rotated by the converted
quaternion and compared with where it must end up — a head turned to the performer's left must face the
performer's left in canonical space. A component-by-component
test agrees with a mirrored implementation as readily as with a correct one.

Every connector's test suite includes, at least: identity, a yaw, a pitch and
a roll of the root and of one limb, each in both directions, from its generated
corpus.

## 5. Known sources

What is known about each source this repository plans, and how it is known.
*Measured* rows cite `usd-vrm-plugins`' evidence, which moves here with the
connector that produced it.

| Source | Handedness | Up | Forward | Unit | Rotation | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| mocopi native UDP | right | +Y | +Z | m | quaternion, scalar-last | **measured** 2026-08-12; the change of basis is the identity ([adapter plan §6](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/roadmap/adapters-mocopi-vmc-ardy.md)) |
| VMC Protocol (Unity senders) | left | +Y | +Z | m | quaternion | **measured**: flip X alone ([motion contract, canonical basis](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/MOTION_CONTRACT.md)); which of two translation channels is body translation is open (`CS-O2`) |
| VRChat OSC Trackers | left, +X is the body's right | +Y | +Z | m | Euler, degrees | **measured** 2026-08-30 against a labelled session, agreeing with VRChat's documentation ([report `motion/03`](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/reports/motion/03-2026-08-30-vrchat-osc-tracking-space.md)) |
| MediaPipe (pose, hands, face) | — | — | — | normalized image or metric world landmarks | positions, not rotations | not yet; see [CONNECTOR_CONTRACT.md](CONNECTOR_CONTRACT.md) CC-O2 |
| WebXR | right | +Y | −Z | m | quaternion | documented only |
| OpenXR | right | +Y | −Z | m | quaternion | documented only |

A source whose forward is −Z (WebXR, OpenXR) reaches canonical with a rotation
of 180° about Y, determinant +1 — no mirroring. That is a documented
expectation until a labelled session measures it.

## 6. Open questions

CS-O1's placement was decided on 2026-09-24: `motionCore` owns
`SignedPermutationBasis`, `IsValidBasis`, `ApplyBasisToPosition` and
`ApplyBasisToRotation` in its unreleased 0.5.2 source. The recorded
`motionSource` converter calls it. VMC and VRChat OSC migrate after that
package is published and their digest pins are updated. `motionConnectorCore`
cannot serve the recorded BVH path. Euler composition remains source-specific.

CS-O3 was decided on 2026-09-24: a connector for a fixed wire protocol keeps
its measured basis and rotation decoding in code. Its installed JSON profile
declares the same facts for inspection and validation; it is not loaded as
executable conversion policy at runtime ([SOURCE_PROFILES §5](SOURCE_PROFILES.md#5-where-profiles-live)).
The profile check pins handedness, up, forward, unit and rotation form against
the conversion tests. A recorded reader such as BVH still takes a variable
basis from its producer profile, because different files can name different
producers. Both call the same `motionCore` arithmetic when its new package is
available to connectors.

| Id | Question | Resolve by |
| --- | --- | --- |
| CS-O2 | VMC senders have two candidate translation channels (root position, hips offset). Which is body translation is a fact about each sender, measured per sender (`usd-motion-plugins` MC-O3, "operator work in `motion-connectors`") | one recorded session from each of two VMC senders |
