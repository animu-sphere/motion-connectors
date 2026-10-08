# Building

How to build and test `motion-connectors` from a checkout. The tree builds the
shared core, transport and wire-format leaves, the VMC, mocopi and VRChat OSC
connectors with their record tools, the WebSocket connector, and the
`motion_connect` CLI. A build pins OpenUSD and runs the source corpus,
installed-consumer and workspace tests.

## Requirements

- OpenUSD **26.08**, exactly. Any other release is a configure error
  ([DEPENDENCIES.md §1](../architecture/DEPENDENCIES.md#1-openusd)). No
  connector opens a stage, but every one links `usd-motion-plugins`'
  `motionCore`, which is built against it. From the first connector on, an
  installed `usd-motion-plugins` goes on `CMAKE_PREFIX_PATH` too.
- CMake 3.22 or later, and a C++20 compiler: MSVC on Windows, Clang on macOS,
  GCC on Linux.
- Python 3, the interpreter OpenUSD was built against, for the workspace tests.

## With `ost`

`ost` activates a pinned OpenUSD runtime and its toolchain, then configures the
root `CMakeLists.txt`:

```powershell
ost build
ost test
```

## With plain CMake

Point `USD_INSTALL_ROOT` at an OpenUSD 26.08 install and use a preset:

```powershell
$env:USD_INSTALL_ROOT = "C:/usd/openusd-26.08"
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release
```

On macOS and Linux the presets are `macos-ninja` / `macos-release` and
`linux-ninja` / `linux-release`. On Windows, run from a Developer PowerShell,
so the compiler and the resource compiler are on `PATH`. The installed-consumer
test configures a second project, and it fails without them.

## Choosing what to build

Every connector is optional (WORKSPACE.md §4), and each is one CMake option:

| Option | Default | Builds |
| --- | --- | --- |
| `MOTIONCONNECTORS_BUILD_VMC` | `ON` | `motionConnectorVmc`, and `vmc_record` with the tools |
| `MOTIONCONNECTORS_BUILD_MOCOPI` | `ON` | `motionConnectorMocopi`, and `mocopi_record` with the tools |
| `MOTIONCONNECTORS_BUILD_VRCHAT_OSC` | `ON` | `motionConnectorVrchatOsc`, and `vrchat_osc_record` with the tools |
| `MOTIONCONNECTORS_BUILD_WEBSOCKET` | `ON` | `motionConnectorWebSocket`; it brings no third-party library |
| `MOTIONCONNECTORS_BUILD_OPENXR` | `OFF` | `motionConnectorOpenXR`; requires installed OpenXR-SDK ≥1.1.36 |
| `MOTIONCONNECTORS_BUILD_TOOLS` | `ON` at the top level | the recorders, and `motion_connect` when all four CLI sources are on |
| `MOTIONCONNECTORS_BUILD_TESTS` | `ON` at the top level | every component's tests and the workspace tests |

The transport and the OSC wire format depend on nothing and are always built.

To enable OpenXR, put its installed SDK on `CMAKE_PREFIX_PATH` or set
`OpenXR_DIR` to the directory holding `OpenXRConfig.cmake`, then enable
`MOTIONCONNECTORS_BUILD_OPENXR`. `motionConnectorOpenXR_connector` uses SDK
fixtures with no runtime/headset. With the SDK enabled, the installed-consumer
lane also consumes its installed package and profile through the same SDK path.
The caller owns its live session and frame loop
([usage](../../libs/motionConnectorOpenXR/README.md#session-ownership)).

The core, the `MotionFrame` wire format and the tracker layer link
`usd-motion-plugins`' `motionCore`, and are built whenever any connector is. With every connector off, configure asks for
no OpenUSD at all:

```powershell
cmake -S . -B build/minimal -G Ninja `
  -DMOTIONCONNECTORS_BUILD_VMC=OFF `
  -DMOTIONCONNECTORS_BUILD_MOCOPI=OFF `
  -DMOTIONCONNECTORS_BUILD_VRCHAT_OSC=OFF `
  -DMOTIONCONNECTORS_BUILD_WEBSOCKET=OFF
```

A single component also configures on its own, against installed packages on
`CMAKE_PREFIX_PATH` -- for example `cmake -S libs/motionConnectorVmc -B ...`.
Its tests follow `<NAME>_BUILD_TESTS` (for example
`MOTIONCONNECTORVMC_BUILD_TESTS`), which defaults to on when it is the
top-level project.

Build output stays in the build tree. The runtime layout -- `bin/`, `lib/`,
`include/`, `share/` -- is what `cmake --install` writes to a prefix.

## What the tests check

### Browser acquisition

The independent WebXR npm module requires no native build or package install:

```powershell
npm test --prefix web/motionConnectorWebXR
npm pack ./web/motionConnectorWebXR --dry-run
```

`web-check` runs these Node 22 API fixtures and package checks. The source
package includes JavaScript, TypeScript declarations and its declarative
profile. No browser or headset is required for deterministic tests; native
builds never require Node. `motionConnectorWire_webxrInterop` decodes the
browser module's tested wire fixture with the C++ codec when the source
fixture is available in the checkout. See the
[WebXR usage guide](../../web/motionConnectorWebXR/README.md).

From `web/motionConnectorWebXR/`, `npm ci --ignore-scripts` and
`npm run typecheck` check the public package exports against browser WebXR
declarations using pinned development dependencies. The runtime module remains
dependency-free.

MediaPipe result acquisition also needs no model, camera or native build:

```powershell
npm ci --ignore-scripts --prefix web/motionConnectorMediaPipe
npm test --prefix web/motionConnectorMediaPipe
npm run typecheck --prefix web/motionConnectorMediaPipe
npm pack ./web/motionConnectorMediaPipe --dry-run
```

`web-check` checks both browser modules. MediaPipe declaration checks use
Tasks Vision 1.1.0; it is development tooling, not bundled runtime code.
`motionConnectorWire_mediapipeInterop` decodes the tested body/hand/face
browser frames with the native codec. See the
[MediaPipe usage guide](../../web/motionConnectorMediaPipe/README.md).

### Native suites

| Test | Checks |
| --- | --- |
| `workspace_docs`, `workspace_docs_selftest` | every relative link and anchor resolves; every version and OpenUSD pin mirror agrees (`scripts/check_docs.py`) |
| `workspace_boundaries`, `workspace_boundaries_selftest` | every library's production sources and package declarations preserve the acquisition boundary; injected source, manifest, installed-config and actual CMake closure violations fail without an SDK or compiler |
| `motionConnectorWire_corpus`, `_messageGen` | every generated `openstrata.motion.frame/v1` message decodes, re-encodes or is refused exactly as `expectations.txt` says, and the committed corpus is what its generator writes |
| `motionConnector*_*Corpus` | each committed VMC, mocopi and VRChat OSC capture reaches the shared connector's `MotionFrame` path with stable frame counts |
| `motionConnectorWebSocket_framingCorpus`, `_messageCorpus`, `_corpusGen` | every generated byte stream gives the session's events, close status and byte that `expectations.txt` says; every message capture gives the connector's frames and diagnostics; and the committed corpus is what its generator writes |
| `motionConnectorWebSocket_loopback` | both directions in both roles over loopback sockets, the `Origin` and peer bounds, reconnects and silence. It binds sockets, so a lane that forbids binds excludes it by name |
| `motion_connect_inspect_*` | the CLI replays representative UDP and WebSocket captures through the shared contract |
| `motion_connect_bridge_*` | four capture sources reach WebSocket `dump`; an independent client checks listen/path/origin policy, numbering, receive times, pacing and close 1001; live observations, silence, limits and open failures are checked |
| `motion_connect_record_*`, `motion_connect_capture_lifecycle` | four live sources save byte-exact input and per-record peers before decoding; capture replay parity, silent duration expiry, frame limits, open/output errors and capture lifecycle are checked |
| `workspace_installed_consumer` | the tree installs into a clean prefix that names no source or build path, and a project copied outside the repository consumes every package the build installed, each of which `tests/installed_consumer/packages.json` must list; when the build has `motion_connect`, the installed one passes `list`, `dump`, `inspect`, silent `record` and argument checks from the prefix |
| `motionConnectorMocopi_frameSource` | source missing/loss/duplicate and restart facts, timestamps and diagnostic identity, with shared-connector parity over all captures |
| `motionConnectorVmc_frameSource` | source observations retain missing/stale/duplicate facts, restart timestamps, receive-clock fallback and diagnostic identity without downstream intake policy |

`workspace_installed_consumer` scans installed headers/configs, checks each
imported target's closure and compiles every installed library header with
discovery of `motionSampling`, `motionRecording`, `motionRetarget` and
`motionUsd` disabled. When VMC or mocopi is built, it also runs that connector's
acquisition classes. Their existing
intake/sampling/restart-policy tests link downstream packages explicitly and
carry the `consumer-integration` label. The private intake compositions live
under each connector's `tests/consumer/`. Recorder raw capture and inspection
use acquisition paths alone; `--export-trace` invokes the downstream writer
in a separate replay pass. VMC no longer prints downstream `intake:` statistics.

`ctest -LE installed-consumer` leaves the second project out.

The acquisition boundary scans and their mutation tests also run without an
OpenUSD runtime, in the `boundaries-check` workflow or locally:

```powershell
python scripts/check_boundaries.py
python tests/test_boundaries.py
```

The mutation tests require CMake 3.22 or later but configure projects with no
compiler. Actual dependency-closure checks also run during library configure,
including standalone builds with tests disabled.

## Replaying the recorded device sessions

The mocopi and VRChat OSC device sessions are manifest rows with no bytes in
the repository ([WORKSPACE.md §5](../architecture/WORKSPACE.md#5-test-data)).
An operator who kept the captures names their directory at configure time,
and `workspace_recorded_sessions` replays every row against it:

```powershell
$env:MOTIONCONNECTORS_RECORDED_SESSIONS = "$HOME/mocopi_sessions"
ost build
ost test
```

`-DMOTIONCONNECTORS_RECORDED_SESSIONS=<dir>` does the same on a plain CMake
configure. Captures are found by their sha256, so the directory's layout does
not matter. For each row, `scripts/check_recorded_sessions.py` requires the
record tool's `--inspect` to reproduce the receive statistics, the length
census and the per-address counts. A saved `<capture>.inspect.txt` beside the
capture must match the new output byte for byte. The raised diagnostics must
be exactly `expectedDiagnostics`. For mocopi, `--export-trace` must reproduce
each session's frames, sender rate and hips path. Finally,
`motion_connect inspect` must deliver the same frames through the shared
contract. Without the variable the test is not registered, so CI never runs
it. A row whose capture is absent is skipped; `--require-all` makes it a
failure.
