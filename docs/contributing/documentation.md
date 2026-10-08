# Documentation guidelines

Documentation is part of the implementation contract. A change is incomplete
if it changes one of the following without updating the page that owns it:

- a public boundary;
- a frame's or a profile's meaning;
- a source's declared basis;
- repository architecture;
- capability or release availability.

## Category ownership

| Category | Put this here | Not this |
| --- | --- | --- |
| `architecture/` | Component identities, dependency edges, layout, build modes, external dependencies: the binding structural contract, without a duplicated implementation-status column. | Rationale; unimplemented components described as present. |
| `design/` | Intended contracts, their rationale, the evidence they rest on, and open questions. | Implementation progress; release scheduling (planned / next / later). |
| `reference/` | Facts about the current tree: capabilities, diagnostics. | Release scheduling; status snapshots copied from another repository. |
| `roadmap/` | Incomplete, ordered work, its completion criteria, and which release carries it. | Completed work; rationale. |
| `guides/` | How to accomplish a task, with commands that have been run. | Commands nobody has run. |
| `releases/` | One immutable record per released version. | Work in progress. |
| `reports/` | Dated evidence from real runs and real sessions; append-only. | Current-state claims. |
| `archive/` | Plans and documents that were once authoritative and no longer are, each opening with a *Historical only* banner. | Anything a reader should act on. |
| `contributing/` | How to maintain this repository. | End-user tasks. |

## Status rules

- Only [CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md) declares
  supported, approximated, unsupported, implemented, tested and release
  availability. A supported row must name test evidence; hardware-only
  evidence does not establish deterministic support.
- Design documents record accepted decisions, rationale, invariants and open
  questions. Contract adoption dates are decision history, not dated progress
  snapshots. Link to the matrix for conformance; put scheduling in the roadmap.
- Architecture owns structure, component identity and dependency direction.
  A reserved layout is a design allocation, not an implemented directory.
- Roadmaps contain only unfinished tasks and completion criteria. Remove a
  finished item in the same PR; do not leave checked boxes, done markers or
  completed phases as a history log.
- README pages explain purpose, inventory and usage. Link to canonical status
  instead of copying connector support tables, implementation checklists or
  release availability.
- Do not use dated prose as a status source or copy dependency maps, open
  question tables or release scope into multiple pages. Link to the owner.
- Release records and dated reports are frozen history. Later findings get a
  new report or a short forward note; they are not rewritten as current status.

## Cross-repository contracts

**One concept, one owning repository, one canonical document.** This
repository owns how external motion *enters*; `usd-motion-plugins` owns what
generic motion *is*; each avatar repository owns how its format *uses* it.

> A repository may describe how it consumes a sibling repository's contract,
> but must not redefine that contract.
>
> Link to the owning repository instead of copying its API semantics,
> capability status, roadmap, or implementation state.

Good: "`MotionFrame` carries `usd-motion-plugins`' `MotionPose`, whose
semantics are its MOTION_CONTRACT.md." Bad: a definition of what `MotionPose`
contains, maintained here. Link to a sibling's canonical document, never to one
it has archived or superseded; where a decision was taken in a document that is
now history there, cite it as history, not as policy.

## Root README

The root README is an entry point in the shared shape — Scope,
Architecture, Components, Documentation, Build, License — and stays short.
It carries no current milestone, no version status prose, no status column in
its component table, and no contract definitions or sibling status; it links
to the [capability matrix](../reference/CAPABILITY_MATRIX.md) and the
[roadmap](../roadmap/current.md) instead.

## Stable numbering

Design and architecture documents number their sections, and a number never
changes meaning. **Sibling repositories cite them** ("the connectors policy
§42"). A revision adds subsections or appends sections. Open questions are
identified by prefix and number (`CC-O1`, `CS-O1`, `SP-O1`, `FW-O1`,
`WSC-O1`, `CLI-O1`, `WS-O1`, `DIAG-O1`) and never reused.

## Evidence

- **A basis, a frame policy or a sender's behaviour is measured, documented or
  assumed**, and the page says which. Documentation is a claim until a
  labelled session verifies it ([COORDINATE_SYSTEMS.md §3](../design/COORDINATE_SYSTEMS.md#3-what-a-connector-declares)).
- **Evidence from sibling repositories** is cited with where it was measured.
  It is not restated as this repository's finding, and not re-opened without
  new evidence. When code arrives from `usd-vrm-plugins`, its evidence
  arrives in the document that owns it.
- **A report from a real session** records the sender, its version, the
  device, the date and what was measured. It never records the performer's
  identity or where non-redistributable bytes are kept.

## Naming

- Shared motion names are `usd-motion-plugins`' (`MotionPose`, `HumanJoint`,
  `MotionChannelSet`). A sibling's old name (`HumanoidPose`, `vrmAdapterVmc`)
  appears only where the text is about the sibling or the import.
- When phases are mentioned, qualify them by their owning repository or
  document ([DESIGN_POLICY.md §46.6](../design/DESIGN_POLICY.md#466-phases-are-always-qualified)).
- Product and protocol names appear in a connector's own documents and in
  provenance, never as a condition in a shared contract.

## Language and form

- Repository documents are in English.
- Relative links for everything in the repository; code spans for commands,
  paths, targets, types, profile ids and diagnostic codes.
- Keep each category index (`docs/README.md`, `roadmap/README.md`) in sync with
  its files.
- Never commit machine-local paths; write `$HOME` or `%USERPROFILE%`.
- Never commit a capture, motion or recording whose terms do not allow
  redistribution, and never a recording of a person without their consent.

## Definition of Done

A feature requires code + tests + capability matrix + architecture/design
updates where needed + changelog + removal of its finished roadmap items.
Apply these updates in the same PR. A documentation-only change updates the
owning pages and changelog and runs the documentation checks.

## Change checklist

1. Capability status changed → update the matrix with test evidence and release availability.
2. Architecture changed → update the architecture owner.
3. Design decision changed → update rationale/invariants/open questions in design.
4. Landed behavior changed → update `CHANGELOG.md`; freeze shipped scope in `releases/` at release time.
5. Remove finished work from `roadmap/` in the same PR.
6. Add no duplicate status table, dependency map or open-question table.
7. List new pages in their category index; keep links and heading anchors valid.
8. Check changes to a cited section against sibling citations; preserve section meaning and identifiers.
9. Propose changes to `usd-motion-plugins`' contract there rather than redefining it here.

## Automated drift checks

Run `python scripts/check_docs.py --selftest` and `python scripts/check_docs.py`.
The existing `docs-check` workflow runs both on documentation changes.

- Fail on roadmap done markers, checked task boxes and implemented/completed
  claims outside fenced examples.
- Warn on `planned for v`, `next release`, `currently implementing` and
  `in progress` outside the roadmap in current README, architecture, design
  and reference prose. Warnings require review, not a blanket word ban.
- Require the root README and architecture pages to link to the capability matrix.
- Check published release availability in the matrix against changelog headings,
  release records and `VERSION`. Future scope belongs to the roadmap; a matrix
  row uses `unreleased` until the release record and finalized changelog exist.
  When preparing a release, provide its record in the same PR as the finalized
  heading. Historical records are checked for consistency, not edited.

These checks detect common drift; reviewers still assess semantic ownership,
missing evidence and duplicated claims that a phrase scan cannot prove.
