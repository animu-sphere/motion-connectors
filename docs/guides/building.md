# Building

How to build and test `motion-connectors` from a checkout. The tree holds no
component yet (the [roadmap](../roadmap/current.md) says what arrives first),
so a build today configures the root project, pins OpenUSD, and runs the
workspace tests. The commands do not change when the first library arrives.

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

## What the tests check

| Test | Checks |
| --- | --- |
| `workspace_docs`, `workspace_docs_selftest` | every relative link and anchor resolves; every version and OpenUSD pin mirror agrees (`scripts/check_docs.py`) |
| `workspace_installed_consumer` | the tree installs into a clean prefix that names no source or build path, and a project copied outside the repository consumes every package `tests/installed_consumer/packages.json` lists (none yet) |

`ctest -LE installed-consumer` leaves the second project out.
