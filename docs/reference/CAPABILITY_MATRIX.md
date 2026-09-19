# Capability matrix

This is the only document that says what this repository implements. A
capability is listed as supported only when a test is behind it, and a source
counts as supported only when the test needs no hardware.

Vocabulary: **supported** · **approximated** · **unsupported** · **—**
nothing implemented. The "Implemented elsewhere" column says where the
behaviour exists today, before it moves here; it is not a claim about this
repository.

Status (2026-09-19): **nothing is implemented.**

## 1. Contract

| Capability | Status | Contract | Implemented elsewhere | Release |
| --- | --- | --- | --- | --- |
| `IMotionConnector`, state, capabilities | — | [CONNECTOR §2, §5](../design/CONNECTOR_CONTRACT.md) | `usd-vrm-plugins` per adapter (`LiveSource`), not as one interface | v0.1.0 |
| `MotionFrame`, `FrameTiming`, actors | — | [CONNECTOR §3, §6, §11](../design/CONNECTOR_CONTRACT.md#3-motionframe) | nowhere | v0.1.0 |
| Bounded frame buffer, `Latest` / `Ordered` / `Lossless` | — | [CONNECTOR §8](../design/CONNECTOR_CONTRACT.md#8-buffering-push-and-pull) | `usd-vrm-plugins` `liveTransport` (datagram queue) | v0.1.0 |
| Source profiles | — | [SOURCE_PROFILES](../design/SOURCE_PROFILES.md) | nowhere as one format | v0.1.0 |
| UDP transport, packet capture and replay | — | [CONNECTOR §12](../design/CONNECTOR_CONTRACT.md#12-raw-capture) | `usd-vrm-plugins` `liveTransport` | v0.1.0 |
| OSC 1.0 wire format | — | [WORKSPACE §1.1](../architecture/WORKSPACE.md#11-native-libraries) | `usd-vrm-plugins` `osc` | v0.1.0 |
| `TrackerObservation`, assignment, tracker solve | — | [CONNECTOR §4](../design/CONNECTOR_CONTRACT.md#4-trackerobservation) | `usd-vrm-plugins` `motionTracking` | v0.1.0 |

## 2. Sources

| Source | Profile | Body | Hands | Face | Root | Trackers | Status | Implemented elsewhere | Release |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| VMC Protocol | `vmc.v1` | — | — | — | — | | — | `usd-vrm-plugins` `vrmAdapterVmc` | v0.1.0 |
| mocopi native UDP | `mocopi.body.v1` | — | | | — | | — | `usd-vrm-plugins` `vrmAdapterMocopi` | v0.1.0 |
| VRChat OSC Trackers | `vrchat-osc.trackers.v1` | | | | | — | — | `usd-vrm-plugins` `vrmAdapterVrchatOsc` | v0.1.0 |
| WebSocket (`MotionFrame`) | — | — | — | — | — | — | — | nowhere | v0.2.0 |
| MediaPipe | `mediapipe.*.v1` | — | — | — | | | — | nowhere | v0.3.0 |
| WebXR | `webxr.*.v1` | | — | | | | — | nowhere | v0.4.0 |
| OpenXR | `openxr.*.v1` | | — | | | | — | nowhere | later |
| Generation adapter (ARDY) | — | | | | | | — | nowhere | later |

An empty cell means that source cannot carry that part.

## 3. Tools and bindings

| Capability | Status | Implemented elsewhere | Release |
| --- | --- | --- | --- |
| `motion_connect dump` | — | nowhere | v0.1.0 |
| `motion_connect list`, `inspect` | — | nowhere | v0.1.0 |
| `motion_connect record`, `bridge` | — | `usd-vrm-plugins` `vmc_record`, `mocopi_record`, `vrchat_osc_record` (record only) | v0.2.0 |
| Python bindings | — | nowhere | v0.2.0 |
| JS / TS package, WASM data ABI | — | nowhere | v0.3.0 |

## 4. Not here by design

| Capability | Status | Reason | Where |
| --- | --- | --- | --- |
| Retargeting onto a skeleton | unsupported by design | design policy §20, Rule 3 | `usd-motion-plugins` |
| Filtering, smoothing, resampling | unsupported by design | design policy §26 | `usd-motion-plugins` |
| Canonical recording, motion file formats | unsupported by design | design policy §25 | `usd-motion-plugins` |
| VRM, VRMA, PMX, VMD semantics | unsupported by design | design policy §21, §22, Rule 4 | the format repositories |
| USD stage authoring | unsupported by design | design policy §24, Rule 5 | `usd-motion-plugins`, `usd-avatar-runtime` |
