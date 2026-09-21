# Documentation guidelines

Documentation is part of the implementation contract. A change is incomplete
if it changes one of the following without updating the page that owns it:

- a public boundary;
- a frame's or a profile's meaning;
- a source's declared basis;
- implemented architecture;
- delivery status.

## Category ownership

| Category | Put this here | Not this |
| --- | --- | --- |
| `architecture/` | Component identities, dependency edges, layout, build modes, external dependencies: the binding structural contract, with what exists marked as such. | Rationale; unimplemented components described as present. |
| `design/` | Intended contracts, their rationale, the evidence they rest on, and open questions. | Claims that something is implemented. |
| `reference/` | Facts about the current tree: capabilities, diagnostics. | Plans, except in a column clearly labelled as elsewhere or later. |
| `roadmap/` | Incomplete, ordered work, its completion criteria, and which release carries it. | Completed work; rationale. |
| `guides/` | How to accomplish a task, with commands that have been run. | Commands nobody has run. |
| `releases/` | One immutable record per released version. | Work in progress. |
| `reports/` | Dated evidence from real runs and real sessions; append-only. | Current-state claims. |
| `contributing/` | How to maintain this repository. | End-user tasks. |

## Status rules

- A design document carries `proposed`, `accepted`, `binding`, `superseded` or
  `rejected`. A section becomes binding when the code it describes lands with
  tests; changing it afterwards is a contract change.
- Roadmap items are ✅ done, 🚧 in progress, ⬜ not started, or ⛔ blocked.
- The capability matrix never says "supported" without a test, and never for a
  source whose only test needs hardware.
- A release record and a dated report are not rewritten. A later finding gets a
  new report and a one-line forward note on the old one.

## Stable numbering

Design and architecture documents number their sections, and a number never
changes meaning. **Sibling repositories cite them** ("the connectors policy
§42"). A revision adds subsections or appends sections. Open questions are
identified by prefix and number (`CC-O1`, `CS-O1`, `SP-O1`, `WS-O1`,
`DIAG-O1`) and never reused.

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

## Change checklist

1. Planned behaviour is not presented as implemented.
2. Every new page appears in its category index.
3. Relative links and heading anchors resolve.
4. Implementation changes update `architecture/` and `reference/`.
5. Completed work leaves `roadmap/`.
6. A departure from a design document is recorded in that document.
7. A change to a section a sibling repository cites is checked against the
   citation.
8. A change that touches `usd-motion-plugins`' contract is proposed there, not
   worked around here.
