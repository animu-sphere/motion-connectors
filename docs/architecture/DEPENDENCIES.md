# External dependencies

What `motion-connectors` builds against and what it refuses. Edges between
this repository's own components are
[WORKSPACE.md §2](WORKSPACE.md#2-dependency-directions)'s.

Status (2026-09-21): **adopted by the current workspace.** The OpenUSD pin and
the toolchain below are what the root project enforces. The per-connector rows
describe the imported connectors and the reserved modules. Every value is taken from the sibling
repositories so that `usd-avatar-runtime` can compose all of them into one
process.

## 1. OpenUSD

| | |
| --- | --- |
| Pin | OpenUSD **26.08**, exactly: the release `usd-motion-plugins`, `usd-vrm-plugins` and `usd-mmd-plugins` pin. This repository opens no stage, but `motionCore` is built against one OpenUSD release, and a consumer built against another does not link |
| `motionConnectorCore`, imported connectors | foundation value types only, through `motionCore`: `gf`, `tf`, `vt` |
| tools, examples | whatever `usd-motion-plugins` library they call; `examples/usd_avatar_live` is the only place a stage appears |
| Pin changes | coordinated: a new OpenUSD release is adopted here together with `usd-motion-plugins`, `usd-vrm-plugins` and `usd-mmd-plugins`. Who releases first is open in `usd-vrm-plugins`' migration track |

Whether the web path can avoid this closure is
[WORKSPACE.md](WORKSPACE.md#7-open-questions) WS-O4.

## 2. `usd-motion-plugins`

| | |
| --- | --- |
| Links | `motionCore` (the pose values); `motionSampling` or `motionRecording` for the live-source bridge the imported connectors use (WORKSPACE §2.1) |
| Version | during 0.x, one exact `usd-motion-plugins` release per release here, until both sides agree a range. The same question is open in `usd-vrm-plugins`' migration track |
| Consumed as | an installed package, never a source tree |

## 3. Toolchain

| | |
| --- | --- |
| Language | C++20 for native libraries; TypeScript for web modules |
| Build | CMake 3.22 or later; `CMakePresets.json` for plain CMake |
| Compilers | MSVC on Windows, Clang on macOS arm64, GCC on Linux: the siblings' three lanes |
| OpenStrata | `ost` 0.23.6, pinned in `openstrata.ci.yaml`, as the sibling workspaces pin it |
| Tests | as in the siblings: plain executables registered with CTest, checking with `assert()` compiled into Release builds, unless the scaffold records a reason to differ |
| Python | the interpreter OpenUSD was built against, for bindings and tooling (v0.2.0) |
| Node | an LTS release, for the JS / TS package (v0.3.0) |

## 4. Per-connector dependencies

Each is isolated to its connector and is off unless that connector is built
(design policy §28).

| Connector | Dependency | Kind |
| --- | --- | --- |
| `motionConnectorTransport` | OS sockets | system |
| `motionConnectorOsc` | none; the wire format is implemented here | — |
| `motionConnectorVmc` | `motionConnectorCore`, `motionCore`, `motionSampling`, `motionRecording`, transport and OSC | installed sibling packages |
| `motionConnectorMocopi` | `motionConnectorCore`, `motionCore`, `motionSampling`, `motionRecording` and transport | installed sibling packages |
| `motionConnectorVrchatOsc` | `motionConnectorCore`, `motionCore`, transport and OSC | installed sibling packages |
| `motionConnectorWebSocket` | a WebSocket library, optional; TLS optional on top | third party, chosen with the connector |
| `motionConnectorOpenXR` | the OpenXR loader | third party |
| `motionConnectorMediaPipe` | the MediaPipe Tasks package | npm |
| `motionConnectorWebXR` | browser APIs only | — |

Each third-party dependency is recorded in `THIRD_PARTY_NOTICES.md` in the
change that adds it.

## 5. Refused

| Dependency | Refused because |
| --- | --- |
| OpenUSD stage, Sdf, `usdSkel`, Hydra, OpenExec in any library | no connector requires a `UsdStage` (design policy Rule 5, §24) |
| a filtering, IK or retargeting library | these exist once, downstream (design policy §26) |
| an ML framework or model runtime in native code | a model-based tracker runs in its own package (MediaPipe in the browser), and a generator sits behind `usd-motion-plugins`' generator interface |
| `usd-vrm-plugins`, `usd-mmd-plugins`, `usd-avatar-runtime` | the dependency direction is one way ([WORKSPACE.md §2.3](WORKSPACE.md#23-the-ecosystem)) |
| a vendor SDK discovered at build time | a vendor SDK is declared in the connector's manifest (WS-O5) or not used |

## 6. Data

- **Recorded sessions** carry their performers' and senders' terms and are
  committed only where those terms allow redistribution. Everything else
  leaves a manifest ([WORKSPACE.md §5](WORKSPACE.md#5-test-data)).
- **Generated captures** are produced by committed code and are this
  repository's own.
- A recording of a person is personal data. It is recorded only with the
  performer's consent, and its manifest never names them.
