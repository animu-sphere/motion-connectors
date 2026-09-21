# Diagnostics

The catalog of diagnostic codes this repository raises. Status (2026-09-21):
**three imported families, none renamed yet**. The codes the arrived
connectors raise are listed by their own `Diagnostics.h` and are tabulated here
once DIAG-O1 gives them their names — one change over every connector, rather
than a rename per import. A code that is *new* here is added to §2 in the
change that first raises it.

## 1. The record

A diagnostic is a **value** reported beside frames, never an exception thrown
into the runtime ([CONNECTOR_CONTRACT.md §9](../design/CONNECTOR_CONTRACT.md#9-diagnostics)):

- a code;
- a severity from one table, so a call site cannot choose it;
- a subject naming what it is about: a joint, an address, a packet sequence
  or a byte offset;
- a detail sentence for a person;
- whether it is **recoverable**.

Tests assert the code and the subject, never the prose.

Namespaces are separate by layer, so a reader can tell a decode failure from a
contract violation without knowing which connector produced it:

| Layer | Owns |
| --- | --- |
| transport | bind, receive, capture-file failures |
| wire format (OSC) | malformed packets, as protocol-neutral events |
| each connector | its protocol's semantics and frame assembly, frozen before its decoder |
| connector core | state transitions, buffer overflow, contract violations (non-finite values, clock regressions) |

A code raised by `usd-motion-plugins` keeps its own code when it passes
through; this repository never re-codes it.

## 2. Catalog

| Code | Severity | Recoverable | Raised by | Meaning |
| --- | --- | --- | --- | --- |
| — | | | | |

## 3. Arriving with the imports

These families exist in `usd-vrm-plugins` today and arrive with their
connectors. Their final names are DIAG-O1.

| Family today | Codes | Arrives with |
| --- | --- | --- |
| `VRM_VMC_*` | 8: packet malformed, unsupported message, timestamp regression, duplicate bone, incomplete frame, source restarted, socket bind failed, stale joint | `motionConnectorVmc` — **arrived 2026-09-21**, unrenamed |
| `VRM_MOCOPI_*` | 9: socket bind failed, device unavailable, unsupported joint, packet malformed, tracking lost, timestamp invalid, source restarted, frame incomplete, non-finite transform | `motionConnectorMocopi` — **arrived 2026-09-21**, unrenamed |
| `VRM_VRCHAT_OSC_*` | 10 | `motionConnectorVrchatOsc` — **arrived 2026-09-21**, unrenamed |

`liveTransport` and `osc` hold no code enum of their own: they report through
the connector that links them.

## 4. Open questions

| Id | Question | Resolve by |
| --- | --- | --- |
| DIAG-O1 | Code style and the renaming of the imported codes. Deferred past the first two imports on purpose (2026-09-21): renaming a family per import would spend the same review twice and leave the ecosystem inconsistent in between. The `VRM_` prefix is wrong here, because nothing in this repository is VRM's. Candidates: `VMC_*` / `MOCOPI_*` per connector with `CONNECTOR_*` for the core; or numbered codes, as `usd-motion-plugins`' design policy §29 proposes and its DIAG-O1 leaves open. Decide together with `usd-motion-plugins`, so the ecosystem has one style | now: every connector has arrived, before v0.1.0 |
