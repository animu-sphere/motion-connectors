# motion-connectors documentation

Documentation is organized by responsibility, and each category answers one
class of question. The layout is the one `usd-motion-plugins`,
`usd-vrm-plugins` and `usd-mmd-plugins` use, so the repositories read the same
way.

The [capability matrix](reference/CAPABILITY_MATRIX.md) is the sole source of
truth for what is currently implemented. Other documents describe intent,
structure, evidence or incomplete work according to the ownership table below.

| Category | Answers | Start here |
| --- | --- | --- |
| [design/](design/) | What a connector is, where it stops, and why. | [DESIGN_POLICY.md](design/DESIGN_POLICY.md) |
| [architecture/](architecture/) | Component identities, dependency directions inside and across repositories, external dependencies. | [WORKSPACE.md](architecture/WORKSPACE.md) · [DEPENDENCIES.md](architecture/DEPENDENCIES.md) |
| [reference/](reference/) | Facts about the current tree: what is implemented, which diagnostics exist. | [CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) · [DIAGNOSTICS.md](reference/DIAGNOSTICS.md) |
| [roadmap/](roadmap/) | What is planned next (incomplete work only), and which release carries it. | [README.md](roadmap/README.md) · [current.md](roadmap/current.md) |
| [guides/](guides/) | How to build and test the tree. | [building.md](guides/building.md) |
| [contributing/](contributing/) | How to maintain these documents. | [documentation.md](contributing/documentation.md) |

## Canonical documents

- [design/DESIGN_POLICY.md](design/DESIGN_POLICY.md) is the **design policy**.
  It covers the repository boundary, the ecosystem split, security, design
  rules and the decisions made when the policy was adopted. Release scope and
  ordering live in [roadmap/](roadmap/), not here. Sibling repositories cite
  the policy by section number.
- Three focused contracts own one area each, and on that area they win over
  the design policy:
  - [design/CONNECTOR_CONTRACT.md](design/CONNECTOR_CONTRACT.md) covers the
    connector interface, `MotionFrame`, tracker observations, state, time,
    frame assembly, buffering, diagnostics, untrusted input and actors;
  - [design/COORDINATE_SYSTEMS.md](design/COORDINATE_SYSTEMS.md) covers the
    canonical basis, conversion rules, and each source's declared and
    measured basis;
  - [design/SOURCE_PROFILES.md](design/SOURCE_PROFILES.md) covers profile
    identifiers, what a profile declares, and joint naming.
- The motion values themselves (`MotionPose`, `RootMotion`,
  `MotionChannelSet`, `SourceMetadata` and the `MotionStream` intake) are
  owned by `usd-motion-plugins`' `MOTION_CONTRACT.md`. This repository does
  not redefine them.
- [architecture/WORKSPACE.md](architecture/WORKSPACE.md) is the binding
  **workspace contract**. Structural changes go there first, in their own pull
  request.

## Source-of-truth rules

- Code is authoritative for implemented behaviour; `architecture/` and
  `reference/` record it and change with it.
- `design/` defines intended contracts and labels what is not implemented.
- The capability matrix is the only document that states current capability
  status; other pages link to it instead of copying a status snapshot.
- Which release carries what is stated only in the
  [roadmap status table](roadmap/README.md#status-at-a-glance).
- The details are in [contributing/documentation.md](contributing/documentation.md).
