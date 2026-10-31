# motionConnectorCAbi

The in-process acquisition C ABI over the connectors, the boundary language
bindings are built on. [C_ABI.md](../../docs/design/C_ABI.md) owns the contract:
versioning, ownership, lifetimes, errors and strings. The declarations are in
[`MotionConnector.h`](include/motionConnectorCAbi/MotionConnector.h).

```c
#include "motionConnectorCAbi/MotionConnector.h"

motion_connector_t* connector = NULL;
if (motion_connector_create("vmc", &connector) != MOTION_STATUS_OK) { /* ... */ }

MOTION_INIT(motion_connector_config_t, config);
config.port = 39539;                  /* always stated; loopback by default */
config.buffer_mode = MOTION_BUFFER_ORDERED;
config.buffer_capacity = 64;
if (motion_connector_open(connector, &config) == MOTION_STATUS_OPEN_FAILED) {
    motion_string_view_t why;         /* a state, not a fault: it may be retried */
    motion_connector_last_error(connector, &why);
}

/* The caller's tick. Poll never blocks, and the frame is the caller's. */
motion_frame_t* frame = NULL;
while (motion_connector_poll(connector, &frame) == MOTION_STATUS_OK) {
    MOTION_INIT(motion_frame_info_t, info);
    motion_frame_get_info(frame, &info);
    /* motion_frame_get_actor, _get_pose, _get_channel, _get_tracker */
    motion_frame_release(frame);
}

motion_diagnostic_t* diagnostic = NULL;
while (motion_connector_poll_diagnostic(connector, &diagnostic) == MOTION_STATUS_OK) {
    motion_diagnostic_release(diagnostic);
}

motion_connector_destroy(connector);
```

## Consuming it

```cmake
find_package(motionConnectorCAbi CONFIG REQUIRED)
target_link_libraries(app PRIVATE motionConnectorCAbi::motionConnectorCAbi)
```

The header is C and includes no connector type, so a consumer needs no C++ and
none of the connector packages. The process must still be able to load the shared
library and its OpenUSD `gf` closure at run time
([C_ABI §2](../../docs/design/C_ABI.md#2-package-and-sources)).

## Tests

No test needs hardware. Only `motionConnectorCAbi_loopback` binds a socket, on
loopback at an OS-chosen port.

```powershell
ctest --test-dir build/<target> -R motionConnectorCAbi --output-on-failure
```
