# motionConnectorOpenXR

Optional OpenXR loader acquisition implementing `IMotionConnector`. It locates
head and controller spaces, `XR_EXT_hand_tracking` joints and
`XR_FB_body_tracking` upper-body joints relative to one caller-selected base
space, then emits normalized `TrackerObservation`s in `MotionFrame`.

Current capability status, test evidence and release availability are in the
[capability matrix](../../docs/reference/CAPABILITY_MATRIX.md).

## Session ownership

Construct `OpenXRConnector` with `SessionInput` borrowed handles. The caller
creates the instance/session, enables extensions, creates the VIEW space for
the head, controller action spaces, hand trackers and a default FB body tracker,
and keeps them alive until the connector is closed. All handles must belong
to that same running session. Only configured handles are queried. Changing a
session or base space requires a new connector; reopening clears queued frames,
numbers and timing history. The connector destroys no SDK object.

The owner runs `xrWaitFrame`, session events, `xrSyncActions` and the frame loop.
After synchronization, call `Acquire(predictedDisplayTime, receiveSeconds)`,
then `Poll(frame)`. `Poll` only drains the queue and makes no SDK call.
Calls to Open/Close/Acquire must be serialized; Poll may drain concurrently
with Acquire. Receive time is the caller's local monotonic clock, in seconds.
Source time is the selected positive `XrTime` divided by 1e9, with a
runtime-specific epoch (`ClockDomain::Device`), never aligned to receive time.
Body-only frames use the SDK's returned body time when it differs, preserving
repeated or regressing source samples for downstream interpretation.
`frameNumber` counts accepted acquisitions; there is no source sequence.

```cpp
using namespace openstrata::connectors;
openxr::SessionInput input;
input.instance = instance;
input.baseSpace = localSpace;
input.headSpace = viewSpace;
openxr::OpenXRConnector connector(input);
core::ConnectorConfig config;
config.sourceProfile = "openxr.observations.v1";
auto status = connector.Open(config);
// In the owner's synchronized frame loop, if status succeeded:
connector.Acquire(predictedDisplayTime, receiveSeconds);
core::MotionFrame frame;
while (connector.Poll(frame)) { /* consume normalized observations */ }
```

## Observations and refusals

The profile is `openxr.observations.v1`, installed as declarative JSON. Tracker
IDs are `head`, `controller:left/right`, `hand:left/right:<index>` (the 26 EXT
joint indices), and `body:<index>` (the 70 default FB upper-body joint indices).
Hand/body joints are tracking-space observations, not parent-local humanoid
rotations. They carry no `MotionPose`, root motion or inferred confidence. The
runtime supplies anatomical assignment and any downstream pose reconstruction.
Semantic hand poses, including any `openxr.hand.v1` interpretation, belong
to downstream motion processing rather than this observation provider.

Capabilities are trackers and source timestamps, plus controllers when a
controller handle is supplied and confidence when a body handle is supplied.
The FB frame confidence is copied to its active body observations. Body/hands
capability flags describe joints in a semantic pose, so are not claimed.
The profile lists the maximum capability set; the instance reports only its
configured inputs. Validity flags control each component independently;
inactive or missing components have false availability and never reuse old
values. Valid-but-untracked data is retained with degraded state. Invalid
marked values are refused per component; zero quaternions are invalid.

Coordinates use the documented right-handed +Y up, -Z forward metre basis.
`motionCore`'s signed permutation converts position to `(-x,y,-z)` and a
normalized quaternion to `(w,-x,y,-z)`, a proper Y half-turn of the basis.
Evidence remains documented until a labelled device session measures it.
The deterministic test physically checks yaw, pitch and roll in both
directions for head, hand and body observations.

FB body tracking can return an actual observation time different from the
requested time ([SDK record](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrBodyJointLocationsFB.html)).
Body-only acquisition keeps that actual time and reports the mismatch. A mixed
head/controller/hand frame cannot contain observations from a different instant:
its body components become unavailable with a diagnostic retaining the SDK's
body time in nanoseconds. Use a separate body-only connector to retain
asynchronous body observations. The connector performs no temporal alignment
or interpolation. Active body data with a nonpositive returned time is refused.

Open starts Connecting. A complete tracked acquisition becomes Connected;
invalid/untracked components become Degraded and later tracking recovers.
SDK failures (including session-loss statuses), bad counts and bad active body
confidence publish no partial frame and become Error; reopen after the owner
has restored the session. Earlier queued frames remain drainable. Invalid or
regressing timestamps are refused before SDK calls. Latest/Ordered expose
dropped counts through shared buffer stats; Lossless refuses before SDK calls
when full, so the caller can drain and retry the same instant.

`GetDiagnostics()` holds only the latest Open/Acquire diagnostics. Each record
has a code, subject, receive timestamp and SDK result for locate failures;
body time mismatch also records the reported SDK source time in nanoseconds;
`CodeName`, `CodeSeverity`, `CodeRecoverable` and `CodeDetail` supply the fixed
catalog. Consumers may copy records for history. The decoder trusts SDK array
storage, bounds outputs to 125 configured observations and makes no network,
filtering, retargeting, recording or stage calls.

## Build and validation

Requires an installed OpenXR-SDK **1.1.36 or newer**, including its CMake
`OpenXR::openxr_loader` target. See [third-party notices](THIRD_PARTY_NOTICES.md).
The workspace option `MOTIONCONNECTORS_BUILD_OPENXR` defaults OFF; selecting
another connector requires no OpenXR installation. Standalone configuration
finds installed `motionConnectorCore` and OpenXR packages.

`motionConnectorOpenXR_connector` supplies the same SDK function resolution
and locate calls as a fixture, testing coordinates, flags, inactive inputs,
timestamps, confidence, SDK failures, queue modes and reopen/close without a
runtime or headset. It links the real SDK loader. Installed-consumer coverage
resolves the package with downstream motion packages disabled. Hardware
session creation, graphics/headless bindings and measured device validation
remain caller integration work.
