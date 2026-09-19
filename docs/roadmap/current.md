# Current: the scaffold and the contract, then v0.1.0

Status: 🚧 documentation baseline done (2026-09-19); everything below ⬜.

Connector Phase 1 is *contract first* ([DESIGN_POLICY.md §30](../design/DESIGN_POLICY.md#30-recommended-initial-implementation-order),
§44). The documents exist: the design policy, the connector contract, the
coordinate-system and source-profile contracts, and the workspace contract.
They are **proposed**, and they build on `usd-motion-plugins`' motion contract
and `usd-vrm-plugins`' measurements. What remains is a tree that can receive
code, the decisions that must precede code, and then the first import.

## What remains

### The scaffold ⬜ (can start now)

- ⬜ Resolve **WS-O1**: names, directories, target and C++ namespaces.
- ⬜ Root `CMakeLists.txt`, `CMakePresets.json`, `VERSION` (`0.1.0` in
  development), `openstrata.toml`, `openstrata.ci.yaml` with the siblings'
  OpenUSD 26.08 runtimes and `ost` pin, and a generated CI workflow. Every
  connector is a separate option, off unless asked for.
- ⬜ A docs check in CI: every relative link and anchor resolves, and every
  version mirror agrees with `VERSION`, as the siblings' `check_docs.py` do.
- ⬜ Community files matching the siblings: `CONTRIBUTING.md`,
  `CODE_OF_CONDUCT.md`, `SECURITY.md` (with the network-exposure rules of
  [CONNECTOR_CONTRACT.md §10](../design/CONNECTOR_CONTRACT.md#10-untrusted-input)),
  `THIRD_PARTY_NOTICES.md`, and issue and pull request templates.

### The contract, Connector Phase 1 ⬜ (the design can start now; the code waits for `motion-core`)

- ⬜ Resolve **CC-O6** (`Poll`), **SP-O1** (profile ids), **SP-O2** (profiles as
  data) and **DIAG-O1** (code style, together with `usd-motion-plugins`).
- ⬜ Propose this repository's answer to `usd-motion-plugins` **MC-O5**
  (`MotionStream`'s shape) from CC-O3, and take part in the decision there
  before its v0.1.0 freezes it.
- ⬜ `motionConnectorCore`: the interface, `MotionFrame`, state, capabilities,
  timing and the bounded buffer, with unit tests for every buffer mode and every
  state transition. ⛔ on an installed `motion-core`.

### v0.1.0: contract and VMC ⛔ on `usd-motion-plugins` v0.1.0

`usd-motion-plugins` v0.1.0 waits for `usd-vrm-plugins` v0.9.0 (the OpenExec
foundation), whose findings are fixed as the core moves.

- ⬜ Resolve **WS-O7** with `usd-vrm-plugins`: whether the shared leaves can
  move before every connector that links them.
- ⬜ Import `liveTransport` as `motionConnectorTransport` and `osc` as
  `motionConnectorOsc`, with history, tests and the address-literal check
  ([WORKSPACE.md §3](../architecture/WORKSPACE.md#3-moving-code-in)).
- ⬜ Import `vrmAdapterVmc` as `motionConnectorVmc`, with `vmcRecord`, the
  generated corpus and the recorded-session manifests. Rename its diagnostics
  per DIAG-O1.
- ⬜ Adapt it to `IMotionConnector` in a change of its own, after the move.
- ⬜ Write `vmc.v1` as the first source profile, with the basis evidence of
  [COORDINATE_SYSTEMS.md §5](../design/COORDINATE_SYSTEMS.md#5-known-sources).
- ⬜ `motion-connect dump --source vmc --port 39539`, printing the frame shape
  of design policy §31; `list` and `inspect`.
- ⬜ Reproduce `usd-vrm-plugins`' VMC replay evidence against this repository's
  package, so that repository can delete its copy.

## Completion criteria

v0.1.0 is done when all of the following hold:

- A VMC sender on loopback reaches `motion-connect dump` as `MotionFrame`s with
  canonical body joints, timestamps, confidence and a source profile.
- A packet capture replays through the same path, deterministically, in CI,
  with no hardware and no network peer.
- Every generated malformed-packet case is refused with a diagnostic.
- The packages configure from a clean installed prefix.
- `usd-vrm-plugins` builds without its `vrmAdapterVmc`, and without its
  `liveTransport` and `osc` on the terms WS-O7 settles.
