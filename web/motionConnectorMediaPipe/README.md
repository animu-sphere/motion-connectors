# motionConnectorMediaPipe

Caller-driven acquisition from MediaPipe Tasks Pose, Hand and Face Landmarker
results into `openstrata.motion.frame/v1`. The caller owns the task, camera,
model/WASM assets, video timestamp and inference loop. `acquire` copies one
completed task result; `poll` only drains a bounded queue. No inference, camera,
socket, filter or anatomical reconstruction runs inside this module.

## Usage

```js
import { MediaPipeConnector } from "@openstrata/motion-connector-mediapipe";

// The caller has configured a PoseLandmarker with runningMode: "VIDEO", numPoses: 1.
const connector = new MediaPipeConnector({ part: "body", actorId: "performer:0" });
connector.open({ bufferMode: "ordered", bufferCapacity: 4 });

// Within the caller's inference loop; use the same local monotonic time for Tasks.
const timestamp = performance.now();
const result = poseLandmarker.detectForVideo(video, timestamp);
if (connector.acquire(result, timestamp, performance.now() / 1000)) {
  const frame = connector.poll();
  if (frame) consume(frame); // JSON.stringify(frame) is the existing native wire shape.
}
connector.close();
```

Select `part: "hands"` for HandLandmarker results (at most two hands of one
person, distinct reported sides), or `part: "face"` for FaceLandmarker results
(`numFaces: 1`, `outputFaceBlendshapes: true`). These are separate connectors
with separate profile IDs and clocks. The caller identifies the person;
this module performs no cross-task or multi-person association. More than one
pose or face is refused. Setting `actorId` does not establish spatial alignment.

## Observation contract

- `mediapipe.body.v1`: 33 position-only tracker observations, `body:<index>`.
  Metric `worldLandmarks` only; origin is the hip midpoint. Optional world
  landmark visibility is copied to confidence, without combining presence.
- `mediapipe.hands.v1`: 21 observations per detected hand,
  `hand:left/right:<index>`. The highest scored handedness category determines
  the reported side; ties, unknown sides and duplicate sides are refused.
  Side classification scores are not landmark confidence. Each hand has its
  own geometric-center origin: the two hands and body do not share translation.
  Side names follow the task result, including the caller's input mirroring.
- `mediapipe.face.v1`: `faceBlendshapes` categories become
  `mediapipe:<categoryName>` channels in a sparse `MotionPose`, with no root or
  joint rotations. Zero scores are retained; unreported categories are absent.
  Face mesh coordinates and facial transformation matrices are not acquired.

No source landmark is a parent-local joint rotation. Source index/name and
side hints live in the profiles and tracker IDs; semantic assignment and
generic body/hand solve belong downstream. The native tracker type and wire
format require no extension ([CC-O2](../../docs/design/CONNECTOR_CONTRACT.md#41-landmark-observations)).

Positions convert once as `(x, -y, -z)` and are rounded to float32. Metres and
the relative origins are vendor-documented; image-right +X, image-down +Y,
away-from-camera +Z world axes are **assumed**, pending labelled measurement.
Generated fixtures check that assumption, not actual device geometry. Normalized
image landmarks are never promoted to metres or used as a fallback.

Source time is the caller's local monotonic inference/video timestamp in
milliseconds, converted to seconds. Receive time is monotonic seconds. Source
time must strictly advance; receive time can stay equal. Reopen after a seek or
clock reset to start a new epoch. Frame numbers are decimal strings from 1.

## Failure and buffering

`open` moves Disconnected to Connecting; successful acquisition moves to
Connected or Degraded when diagnostics exist. Invalid configuration is Error.
Malformed topology or category data refuses the whole result without advancing
time/numbering or altering previously queued frames. A bad individual position
keeps its tracker ID with no position; invalid visibility omits confidence.
No previous value is filled in. An empty detection publishes empty actors;
disabled face blendshapes publish an actor without a pose, with a diagnostic.
Diagnostic snapshots contain only the most recent open/acquire call.

`latest` replaces queued frames, `ordered` drops the oldest at capacity,
and `lossless` refuses acquisition while full. For lossless, drain and retry
the same result/time; Tasks need not run again. Capacity is 1–4096, default 1.
`close` clears queued frames. `open` also resets timing, diagnostics and stats.

## Package and verification

Independent ESM npm source package, with TypeScript declarations and three
declarative profiles exported by package subpath. It has no runtime import or
dependency; the caller installs MediaPipe Tasks and supplies its results.
Pinned development dependencies check against the real Tasks result types.
Models and WASM are not distributed here.

```powershell
npm ci --ignore-scripts
npm test
npm run typecheck
npm pack --dry-run
```

Tests cover physical axis directions, metric/image separation, availability,
visibility, side identity under ordering changes, sparse face channels,
malformed/oversized input, clocks, recovery and every buffer mode. Golden
browser frames are decoded by `motionConnectorWire_mediapipeInterop`.
Hardware or an ML model is not required; real inference/hardware compatibility
and basis measurement remain operator evidence.

Vendor references: [Pose](https://developers.google.com/edge/mediapipe/solutions/vision/pose_landmarker/web_js),
[Hand](https://developers.google.com/edge/mediapipe/solutions/vision/hand_landmarker/web_js),
[Face](https://developers.google.com/edge/mediapipe/solutions/vision/face_landmarker/web_js).
