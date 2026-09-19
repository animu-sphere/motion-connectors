# Current: the contract, then v0.1.0

Status: 🚧 documentation baseline done (2026-09-19); scaffold done
(2026-09-19), its CI rendered with the first import; v0.1.0 🚧 —
`motionConnectorTransport` and `motionConnectorOsc` imported (2026-09-19).

Connector Phase 1 is *contract first* ([DESIGN_POLICY.md §30](../design/DESIGN_POLICY.md#30-recommended-initial-implementation-order),
§44). The documents exist: the design policy, the connector contract, the
coordinate-system and source-profile contracts, and the workspace contract.
They are **proposed**, and they build on `usd-motion-plugins`' motion contract
and `usd-vrm-plugins`' measurements. The tree can now receive code. What
remains is the decisions that must precede code, and then the imports.

## What remains

### The scaffold ✅ (2026-09-19)

The names and layout are decided (WS-O1:
[WORKSPACE.md §1.1](../architecture/WORKSPACE.md#11-native-libraries)), and so
is the import order (WS-O7:
[WORKSPACE.md §3](../architecture/WORKSPACE.md#3-moving-code-in)). The root
project pins OpenUSD 26.08 and gives every connector its own option, off
unless asked for. The docs check gates every pull request, the
installed-consumer lane runs over an empty package list, and the community
files are in place. What landed is in the [changelog](../../CHANGELOG.md), and
how to build it is in the [building guide](../guides/building.md).

- ✅ **The rendered `ost` CI workflow** (2026-09-19, with
  `motionConnectorTransport`). Under `ost` 0.22.10 every rendered job runs the
  workspace graph step, which refuses a workspace with no member, so the
  scaffold could not render it; the first member made it renderable, and the
  graph cell arrived with it.

### The contract, Connector Phase 1 ⬜ (the design can start now; the code waits for `motionCore`)

- ⬜ Resolve **CC-O6** (`Poll`), **SP-O1** (profile ids), **SP-O2** (profiles as
  data) and **DIAG-O1** (code style, together with `usd-motion-plugins`).
- ⬜ Propose this repository's answer to `usd-motion-plugins` **MC-O5**
  (`MotionStream`'s shape) from CC-O3, and take part in the decision there
  before its v0.1.0 freezes it.
- ⬜ `motionConnectorCore`: the interface, `MotionFrame`, state, capabilities,
  timing and the bounded buffer, with unit tests for every buffer mode and every
  state transition. ⛔ on an installed `motionCore`.

### v0.1.0: the contract and every imported input ⛔ on `usd-motion-plugins` v0.1.0

`usd-vrm-plugins` v0.9.0 was published on 2026-09-17, so `usd-motion-plugins`
v0.1.0 is no longer blocked. It waits only for its own first import.

- 🚧 Import, in dependency order and one identity per change, with history,
  tests and each identity's checks
  ([WORKSPACE.md §3](../architecture/WORKSPACE.md#3-moving-code-in)). The
  transport and the wire format depend on nothing, so they arrive before
  `usd-motion-plugins` v0.1.0 is installable; everything after them links
  `motionCore`. `usd-vrm-plugins` still deletes its copies in one change, after
  the last import.
  - ✅ `liveTransport` as `motionConnectorTransport` (2026-09-19): 10 commits
    of history through `git filter-repo`, a move-only commit, then the rename
    to `openstrata::connectors::transport`. Its three suites and its boundary
    check came with it, and the check refuses this repository's other
    libraries and every `usd-motion-plugins` library by name.
  - ✅ `osc` as `motionConnectorOsc`, with the address-literal check
    (2026-09-19): 6 commits of history, a move-only commit, then the rename to
    `openstrata::connectors::osc`. The check scans the suite too, and the
    suite's corpus half, which reads the VMC capture format, arrives with the
    VMC connector;
  - `motionTracking` as `motionConnectorTracking`, on the terms **WS-O2**
    settles;
  - `vrmAdapterVmc`, `vrmAdapterMocopi` and `vrmAdapterVrchatOsc` as
    `motionConnectorVmc`, `motionConnectorMocopi` and
    `motionConnectorVrchatOsc`. Each comes with its record tool, its
    generated corpus and its recorded-session manifests, and each renames its
    diagnostics per DIAG-O1. The mocopi import settles **CC-O4**, and the
    VRChat OSC import settles **CS-O1** and **CS-O3**.
- ⬜ Adapt each connector to `IMotionConnector` in a change of its own, after
  it moves, VMC first.
- ⬜ Write `vmc.v1` as the first source profile, with the basis evidence of
  [COORDINATE_SYSTEMS.md §5](../design/COORDINATE_SYSTEMS.md#5-known-sources),
  then `mocopi.body.v1` and the VRChat OSC profile.
- ⬜ `motion_connect dump --source vmc --port 39539`, printing the frame shape
  of design policy §31; `list` and `inspect`.
- ⬜ Reproduce `usd-vrm-plugins`' replay evidence for all three connectors
  against this repository's packages, so that repository can delete every
  copy in one change (its migration track, MIG-4).

## Completion criteria

v0.1.0 is done when all of the following hold:

- A VMC sender on loopback reaches `motion_connect dump` as `MotionFrame`s with
  canonical body joints, timestamps, confidence and a source profile.
- A packet capture replays through the same path, deterministically, in CI,
  with no hardware and no network peer.
- Every generated malformed-packet case is refused with a diagnostic.
- The packages configure from a clean installed prefix.
- The mocopi and VRChat OSC captures replay through the same path, each to
  the result `usd-vrm-plugins` recorded for it.
- `usd-vrm-plugins` builds without `liveTransport`, `osc`, `motionTracking`
  and all three `vrmAdapter*` libraries, consuming none of them.
