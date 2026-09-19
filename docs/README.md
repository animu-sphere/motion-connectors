# motion-connectors documentation

Documentation is organized by responsibility, and each category answers one
class of question. The layout is the one `usd-motion-plugins`,
`usd-vrm-plugins` and `usd-mmd-plugins` use, so the repositories read the same
way.

**The tree holds documentation only (2026-09-19).** The design policy is
accepted and the focused contracts are proposed. The first connectors arrive
from `usd-vrm-plugins`, where they are implemented and measured today.
[reference/](reference/) is the only place that says what is implemented
here. So far, nothing is.

| Category | Answers | Start here |
| --- | --- | --- |
| [design/](design/) | What a connector is, where it stops, and why. | [DESIGN_POLICY.md](design/DESIGN_POLICY.md) |
| [architecture/](architecture/) | Component identities, dependency directions inside and across repositories, external dependencies. | [WORKSPACE.md](architecture/WORKSPACE.md) · [DEPENDENCIES.md](architecture/DEPENDENCIES.md) |
| [reference/](reference/) | Facts about the current tree: what is implemented, which diagnostics exist. | [CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) · [DIAGNOSTICS.md](reference/DIAGNOSTICS.md) |
| [roadmap/](roadmap/) | What is planned next (incomplete work only), and which release carries it. | [README.md](roadmap/README.md) · [current.md](roadmap/current.md) |
| [contributing/](contributing/) | How to maintain these documents. | [documentation.md](contributing/documentation.md) |

`guides/`, `releases/` and `reports/` are each created with their first real
content. `guides/` comes with the first build guide, alongside the scaffold;
`releases/` with the first release record, at the first tag; `reports/` with
the first dated session, recorded here or imported with a connector.

## Canonical documents

- [design/DESIGN_POLICY.md](design/DESIGN_POLICY.md) is the **design policy**.
  It covers what the repository is for and not for; the boundaries with
  `usd-motion-plugins`, the avatar-format repositories and the runtime
  (§20–§24); security (§27); the release plan (§30–§34); the design rules
  (§42); and how the policy was reconciled with the sibling contracts on
  adoption (§46). Sibling repositories cite it by section number.
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
- Which release carries what is stated only in the
  [roadmap status table](roadmap/README.md#status-at-a-glance).
- The details are in [contributing/documentation.md](contributing/documentation.md).
