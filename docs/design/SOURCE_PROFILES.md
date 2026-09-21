# Source profiles and joint naming

> Status: **adopted**, 2026-09-21. The profile contract, identifiers and the
> installed JSON representation are defined here. The three v0.1.0 connector
> profiles are installed beside their libraries and checked by the workspace
> profile test and the installed-consumer lane.
>
> This document owns what a source *is*: its profile identifier, what the
> profile declares, and how a source's joint names become the shared
> vocabulary. On that area it wins over [DESIGN_POLICY.md](DESIGN_POLICY.md)
> §7 and §8. The vocabulary itself — `HumanJoint` version 1 — is
> `usd-motion-plugins`' `MOTION_CONTRACT.md` §2's. Section numbers are stable;
> open questions are `SP-O<n>` and never reused.

---

## 1. Why profiles

A source's behaviour — its joint set, hierarchy, basis, confidence semantics
and capabilities — is described **once, as data about the source**, not
spread through code that branches on its name. Downstream code never branches
on a profile: provenance is recorded and never a branch condition
(`usd-motion-plugins` policy §4.1, and this policy's Rule 1). A profile is how
a person, a test and a diagnostic find out what a source said it was.

## 2. Identifiers

```text
<source>.<part>.v<major>
```

`<source>` is the protocol or product, lower-case; `<part>` is present when one
source has several independent streams; `v<major>` changes when a profile's
meaning changes, never for an additive field.

| Profile | Source | Part | Connector |
| --- | --- | --- | --- |
| `vmc.v1` | VMC Protocol | body, root, blend shapes | `motionConnectorVmc` |
| `mocopi.body.v1` | mocopi native UDP | body | `motionConnectorMocopi` |
| `vrchat-osc.trackers.v1` | VRChat OSC Trackers | trackers | `motionConnectorVrchatOsc` |
| `mediapipe.body.v1` | MediaPipe Pose | body landmarks | `motionConnectorMediaPipe` |
| `mediapipe.hands.v1` | MediaPipe Hands | hand landmarks | `motionConnectorMediaPipe` |
| `mediapipe.face.v1` | MediaPipe Face | blend shapes | `motionConnectorMediaPipe` |
| `webxr.hand.v1` | WebXR Hand Input | hand joints | `motionConnectorWebXR` |
| `webxr.viewer.v1` | WebXR | head, controllers | `motionConnectorWebXR` |
| `openxr.hand.v1` | OpenXR `XR_EXT_hand_tracking` | hand joints | `motionConnectorOpenXR` |

The table reserves names; a profile exists when its connector lands, and the
[capability matrix](../reference/CAPABILITY_MATRIX.md) says when. Sender
applications and relay identities belong in provenance, not in the profile
identifier, so the three v0.1.0 IDs are the names above.

## 3. What a profile declares

| Field | Meaning |
| --- | --- |
| id | §2 |
| joint set | every source joint name the profile can report, verbatim |
| hierarchy | each source joint's parent, if the source has one |
| joint map | source joint → shared `HumanJoint`, or *unmapped* (§4) |
| basis | the [COORDINATE_SYSTEMS.md §3](COORDINATE_SYSTEMS.md#3-what-a-connector-declares) fields, with their evidence |
| observation kind | pose (rotations), trackers, landmarks (positions), channels |
| confidence | none, per joint, per frame; range and meaning |
| capabilities | the [CONNECTOR_CONTRACT.md](CONNECTOR_CONTRACT.md) capability set this profile can fill |
| clock | the source clock's domain and unit |

## 4. Joint naming: two stages

```text
source joint names           (VMC "LeftUpperArm", mocopi index 11, OpenXR XR_HAND_JOINT_INDEX_TIP_EXT)
      │  the connector, through its profile's joint map
      ▼
shared HumanJoint             (leftUpperArm, leftIndexDistal, …)
      │  usd-motion-plugins retargeting, through a RetargetMap
      ▼
target avatar joints          (a VRM node, a PMX bone, a UsdSkel joint)
```

- **The connector maps source names to the shared vocabulary and stops.** It
  never maps to a target skeleton's joints or indices: `VMC bone name →
  /Asset/skel/Skeleton joint index` is forbidden, `VMC bone name →
  HumanJoint → canonical pose` is required (measured as a rule:
  `usd-vrm-plugins` adapter plan §5.1).
- **Raw names do not leak past the connector.** A source joint that has no
  shared joint is *unmapped*: it is dropped from the pose and counted in the
  connector's diagnostics, never passed on under its raw name. Whether some
  unmapped joints need to travel anyway is `CC-O1`.
- **The shared vocabulary is `HumanJoint` version 1**, the 55-joint humanoid
  (torso and head, legs, arms, three joints per finger), with lower-camel
  names. The design policy §7 list is a subset of it. Its extension — string
  identifiers, non-humanoid rigs — is `usd-motion-plugins`' MC-O1 and this
  repository's `CC-O1`, decided upstream.
- **A tracker is not a joint** ([CONNECTOR_CONTRACT.md §4](CONNECTOR_CONTRACT.md#4-trackerobservation)).
  A tracker profile names trackers, and has no joint map.
- **Channel names are verbatim and namespaced** (`vmc:Joy`, `arkit:jawOpen`).
  Resolving a producer's `Joy` to a rig's `happy` needs the rig, and belongs to
  the avatar-format repository.

## 5. Where profiles live

`usd-motion-plugins` keeps its producer profiles for recorded files as
installed, declarative data (`profiles/motion/`), so that a BVH reader never
names a producer in code. Connector profiles follow the same rule: one JSON data
file per profile, installed under `share/motion-connectors/profiles/`. The
connector's runtime contract still carries the profile ID, while the profile
data is the declarative source description consumed by tools and validation;
the connector does not branch on profile contents.

The representation is `openstrata.motion.source-profile/v1`. It uses the
fields in §3, with `jointSet` entries carrying `source`, `parent` and
`humanJoint` (or `null` for an intentional unmapped joint). Numbered sources
add `sourceIndex`; tracker profiles use `trackers` and never invent a joint
map. A profile declares its basis evidence, confidence, capabilities and clock
even when the value is explicitly `none`.

## 6. Open questions

SP-O1 and SP-O2 are resolved by the installed profiles above:

| Id | Decision | Resolved |
| --- | --- | --- |
| SP-O1 | Use `<source>.<part>.v<major>`; `<part>` is optional, and sender applications and relays remain provenance rather than profile identity. | 2026-09-21 |
| SP-O2 | Use one installed JSON data file per connector-owned profile under `share/motion-connectors/profiles/`, with `openstrata.motion.source-profile/v1`. | 2026-09-21 |

The remaining profile question is postponed with the source that needs it:

| Id | Question | Resolve by |
| --- | --- | --- |
| SP-O3 | Landmark profiles (MediaPipe): a profile whose observation kind is positions has no rotation joint map; what its "joint map" is depends on CC-O2 | Connector Phase 4 |
