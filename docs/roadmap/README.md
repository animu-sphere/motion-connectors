# Roadmap

This directory holds **incomplete work only**: ordering, completion criteria
and intended release scope. Remove an item in the same PR that finishes it.
Implementation status and test evidence live only in the
[capability matrix](../reference/CAPABILITY_MATRIX.md). Structure belongs to
[architecture/](../architecture/), decisions to [design/](../design/), landed
changes to the [changelog](../../CHANGELOG.md), and frozen release scope to
[releases/](../releases/README.md).

| Document | Contents |
| --- | --- |
| [current.md](current.md) | v0.2.0 execution order and release gates; v0.3.0 consumer boundary; deferred work and operator evidence. |
| [boundary-implementation.md](boundary-implementation.md) | Motion-owned solve input prerequisites and the consumer parity/migration gate. |

## Status at a glance

This heading is retained for existing links. The table owns **intended,
incomplete release scope**, not implementation or release availability.

| Milestone | Incomplete scope | Dependency |
| --- | --- | --- |
| v0.2.0: native consumer boundary | CC-O7 decision and C ABI; Python binding; `record_stream` example; installed-consumer and release artifact validation; docs consistency | preserve the acquisition gates in [WORKSPACE §2.4](../architecture/WORKSPACE.md#24-enforcement) |
| v0.3.0: browser / JS / WASM consumer boundary | shared JS/TS consumer API in `bindings/js/`; WASM-friendly data ABI | the native C ABI and browser acquisition contracts |
| after the current milestones | advanced devices, generation adapter and further integration examples | explicit scope and downstream contracts before scheduling |

New connectors are not v0.2.0 release blockers. Binary encoding, TLS,
multi-peer receive, multi-actor redesign, advanced device expansion and large
runtime abstractions follow the current consumer-boundary milestones.

## Decision ordering

Questions stay in their owning documents; this list schedules work without
copying their definitions or resolution status.

- Adopt the motion-owned input and parity contracts before
  [generic solve migration](boundary-implementation.md).
- Resolve [CC-O7](../design/CONNECTOR_CONTRACT.md#13-open-questions) before
  Python binding implementation and v0.2.0 validation.
- Consider [CC-O1 and CC-O5](../design/CONNECTOR_CONTRACT.md#13-open-questions)
  only when semantic joint data or a multi-actor source requires them.
- Collect the sender evidence for
  [CS-O2](../design/COORDINATE_SYSTEMS.md#6-open-questions) as operator work.
- Revisit [WSC-O1 / WSC-O2](../design/WEBSOCKET_CONNECTOR.md#11-open-questions)
  and [FW-O1](../design/FRAME_WIRE_FORMAT.md#9-open-questions) when a concrete
  transport or measured encoding requirement justifies them.
