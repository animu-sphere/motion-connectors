# motionConnectorWebXR

Caller-driven browser acquisition of viewer, controller **grip** and WebXR
hand-joint poses, relative to one caller-owned reference space. The npm module
exports `WebXRConnector` and TypeScript declarations with no runtime dependency.
It emits the tracker-only JavaScript value shape of
[`openstrata.motion.frame/v1`](../../docs/design/FRAME_WIRE_FORMAT.md), ready for
`JSON.stringify`. It creates no session, animation loop, camera or socket.

## Session and frame ownership

The caller requests the XR session and features (including `hand-tracking`
when wanted), chooses a reference space and owns the animation loop. Acquire
inside the active XR animation callback; `poll()` only drains the bounded
queue and returns a frame or `undefined`.

```js
import { WebXRConnector } from "@openstrata/motion-connector-webxr";

// session and referenceSpace belong to the application.
const connector = new WebXRConnector({ session, referenceSpace });
const status = connector.open({ bufferMode: "latest" });
if (!status.succeeded) throw new Error(status.message);

function onFrame(callbackTime, xrFrame) {
  connector.acquire(xrFrame, callbackTime, performance.now() / 1000);
  for (let frame; (frame = connector.poll());) consume(frame);
  session.requestAnimationFrame(onFrame);
}
session.requestAnimationFrame(onFrame);
// On application shutdown: connector.close(); the application ends session.
```

`callbackTime` is the XR animation callback timestamp in milliseconds. It is
converted to seconds with `localMonotonic` clock domain and a browser time
origin; it is not a device clock or a receiver-aligned timestamp. Receive time
is the caller's local monotonic clock in seconds. Source time must strictly
advance; receive time may repeat but must not regress. Invalid time publishes
nothing and consumes no API query or frame number. The connector does not use
the compositor's future `predictedDisplayTime` as an observation timestamp.

Constructor options `viewer`, `controllers` and `hands` default to true;
disable inputs that the session will not supply. At least one must be enabled.
`open()` accepts only `webxr.observations.v1`, no coordinate override,
`latest` / `ordered` / `lossless` mode, and capacity 1–4096 (default 1).
Latest always keeps one frame; Ordered drops the oldest at capacity; Lossless
refuses **before querying WebXR**, allowing the same sample to be retried while
the XR frame is still active after polling. Buffer stats count pushes, polls,
drops, skips and Lossless refusals. Reopen clears queues, counters and history.

The session's `end` event makes the connector Error and requires a replacement
session/connector. Reference-space `reset` also becomes Error; reopen after the
caller accepts the new space origin to start a fresh acquisition epoch. Neither
event nor an API exception silently mixes frames across coordinate epochs.
Earlier queued frames remain drainable until reopen or close. `close()` removes
listeners and queued frames and never destroys an XR object.

## Observations and coordinates

`webxr.observations.v1` is a tracking-space observation profile. Each frame has
one actor, `webxr:0`, and no semantic pose, root, inferred confidence or source
sequence. Capabilities are trackers and source timestamps, plus controllers
when enabled. Hand observations do not claim the semantic `hands` capability.
The existing `webxr.hand.v1` semantic profile remains reserved.

IDs are `head`, `controller:<handedness>:<inputId>` and
`hand:<handedness>:<inputId>:<XRHandJoint>`. Handedness is `left`, `right` or
`none`; input IDs number the session's input objects from 1 and remain stable
while objects are reordered. New input objects get new IDs; removed ones
report no further observations. A controller uses `gripSpace`; target rays,
buttons and gestures are not physical grip poses. A hand reports the 25 WebXR
joint names, not the 26 OpenXR indices. A physical hand's grip is not also
claimed as a controller. Input iteration is bounded to eight sources, and an
overflow refuses the entire sample.

The documented WebXR basis is right-handed, +Y up, −Z forward, metres. A
proper Y half-turn converts position to `(-x,y,-z)` and orientation to
`(w,-x,y,-z)` in the canonical basis. Valid components are independently
converted to float32; quaternions are normalized. Missing poses produce an ID
with absent components; invalid components are omitted independently with a
diagnostic. No previous value is reused. WebXR's emulated position is retained
as the API reported it, with Degraded state and a local diagnostic, never
turned into a confidence score. Assignment and anatomical reconstruction stay
downstream.

Specification evidence:
[WebXR spaces and poses](https://www.w3.org/TR/webxr/#spaces),
[XR animation callbacks](https://www.w3.org/TR/webxr/#xrframe),
[WebXR Hand Input](https://www.w3.org/TR/webxr-hand-input-1/).
This is documented basis evidence; deterministic tests are not device measurement.

## Diagnostics and validation

Open starts Connecting; an accepted complete sample becomes Connected.
Unavailable, emulated or invalid components become Degraded and may recover.
API failures publish no partial frame and become Error until reopen.
`getDiagnostics()` returns a snapshot of the latest open/acquisition or
session event, with the stable `WEBXR_*` code, severity, recoverability, source,
subject, receive timestamp and detail. Consumers retain diagnostic history;
state and diagnostics do not enter the frame. The catalog is in
[`DIAGNOSTICS.md`](../../docs/reference/DIAGNOSTICS.md#2-catalog).

From the repository root:

```powershell
npm test --prefix web/motionConnectorWebXR
npm pack ./web/motionConnectorWebXR --dry-run
```

The Node tests supply browser-shaped API fixtures, including physical
yaw/pitch/roll checks for every observation, availability/recovery, timestamps,
API/session failures, input identity and all buffer modes. They verify the
committed wire sample; `motionConnectorWire_webxrInterop` decodes that sample
using the independent C++ codec. `web-check` runs acquisition and package
checks without a browser, headset, SDK or OpenUSD. It also installs pinned
development tooling to typecheck public exports against browser WebXR types.
From the module directory, run `npm ci --ignore-scripts` and
`npm run typecheck` for that check. Runtime acquisition has no dependency. Native
builds require no Node. Live browser/hardware compatibility remains operator
integration evidence. This package is source-delivered; no npm publication is
claimed.
