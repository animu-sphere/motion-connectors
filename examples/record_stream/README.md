# `record_stream`

A consumer program that records a source as motion traces:

```text
connector → MotionFrame → LiveCaptureSource → MotionRecorder → motion-capture-trace
└─ motion-connectors ─┘   └──────────── usd-motion-plugins ──────────────┘
```

It shows where acquisition ends and motion treatment begins. The connector
libraries stop at `MotionFrame`
([DESIGN_POLICY §47](../../docs/design/DESIGN_POLICY.md#47-external-acquisition-boundary)),
so the `motionRecording` dependency belongs to this executable alone. What the
intake, the recorder and the trace mean is
[`usd-motion-plugins`' motion contract](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/MOTION_CONTRACT.md).
For raw packets rather than canonical motion, use
[`motion_connect record`](../../docs/design/MOTION_CONNECT.md#5-record).

## Running it

Replay a capture, which needs no device or socket:

```powershell
record_stream --source vmc `
  --capture libs/motionConnectorVmc/tests/corpus/arm-raise-30hz.vmcpackets `
  --output takes
```

Record a live session on the source's default port until Ctrl-C, or set a limit:

```powershell
record_stream --source mocopi --output takes --duration 30
```

`--source` takes `vmc`, `mocopi` or `websocket`; a live `websocket` source also
requires `--port`. `--listen`, `--port`, `--duration` and `--max-frames` apply to
a live session. VRChat OSC is not offered, because it delivers tracker
observations with no pose, and a solve needs an operator's assignment.

`--output` names a new or empty directory, refused before the source opens. Each
take becomes `actor-<ordinal>.take-<number>.trace`, ordinals counting actors in
the order they first appeared. An actor id comes from the sender, so it appears
in the report and never in a file name. The takes are written when the session
ends, and memory grows with the session until then.

The exit status is 0 when something was recorded, 1 when the source delivered no
pose or a trace could not be written, and 2 for a usage error.

## The choices it makes

These are this consumer's policy, marked `Consumer policy` in
[main.cpp](main.cpp). A different consumer may choose differently without
changing a connector.

- **One intake and one recorder per actor**, with the intake's default
  conditioning: a missing joint holds its last value and a missing root velocity
  is derived. The report counts both, and each trace is therefore conditioned
  motion, not what the connector delivered. For the unconditioned values, use a
  source recorder's `--export-trace`.
- **Evaluation on the source's clock**, once at each accepted pose. Nothing is
  resampled or interpolated, and a replay records the same trace as its session.
  A runtime ticking on its own clock aligns the intake to it instead.
- **A take ends where the source's time goes back.** One trace is one clock, so
  the first pose of a new epoch resets the intake and opens a new take rather
  than being spliced on. A restart whose clock advances continues the take; its
  diagnostic is still printed. A consumer that needs a boundary there can act on
  that diagnostic.

Tracker observations are counted and otherwise ignored, and a connector's
diagnostics go to stderr beside the frames.

## Building it outside this repository

The directory is self-contained. Copy it and configure against installed packages
of the three connectors and `motionRecording`, as the
[installed-consumer lane](../../docs/guides/building.md#native-suites) does:

```powershell
cmake -S record_stream -B build "-DCMAKE_PREFIX_PATH=<connectors>;<usd-motion-plugins>;<openusd>"
cmake --build build --config Release
```

Availability and test evidence are in the
[capability matrix](../../docs/reference/CAPABILITY_MATRIX.md#3-tools-and-bindings).
