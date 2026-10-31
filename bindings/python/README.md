# motion_connectors

Python acquisition over the [C ABI](../../docs/design/C_ABI.md), ending at
`Frame`. Filtering, solving, retargeting, recording and USD are downstream
([DESIGN_POLICY §19.1](../../docs/design/DESIGN_POLICY.md#191-the-python-binding)
records this binding's decisions). Availability is in the
[capability matrix](../../docs/reference/CAPABILITY_MATRIX.md#3-tools-and-bindings).

```python
import time

from motion_connectors import BufferMode, open_connector, joint_names

names = joint_names()
with open_connector("vmc", port=39539, buffer_mode=BufferMode.ORDERED,
                    buffer_capacity=64) as connector:
    while running:
        for frame in connector.frames():       # drains what is pending, then ends
            for actor in frame.actors:
                if actor.pose is not None:
                    rotations = dict(zip(names, actor.pose.local_rotations))
                    hips = rotations["hips"]   # (w, x, y, z), or None when absent
                for tracker in actor.trackers:
                    print(tracker.tracker_id, tracker.position)
        for diagnostic in connector.diagnostics():
            print(diagnostic.code, diagnostic.subject, diagnostic.detail)
        time.sleep(1 / 60)                     # the caller's tick: nothing here blocks
```

## What to rely on

- **A `Frame` is a plain immutable value.** It holds no native handle, so it can
  outlive its connector, be kept, be pickled and cross threads.
- **`None` is the only absent spelling.** An absent joint, tracker part or
  timestamp is `None`, never zero or identity.
- **Nothing blocks and nothing loops.** `frames()` and `diagnostics()` drain what
  is pending and end. They are separate reads, so neither consumes the other.
- **A port is always stated.** `port=0` asks the OS for one, and
  `connector.endpoint` reports it. `sources()` lists each source's conventional
  port.
- **A failed open is a state.** `OpenFailedError` carries the reason. A `Connector`
  created directly stays usable and may `open` again. `close` is idempotent, keeps
  undelivered diagnostics and may be followed by another `open`.
- **A connector may be shared between threads.** Each one holds a lock around the
  C ABI's one-thread-at-a-time handle.
- **A frame with a name that is not UTF-8 is not delivered.** `stats.refused_frames`
  counts it, and a `C_ABI_FRAME_NOT_UTF8` diagnostic names the field
  ([C_ABI §7](../../docs/design/C_ABI.md#7-strings)).

Without a socket or a device, `push_input` and `flush_input` take recorded bytes
through the same decode and assembly as live input
([C_ABI §6.2](../../docs/design/C_ABI.md#62-the-hardware-free-input-path)).

## Finding the library

The package is pure Python and needs no build step, but its process has to load
`motionConnectorCAbi` and that library's OpenUSD closure
([C_ABI §2](../../docs/design/C_ABI.md#2-package-and-sources)). It looks once, at
first use, in this order:

1. the path in `MOTION_CONNECTORS_LIBRARY`, and only that when it is set;
2. the install prefix the package itself is in: `cmake --install` puts it in
   `<prefix>/lib/python`, so put that directory on `PYTHONPATH`;
3. the platform's search by file name.

On Windows, absolute `PATH` entries are searched for the library's dependencies,
as OpenUSD's `pxr` does; an activated `ost` runtime already has them there. On
Linux and macOS, `LD_LIBRARY_PATH` or `DYLD_LIBRARY_PATH` must reach a relocated
prefix's OpenUSD.

A library of another C ABI major raises `LibraryError`, as a library that cannot
be loaded does. Importing the package never does.

## Tests

No test needs hardware. Only `motionConnectorPython_loopback` binds a socket, on
loopback at an OS-chosen port.

```powershell
ctest --test-dir build/<target> -R motionConnectorPython --output-on-failure
```
