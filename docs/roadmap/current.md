# Current: after v0.1.0

Status: ⬜ v0.1.0's scope is complete ([release record](../releases/v0.1.0.md));
this page holds what it carried forward and what v0.2.0 needs decided first.

## Carried from v0.1.0

- ⬜ Publish the four CLIs once `ost` packages a workspace tool without a
  plugin bundle; until then they ship as source
  ([WORKSPACE §4.1](../architecture/WORKSPACE.md#41-distribution)).
- ⬜ Revisit actor identity with the first multi-actor source (`CC-O5`).

### Operator evidence

Carried from `usd-vrm-plugins`, which shipped these connectors through its
v0.8.0 without them. None closes by writing code; each is a recorded session
and a manifest, and none gates a pull request. A recorded session is replayed
against its manifest with `scripts/check_recorded_sessions.py`
([building guide](../guides/building.md#replaying-the-recorded-device-sessions)).

- ⬜ **A VMC relay or sender session, compared at the canonical layer.** Two
  observation paths of one mocopi session agree to a median 0.084° per bone
  ([`usd-vrm-plugins` report motion/01](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/reports/motion/01-2026-08-15-mocopi-cross-source.md));
  VMC has no recorded sender yet. The same sessions settle `CS-O2`.
- ⬜ **A recovery a device can actually produce, or a decision that it cannot.**
  Removing a mocopi sensor puts the app into re-tracking, so the stream never
  carries a lost sensor, and `MOCOPI_TRACKING_LOST` stays reserved and
  unraised ([CONNECTOR §5](../design/CONNECTOR_CONTRACT.md#5-state)). A source
  restart is recorded.
- ⬜ **A labelled *rolled* VRChat OSC take.** The tracker Euler order is
  measured to three compositions of six, because nobody tilted in the
  2026-08-30 session. Twenty seconds settles it: a head tilted onto one
  shoulder and held, or a foot rolled onto its outer edge, with the side
  written down. Until then the residual is median 0.21°, 12.33° at worst
  ([`usd-vrm-plugins` report motion/03](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/reports/motion/03-2026-08-30-vrchat-osc-tracking-space.md)
  §2.3).
- ⬜ **A redistributable mocopi capture.** The device sessions are
  `recorded/manifest.json` rows with no bytes, because a session is a real
  person's motion. A publishable one needs the vendor's `BVH Sender`, not a
  device.
- ⬜ **A live session reaching an avatar from release artifacts alone.** It
  composes this repository's release with an avatar repository's. v0.1.0's
  libraries are now published, so it waits only on the avatar side; where the
  composed test runs is `usd-avatar-runtime`'s once it exists.

## Next: v0.2.0

v0.2.0 carries the WebSocket transport, the capture/bridge commands, Python
bindings and the record-stream example
([status table](README.md#status-at-a-glance)). Two decisions come before any
of its code, because both fix what crosses a process boundary:

- ⬜ `CC-O8`, the `MotionFrame` wire representation
  ([CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions)).
- ⬜ `CC-O7`, a C ABI, which the Python bindings stand on
  ([CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions)).

## Later releases

The later sequence remains: browser MediaPipe and the JS/WASM boundary,
WebXR, OpenXR and advanced sources. Their open decisions stay in the owning
design or architecture document and are scheduled in
[roadmap/README.md](README.md); this page does not duplicate them.
