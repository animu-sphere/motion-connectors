// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionConnectorCore/FrameBuffer.h"
#include "motionConnectorCore/IMotionConnector.h"
#include <openxr/openxr.h>

#include <memory>
#include <string>
#include <vector>

namespace openstrata::connectors::openxr {

// Borrowed handles. The caller owns session creation, extension enablement,
// action synchronization, the frame loop and destruction of every handle.
struct SessionInput {
    XrInstance instance = XR_NULL_HANDLE;
    XrSpace baseSpace = XR_NULL_HANDLE;
    XrSpace headSpace = XR_NULL_HANDLE;
    XrSpace leftController = XR_NULL_HANDLE;
    XrSpace rightController = XR_NULL_HANDLE;
    XrHandTrackerEXT leftHand = XR_NULL_HANDLE;
    XrHandTrackerEXT rightHand = XR_NULL_HANDLE;
    XrBodyTrackerFB body = XR_NULL_HANDLE;
};

enum class DiagnosticCode {
    ConfigurationInvalid,
    FunctionUnavailable,
    LocateFailed,
    TimestampInvalid,
    TransformInvalid,
    TrackingUnavailable,
    BodyTimeMismatch,
    BufferFull,
};

enum class Severity { Warning, Error };

struct Diagnostic {
    DiagnosticCode code;
    std::string subject;
    double timestamp = 0.0;
    XrResult result = XR_SUCCESS;
    XrTime sourceTime = 0;
};

const char* CodeName(DiagnosticCode code) noexcept;
Severity CodeSeverity(DiagnosticCode code) noexcept;
bool CodeRecoverable(DiagnosticCode code) noexcept;
const char* CodeDetail(DiagnosticCode code) noexcept;

class OpenXRConnector final : public core::IMotionConnector {
public:
    // Function resolution can be supplied by a deterministic runtime fixture.
    explicit OpenXRConnector(SessionInput input,
                             PFN_xrGetInstanceProcAddr resolve = xrGetInstanceProcAddr);
    core::Status Open(const core::ConnectorConfig& config) override;
    void Close() override;
    core::ConnectorState GetState() const override;
    core::ConnectorCapabilities GetCapabilities() const override;
    bool Poll(core::MotionFrame& out) override;

    // Call after the owner's xrWaitFrame / xrSyncActions, with its selected
    // XrTime (nanoseconds). Poll only drains this bounded acquisition queue.
    // receiveTimestamp is in the caller's local monotonic clock, in seconds.
    // Serialize Open/Close/Acquire; Poll may drain concurrently with Acquire.
    bool Acquire(XrTime time, double receiveTimestamp);
    core::FrameBufferStats GetBufferStats() const noexcept;
    // Bounded to the most recent Open/Acquire call; copying preserves history.
    const std::vector<Diagnostic>& GetDiagnostics() const noexcept;

private:
    bool _Resolve(const char* name, PFN_xrVoidFunction& function);
    bool _Result(XrResult result, const char* subject, double timestamp);
    void _Observe(core::ActorFrame& actor, std::string id, XrSpaceLocationFlags flags,
                  const XrPosef& pose, double timestamp, bool& degraded);
    SessionInput _input;
    PFN_xrGetInstanceProcAddr _resolve;
    PFN_xrLocateSpace _locateSpace = nullptr;
    PFN_xrLocateHandJointsEXT _locateHand = nullptr;
    PFN_xrLocateBodyJointsFB _locateBody = nullptr;
    core::ConnectorState _state = core::ConnectorState::Disconnected;
    std::unique_ptr<core::FrameBuffer> _buffer;
    std::vector<Diagnostic> _diagnostics;
    XrTime _lastTime = 0;
    double _lastReceive = 0.0;
    std::uint64_t _frameNumber = 0;
};

} // namespace openstrata::connectors::openxr
