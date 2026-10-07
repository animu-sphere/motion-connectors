// SPDX-License-Identifier: Apache-2.0
#include "motionConnectorOpenXR/Connector.h"
#include "motionCore/BasisConversion.h"

#include <array>
#include <cmath>
#include <utility>

namespace openstrata::connectors::openxr {
namespace {
// Right handed, +Y up, -Z forward -> +Z forward: proper Y half-turn.
constexpr openstrata::motion::SignedPermutationBasis Basis{{0, 1, 2}, {true, false, true}, 1, 1.0};
constexpr XrSpaceLocationFlags Valid =
    XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
constexpr XrSpaceLocationFlags Tracked =
    XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
} // namespace

const char*
CodeName(DiagnosticCode code) noexcept
{
    switch (code) {
    case DiagnosticCode::ConfigurationInvalid:
        return "OPENXR_CONFIGURATION_INVALID";
    case DiagnosticCode::FunctionUnavailable:
        return "OPENXR_FUNCTION_UNAVAILABLE";
    case DiagnosticCode::LocateFailed:
        return "OPENXR_LOCATE_FAILED";
    case DiagnosticCode::TimestampInvalid:
        return "OPENXR_TIMESTAMP_INVALID";
    case DiagnosticCode::TransformInvalid:
        return "OPENXR_TRANSFORM_INVALID";
    case DiagnosticCode::TrackingUnavailable:
        return "OPENXR_TRACKING_UNAVAILABLE";
    case DiagnosticCode::BufferFull:
        return "OPENXR_BUFFER_FULL";
    case DiagnosticCode::BodyTimeMismatch:
        return "OPENXR_BODY_TIME_MISMATCH";
    }
    return "OPENXR_UNKNOWN";
}

Severity
CodeSeverity(DiagnosticCode code) noexcept
{
    return (code == DiagnosticCode::ConfigurationInvalid ||
            code == DiagnosticCode::FunctionUnavailable || code == DiagnosticCode::LocateFailed)
               ? Severity::Error
               : Severity::Warning;
}

bool
CodeRecoverable(DiagnosticCode code) noexcept
{
    return CodeSeverity(code) != Severity::Error;
}

const char*
CodeDetail(DiagnosticCode code) noexcept
{
    switch (code) {
    case DiagnosticCode::ConfigurationInvalid:
        return "The fixed profile, input handles or queue configuration is invalid.";
    case DiagnosticCode::FunctionUnavailable:
        return "A required OpenXR function could not be resolved.";
    case DiagnosticCode::LocateFailed:
        return "OpenXR did not return a successful, well-formed location result.";
    case DiagnosticCode::TimestampInvalid:
        return "Acquisition time is invalid or does not advance, or receive time regresses.";
    case DiagnosticCode::TransformInvalid:
        return "A valid-marked component is non-finite or has a zero quaternion.";
    case DiagnosticCode::TrackingUnavailable:
        return "A component is invalid or not actively tracked.";
    case DiagnosticCode::BufferFull:
        return "The lossless acquisition queue is full; drain it before retrying.";
    case DiagnosticCode::BodyTimeMismatch:
        return "Body tracking returned a different instant from the requested acquisition time.";
    }
    return "Unknown OpenXR diagnostic.";
}

OpenXRConnector::OpenXRConnector(SessionInput input, PFN_xrGetInstanceProcAddr resolve)
    : _input(input), _resolve(resolve), _buffer(std::make_unique<core::FrameBuffer>())
{
}

bool
OpenXRConnector::_Resolve(const char* name, PFN_xrVoidFunction& function)
{
    function = nullptr;
    if (_resolve(_input.instance, name, &function) == XR_SUCCESS && function)
        return true;
    _diagnostics.push_back({DiagnosticCode::FunctionUnavailable, name});
    _state = core::ConnectorState::Error;
    return false;
}

core::Status
OpenXRConnector::Open(const core::ConnectorConfig& config)
{
    Close();
    _diagnostics.clear();
    _lastTime = 0;
    _lastReceive = 0.0;
    _frameNumber = 0;
    if (config.sourceProfile != "openxr.observations.v1" || !config.coordinateConversion.empty() ||
        config.bufferCapacity == 0 || !_resolve || _input.instance == XR_NULL_HANDLE ||
        _input.baseSpace == XR_NULL_HANDLE ||
        (_input.headSpace == XR_NULL_HANDLE && _input.leftController == XR_NULL_HANDLE &&
         _input.rightController == XR_NULL_HANDLE && _input.leftHand == XR_NULL_HANDLE &&
         _input.rightHand == XR_NULL_HANDLE && _input.body == XR_NULL_HANDLE)) {
        _state = core::ConnectorState::Error;
        _diagnostics.push_back({DiagnosticCode::ConfigurationInvalid, "config"});
        return core::Status::Failure(
            "OpenXR requires a fixed observations profile and borrowed input handles");
    }
    PFN_xrVoidFunction function = nullptr;
    if (_input.headSpace || _input.leftController || _input.rightController) {
        if (!_Resolve("xrLocateSpace", function))
            return core::Status::Failure("xrLocateSpace unavailable");
        _locateSpace = reinterpret_cast<PFN_xrLocateSpace>(function);
    }
    if (_input.leftHand || _input.rightHand) {
        if (!_Resolve("xrLocateHandJointsEXT", function))
            return core::Status::Failure("XR_EXT_hand_tracking unavailable");
        _locateHand = reinterpret_cast<PFN_xrLocateHandJointsEXT>(function);
    }
    if (_input.body) {
        if (!_Resolve("xrLocateBodyJointsFB", function))
            return core::Status::Failure("XR_FB_body_tracking unavailable");
        _locateBody = reinterpret_cast<PFN_xrLocateBodyJointsFB>(function);
    }
    _buffer = std::make_unique<core::FrameBuffer>(config.bufferCapacity, config.bufferMode);
    _state = core::ConnectorState::Connecting;
    return core::Status::Ok();
}

void
OpenXRConnector::Close()
{
    _buffer->Clear();
    _locateSpace = nullptr;
    _locateHand = nullptr;
    _locateBody = nullptr;
    _state = core::ConnectorState::Disconnected;
}

core::ConnectorState
OpenXRConnector::GetState() const
{
    return _state;
}

core::ConnectorCapabilities
OpenXRConnector::GetCapabilities() const
{
    core::ConnectorCapabilities caps;
    caps.Add(core::ConnectorCapability::Trackers);
    caps.Add(core::ConnectorCapability::SourceTimestamps);
    if (_input.leftController || _input.rightController)
        caps.Add(core::ConnectorCapability::Controllers);
    if (_input.body) {
        caps.Add(core::ConnectorCapability::Confidence);
    }
    return caps;
}

bool
OpenXRConnector::Poll(core::MotionFrame& out)
{
    return _buffer->Poll(out);
}
core::FrameBufferStats
OpenXRConnector::GetBufferStats() const noexcept
{
    return _buffer->GetStats();
}
const std::vector<Diagnostic>&
OpenXRConnector::GetDiagnostics() const noexcept
{
    return _diagnostics;
}

bool
OpenXRConnector::_Result(XrResult result, const char* subject, double timestamp)
{
    // Positive statuses such as SESSION_LOSS_PENDING must not publish an
    // uninitialized location as a successful observation.
    if (result == XR_SUCCESS)
        return true;
    _diagnostics.push_back({DiagnosticCode::LocateFailed, subject, timestamp, result});
    _state = core::ConnectorState::Error;
    return false;
}

void
OpenXRConnector::_Observe(core::ActorFrame& actor, std::string id, XrSpaceLocationFlags flags,
                          const XrPosef& pose, double timestamp, bool& degraded)
{
    core::TrackerObservation observation;
    observation.trackerId = std::move(id);
    if (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {
        const auto& p = pose.position;
        if (std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)) {
            observation.position = motion::ApplyBasisToPosition(Basis, pxr::GfVec3f(p.x, p.y, p.z));
            observation.hasPosition = true;
        } else
            _diagnostics.push_back(
                {DiagnosticCode::TransformInvalid, observation.trackerId, timestamp});
    }
    if (flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
        const auto& q = pose.orientation;
        const double length = std::sqrt(double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z +
                                        double(q.w) * q.w);
        if (std::isfinite(length) && length > 0.0) {
            const pxr::GfQuatf rotation(
                float(q.w / length),
                pxr::GfVec3f(float(q.x / length), float(q.y / length), float(q.z / length)));
            observation.rotation = motion::ApplyBasisToRotation(Basis, rotation);
            observation.hasRotation = true;
        } else
            _diagnostics.push_back(
                {DiagnosticCode::TransformInvalid, observation.trackerId, timestamp});
    }
    if (!observation.hasPosition || !observation.hasRotation ||
        (flags & (Valid | Tracked)) != (Valid | Tracked)) {
        degraded = true;
        _diagnostics.push_back(
            {DiagnosticCode::TrackingUnavailable, observation.trackerId, timestamp});
    }
    actor.trackers.push_back(std::move(observation));
}

bool
OpenXRConnector::Acquire(XrTime time, double receiveTimestamp)
{
    _diagnostics.clear();
    if (_state == core::ConnectorState::Disconnected || _state == core::ConnectorState::Error)
        return false;
    if (time <= 0 || time <= _lastTime || !std::isfinite(receiveTimestamp) ||
        receiveTimestamp < 0.0 || (_frameNumber && receiveTimestamp < _lastReceive)) {
        _diagnostics.push_back({DiagnosticCode::TimestampInvalid,
                                "time",
                                std::isfinite(receiveTimestamp) ? receiveTimestamp : 0.0});
        _state = core::ConnectorState::Degraded;
        return false;
    }
    // Refuse before reading the SDK, so a caller may drain and retry the same
    // instant without losing a sample or consuming a frame number.
    if (_buffer->Mode() == core::BufferMode::Lossless && _buffer->Size() >= _buffer->Capacity()) {
        _diagnostics.push_back({DiagnosticCode::BufferFull, "frame", receiveTimestamp});
        _state = core::ConnectorState::Degraded;
        return false;
    }
    core::MotionFrame frame;
    frame.sourceProfile = "openxr.observations.v1";
    frame.timing.sourceTimestamp = double(time) * 1e-9;
    frame.timing.sourceClock = core::ClockDomain::Device;
    frame.timing.receiveTimestamp = receiveTimestamp;
    core::ActorFrame actor;
    actor.actor = "openxr:0";
    bool degraded = false;
    const std::array<std::pair<XrSpace, const char*>, 3> spaces{
        {{_input.headSpace, "head"},
         {_input.leftController, "controller:left"},
         {_input.rightController, "controller:right"}}};
    for (const auto& [space, id] : spaces) {
        if (!space)
            continue;
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        if (!_Result(_locateSpace(space, _input.baseSpace, time, &location), id, receiveTimestamp))
            return false;
        _Observe(actor, id, location.locationFlags, location.pose, receiveTimestamp, degraded);
    }
    const std::array<std::pair<XrHandTrackerEXT, const char*>, 2> hands{
        {{_input.leftHand, "hand:left:"}, {_input.rightHand, "hand:right:"}}};
    for (const auto& [hand, prefix] : hands) {
        if (!hand)
            continue;
        std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joints{};
        const XrHandJointsLocateInfoEXT info{
            XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT, nullptr, _input.baseSpace, time};
        XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
                                          nullptr,
                                          XR_FALSE,
                                          uint32_t(joints.size()),
                                          joints.data()};
        if (!_Result(_locateHand(hand, &info, &locations), prefix, receiveTimestamp))
            return false;
        if (locations.jointCount != joints.size())
            return _Result(XR_ERROR_VALIDATION_FAILURE, prefix, receiveTimestamp);
        for (std::size_t i = 0; i < joints.size(); ++i)
            _Observe(actor,
                     std::string(prefix) + std::to_string(i),
                     locations.isActive ? joints[i].locationFlags : 0,
                     joints[i].pose,
                     receiveTimestamp,
                     degraded);
    }
    if (_input.body) {
        std::array<XrBodyJointLocationFB, XR_BODY_JOINT_COUNT_FB> joints{};
        const XrBodyJointsLocateInfoFB info{
            XR_TYPE_BODY_JOINTS_LOCATE_INFO_FB, nullptr, _input.baseSpace, time};
        XrBodyJointLocationsFB locations{XR_TYPE_BODY_JOINT_LOCATIONS_FB};
        locations.jointCount = uint32_t(joints.size());
        locations.jointLocations = joints.data();
        if (!_Result(_locateBody(_input.body, &info, &locations), "body", receiveTimestamp))
            return false;
        if (locations.jointCount != joints.size() ||
            (locations.isActive &&
             (!std::isfinite(locations.confidence) || locations.confidence < 0.0f ||
              locations.confidence > 1.0f || locations.time <= 0)))
            return _Result(XR_ERROR_VALIDATION_FAILURE, "body", receiveTimestamp);
        bool bodyAvailable = locations.isActive != XR_FALSE;
        if (bodyAvailable && locations.time != time) {
            _diagnostics.push_back({DiagnosticCode::BodyTimeMismatch,
                                    "body",
                                    receiveTimestamp,
                                    XR_SUCCESS,
                                    locations.time});
            degraded = true;
            if (actor.trackers.empty())
                // Body-only samples retain actual SDK time, including repeats
                // or regressions. Consumers choose their own temporal policy.
                frame.timing.sourceTimestamp = double(locations.time) * 1e-9;
            else
                // A frame describes one instant, so don't combine a different
                // body instant with the head/controller/hand acquisition.
                bodyAvailable = false;
        }
        for (std::size_t i = 0; i < joints.size(); ++i) {
            _Observe(actor,
                     "body:" + std::to_string(i),
                     bodyAvailable ? joints[i].locationFlags : 0,
                     joints[i].pose,
                     receiveTimestamp,
                     degraded);
            if (bodyAvailable)
                actor.trackers.back().confidence = locations.confidence;
        }
    }
    frame.actors.push_back(std::move(actor));
    frame.frameNumber = _frameNumber + 1;
    if (_buffer->Push(std::move(frame)) == core::FramePushResult::Backpressure) {
        _diagnostics.push_back({DiagnosticCode::BufferFull, "frame", receiveTimestamp});
        _state = core::ConnectorState::Degraded;
        return false;
    }
    ++_frameNumber;
    _lastTime = time;
    _lastReceive = receiveTimestamp;
    _state = degraded ? core::ConnectorState::Degraded : core::ConnectorState::Connected;
    return true;
}
} // namespace openstrata::connectors::openxr
