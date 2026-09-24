# Building

How to build and test `motion-connectors` from a checkout. The tree builds the
shared core, transport and wire-format leaves, the VMC, mocopi and VRChat OSC
connectors, their record tools, and the `motion_connect` CLI. A build pins
OpenUSD and runs the source corpus, installed-consumer and workspace tests.

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
| `MOTIONCONNECTORS_BUILD_TOOLS` | `ON` at the top level | the recorders, and `motion_connect` when all three connectors are on |
| `MOTIONCONNECTORS_BUILD_TESTS` | `ON` at the top level | every component's tests and the workspace tests |

The transport and the wire format depend on nothing and are always built. The
core and the tracker layer link `usd-motion-plugins`' `motionCore`, and are
built whenever any connector is. With every connector off, configure asks for
no OpenUSD at all:

```powershell
cmake -S . -B build/minimal -G Ninja `
  -DMOTIONCONNECTORS_BUILD_VMC=OFF `
  -DMOTIONCONNECTORS_BUILD_MOCOPI=OFF `
  -DMOTIONCONNECTORS_BUILD_VRCHAT_OSC=OFF
```

A single component also configures on its own, against installed packages on
`CMAKE_PREFIX_PATH` -- for example `cmake -S libs/motionConnectorVmc -B ...`.
Its tests follow `<NAME>_BUILD_TESTS` (for example
`MOTIONCONNECTORVMC_BUILD_TESTS`), which defaults to on when it is the
top-level project.

Build output stays in the build tree. The runtime layout -- `bin/`, `lib/`,
`include/`, `share/` -- is what `cmake --install` writes to a prefix.

## What the tests check

| Test | Checks |
| --- | --- |
| `workspace_docs`, `workspace_docs_selftest` | every relative link and anchor resolves; every version and OpenUSD pin mirror agrees (`scripts/check_docs.py`) |
| `motionConnector*_*Corpus` | each committed VMC, mocopi and VRChat OSC capture reaches the shared connector's `MotionFrame` path with stable frame counts |
| `motion_connect_inspect_*` | the CLI replays representative captures through the shared contract |
| `workspace_installed_consumer` | the tree installs into a clean prefix that names no source or build path, and a project copied outside the repository consumes every package the build installed, each of which `tests/installed_consumer/packages.json` must list |

`ctest -LE installed-consumer` leaves the second project out.
