// SPDX-License-Identifier: Apache-2.0
#include "motionConnectorOpenXR/Connector.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

using namespace openstrata::connectors;
using namespace openstrata::connectors::openxr;
namespace {
template <class T>
T
Handle(std::uintptr_t value)
{
    return reinterpret_cast<T>(value);
}
constexpr XrSpaceLocationFlags Flags =
    XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
    XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
XrSpaceLocationFlags flags = Flags;
XrPosef pose{{0, 0, 0, 1}, {1, 2, -3}};
XrResult result = XR_SUCCESS;
bool active = true;
bool missingFunction = false;
bool nullFunction = false;
bool badCount = false;
float confidence = 0.75f;
int calls = 0;
XrTime expectedTime = 0;
XrTime bodyTimeOffset = 0;

XrResult XRAPI_CALL
Locate(XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* out)
{
    assert(space != XR_NULL_HANDLE && base == Handle<XrSpace>(2));
    assert(time == expectedTime && out->type == XR_TYPE_SPACE_LOCATION && !out->next);
    ++calls;
    out->locationFlags = flags;
    out->pose = pose;
    return result;
}
XrResult XRAPI_CALL
Hand(XrHandTrackerEXT handle, const XrHandJointsLocateInfoEXT* info, XrHandJointLocationsEXT* out)
{
    assert(handle != XR_NULL_HANDLE && info->type == XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT);
    assert(info->baseSpace == Handle<XrSpace>(2) && info->time == expectedTime);
    assert(out->type == XR_TYPE_HAND_JOINT_LOCATIONS_EXT &&
           out->jointCount == XR_HAND_JOINT_COUNT_EXT);
    ++calls;
    out->isActive = active ? XR_TRUE : XR_FALSE;
    for (uint32_t i = 0; i < out->jointCount; ++i)
        out->jointLocations[i] = {flags, pose, 0.01f};
    if (badCount)
        out->jointCount = 0;
    return result;
}
XrResult XRAPI_CALL
Body(XrBodyTrackerFB handle, const XrBodyJointsLocateInfoFB* info, XrBodyJointLocationsFB* out)
{
    assert(handle != XR_NULL_HANDLE && info->type == XR_TYPE_BODY_JOINTS_LOCATE_INFO_FB);
    assert(info->baseSpace == Handle<XrSpace>(2) && info->time == expectedTime);
    assert(out->type == XR_TYPE_BODY_JOINT_LOCATIONS_FB &&
           out->jointCount == XR_BODY_JOINT_COUNT_FB);
    ++calls;
    out->isActive = active ? XR_TRUE : XR_FALSE;
    out->confidence = confidence;
    out->time = expectedTime + bodyTimeOffset;
    for (uint32_t i = 0; i < out->jointCount; ++i)
        out->jointLocations[i] = {flags, pose};
    if (badCount)
        out->jointCount = 0;
    return result;
}
XrResult XRAPI_CALL
Resolve(XrInstance instance, const char* name, PFN_xrVoidFunction* out)
{
    assert(instance == Handle<XrInstance>(1));
    *out = nullptr;
    if (missingFunction)
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    if (nullFunction)
        return XR_SUCCESS;
    if (!std::strcmp(name, "xrLocateSpace"))
        *out = reinterpret_cast<PFN_xrVoidFunction>(Locate);
    if (!std::strcmp(name, "xrLocateHandJointsEXT"))
        *out = reinterpret_cast<PFN_xrVoidFunction>(Hand);
    if (!std::strcmp(name, "xrLocateBodyJointsFB"))
        *out = reinterpret_cast<PFN_xrVoidFunction>(Body);
    return *out ? XR_SUCCESS : XR_ERROR_FUNCTION_UNSUPPORTED;
}
SessionInput
AllInputs()
{
    return {Handle<XrInstance>(1),
            Handle<XrSpace>(2),
            Handle<XrSpace>(3),
            Handle<XrSpace>(4),
            Handle<XrSpace>(5),
            Handle<XrHandTrackerEXT>(6),
            Handle<XrHandTrackerEXT>(7),
            Handle<XrBodyTrackerFB>(8)};
}
core::ConnectorConfig
Config()
{
    core::ConnectorConfig c;
    c.sourceProfile = "openxr.observations.v1";
    return c;
}
bool
Acquire(OpenXRConnector& connector, XrTime time, double receive = 4.0)
{
    expectedTime = time;
    return connector.Acquire(time, receive);
}
bool
HasCode(const OpenXRConnector& connector, DiagnosticCode code)
{
    for (const auto& diagnostic : connector.GetDiagnostics())
        if (diagnostic.code == code)
            return true;
    return false;
}
void
Near(const pxr::GfVec3f& a, const pxr::GfVec3f& b)
{
    assert((a - b).GetLength() < 1e-5f);
}
} // namespace

int
main()
{
    const auto config = Config();
    OpenXRConnector connector(AllInputs(), Resolve);
    assert(connector.GetState() == core::ConnectorState::Disconnected);
    assert(!Acquire(connector, 1));
    assert(connector.Open(config));
    assert(connector.GetState() == core::ConnectorState::Connecting);
    const auto caps = connector.GetCapabilities();
    assert(caps.Has(core::ConnectorCapability::Trackers));
    assert(caps.Has(core::ConnectorCapability::Controllers));
    assert(!caps.Has(core::ConnectorCapability::Hands));
    assert(!caps.Has(core::ConnectorCapability::Body));
    assert(caps.Has(core::ConnectorCapability::Confidence));
    assert(!caps.Has(core::ConnectorCapability::RootMotion));
    assert(Acquire(connector, 2000000000));
    assert(calls == 6 && connector.GetState() == core::ConnectorState::Connected);
    core::MotionFrame frame;
    assert(connector.Poll(frame));
    assert(frame.sourceProfile == config.sourceProfile && frame.frameNumber == 1);
    assert(frame.timing.sourceTimestamp == 2.0 && frame.timing.receiveTimestamp == 4.0);
    assert(frame.timing.sourceClock == core::ClockDomain::Device && !frame.timing.sequence);
    assert(frame.actors.size() == 1 && frame.actors[0].actor == "openxr:0");
    assert(!frame.actors[0].pose);
    const auto& observations = frame.actors[0].trackers;
    assert(observations.size() == 3 + 2 * XR_HAND_JOINT_COUNT_EXT + XR_BODY_JOINT_COUNT_FB);
    assert(observations[0].trackerId == "head" && observations[1].trackerId == "controller:left");
    assert(observations[3].trackerId == "hand:left:0");
    assert(observations[29].trackerId == "hand:right:0");
    assert(observations[55].trackerId == "body:0");
    assert(!observations[0].confidence && observations[55].confidence == confidence);
    Near(observations[0].position, pxr::GfVec3f(-1, 2, 3));
    assert(!connector.Poll(frame));

    // Physically rotate forward/up/right in source space and compare their
    // canonical directions; exercise yaw, pitch and roll in both directions.
    XrTime time = 2000000000;
    for (int axis = 0; axis < 3; ++axis)
        for (float sign : {-1.0f, 1.0f}) {
            pxr::GfVec3f v(0.0f);
            v[axis] = sign * std::sqrt(0.5f);
            pose.orientation = {v[0] * 2, v[1] * 2, v[2] * 2, std::sqrt(0.5f) * 2};
            assert(Acquire(connector, ++time));
            assert(connector.Poll(frame));
            const pxr::GfQuatf source(std::sqrt(0.5f), v);
            for (pxr::GfVec3f direction :
                 {pxr::GfVec3f(0, 0, -1), pxr::GfVec3f(0, 1, 0), pxr::GfVec3f(1, 0, 0)}) {
                const auto rotated = source.Transform(direction);
                const pxr::GfVec3f canonical(-direction[0], direction[1], -direction[2]);
                const pxr::GfVec3f expected(-rotated[0], rotated[1], -rotated[2]);
                Near(frame.actors[0].trackers[0].rotation.Transform(canonical), expected);
                Near(frame.actors[0].trackers[3].rotation.Transform(canonical), expected);
                Near(frame.actors[0].trackers[55].rotation.Transform(canonical), expected);
            }
        }

    // Missing/invalid components never reuse a previous pose or touch invalid
    // SDK fields. Valid-but-untracked components remain observations, degraded.
    pose = {{0, 0, 0, 0}, {std::numeric_limits<float>::quiet_NaN(), 2, -3}};
    flags = 0;
    assert(Acquire(connector, ++time) && connector.Poll(frame));
    assert(!frame.actors[0].trackers[0].hasPosition && !frame.actors[0].trackers[0].hasRotation);
    assert(!HasCode(connector, DiagnosticCode::TransformInvalid));
    flags = Flags;
    assert(Acquire(connector, ++time) && connector.Poll(frame));
    assert(HasCode(connector, DiagnosticCode::TransformInvalid));
    assert(!frame.actors[0].trackers[0].hasPosition && !frame.actors[0].trackers[0].hasRotation);
    pose = {{0, 0, 0, 1}, {1, 2, -3}};
    flags = XR_SPACE_LOCATION_POSITION_VALID_BIT;
    assert(Acquire(connector, ++time) && connector.Poll(frame));
    assert(frame.actors[0].trackers[0].hasPosition && !frame.actors[0].trackers[0].hasRotation);
    flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    assert(Acquire(connector, ++time) && connector.Poll(frame));
    assert(!frame.actors[0].trackers[0].hasPosition && frame.actors[0].trackers[0].hasRotation);
    flags = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    assert(Acquire(connector, ++time) && connector.Poll(frame));
    assert(connector.GetState() == core::ConnectorState::Degraded);
    flags = Flags;
    active = false;
    assert(Acquire(connector, ++time) && connector.Poll(frame));
    assert(frame.actors[0].trackers[0].hasPosition && !frame.actors[0].trackers[3].hasPosition);
    assert(!frame.actors[0].trackers[55].confidence);
    active = true;
    assert(Acquire(connector, ++time) && connector.Poll(frame));
    assert(connector.GetState() == core::ConnectorState::Connected &&
           connector.GetDiagnostics().empty());

    // The SDK can return body data from a different instant. Do not relabel
    // that data as synchronized with viewer/controller/hand observations.
    bodyTimeOffset = -1;
    assert(Acquire(connector, ++time) && connector.Poll(frame));
    assert(frame.actors[0].trackers[0].hasPosition && !frame.actors[0].trackers[55].hasPosition);
    assert(!frame.actors[0].trackers[55].confidence);
    assert(HasCode(connector, DiagnosticCode::BodyTimeMismatch));
    assert(connector.GetDiagnostics().front().sourceTime == time - 1);
    bodyTimeOffset = 0;

    // Invalid/regressing request time makes no SDK call and publishes no frame.
    const int before = calls;
    assert(!Acquire(connector, time));
    assert(!Acquire(connector, -1));
    assert(!Acquire(connector, time + 1, std::numeric_limits<double>::infinity()));
    assert(!Acquire(connector, time + 1, 3.0));
    assert(calls == before && !connector.Poll(frame));
    assert(HasCode(connector, DiagnosticCode::TimestampInvalid));

    // Each queue mode uses the shared contract and reports observable loss.
    auto queueConfig = config;
    for (auto mode :
         {core::BufferMode::Latest, core::BufferMode::Ordered, core::BufferMode::Lossless}) {
        queueConfig.bufferMode = mode;
        assert(connector.Open(queueConfig));
        assert(Acquire(connector, 1));
        const auto callsBefore = calls;
        const bool accepted = Acquire(connector, 2);
        assert(accepted == (mode != core::BufferMode::Lossless));
        assert(connector.Poll(frame));
        assert(frame.frameNumber == (mode == core::BufferMode::Lossless ? 1 : 2));
        if (!accepted) {
            assert(HasCode(connector, DiagnosticCode::BufferFull) && calls == callsBefore);
            assert(Acquire(connector, 2) && connector.Poll(frame) && frame.frameNumber == 2);
        }
    }

    for (XrResult failure : {XR_ERROR_RUNTIME_FAILURE, XR_SESSION_LOSS_PENDING}) {
        assert(connector.Open(config));
        result = failure;
        assert(!Acquire(connector, 1) && !connector.Poll(frame));
        assert(connector.GetState() == core::ConnectorState::Error);
        assert(HasCode(connector, DiagnosticCode::LocateFailed));
        assert(connector.GetDiagnostics().back().result == failure);
        result = XR_SUCCESS;
        assert(!Acquire(connector, 2));
    }
    assert(connector.Open(config));
    badCount = true;
    assert(!Acquire(connector, 1) && !connector.Poll(frame));
    badCount = false;
    assert(connector.Open(config));
    confidence = std::numeric_limits<float>::quiet_NaN();
    assert(!Acquire(connector, 1) && !connector.Poll(frame));
    confidence = 0.75f;
    missingFunction = true;
    assert(!connector.Open(config));
    assert(HasCode(connector, DiagnosticCode::FunctionUnavailable));
    missingFunction = false;
    nullFunction = true;
    assert(!connector.Open(config));
    nullFunction = false;
    assert(connector.Open(config) && Acquire(connector, 1));
    connector.Close();
    assert(!connector.Poll(frame) && !Acquire(connector, 2));
    assert(connector.Open(config) && Acquire(connector, 1) && connector.Poll(frame));
    assert(frame.frameNumber == 1);

    auto input = AllInputs();
    input.leftController = input.rightController = XR_NULL_HANDLE;
    input.leftHand = input.rightHand = XR_NULL_HANDLE;
    input.body = XR_NULL_HANDLE;
    OpenXRConnector head(input, Resolve);
    assert(head.Open(config));
    assert(!head.GetCapabilities().Has(core::ConnectorCapability::Hands));
    assert(!head.GetCapabilities().Has(core::ConnectorCapability::Controllers));
    assert(Acquire(head, 1) && head.Poll(frame) && frame.actors[0].trackers.size() == 1);

    // Sparse extension-only sources need no head/controller locate function.
    input.headSpace = XR_NULL_HANDLE;
    input.leftHand = Handle<XrHandTrackerEXT>(6);
    OpenXRConnector handOnly(input, Resolve);
    assert(handOnly.Open(config) && Acquire(handOnly, 1) && handOnly.Poll(frame));
    assert(frame.actors[0].trackers.size() == XR_HAND_JOINT_COUNT_EXT);
    result = XR_ERROR_RUNTIME_FAILURE;
    assert(!Acquire(handOnly, 2) && !handOnly.Poll(frame));
    assert(handOnly.GetDiagnostics().back().subject == "hand:left:");
    result = XR_SUCCESS;
    input.leftHand = XR_NULL_HANDLE;
    input.body = Handle<XrBodyTrackerFB>(8);
    OpenXRConnector bodyOnly(input, Resolve);
    assert(bodyOnly.Open(config) && Acquire(bodyOnly, 1) && bodyOnly.Poll(frame));
    assert(frame.actors[0].trackers.size() == XR_BODY_JOINT_COUNT_FB);
    bodyTimeOffset = 1;
    assert(Acquire(bodyOnly, 2) && bodyOnly.Poll(frame));
    assert(frame.timing.sourceTimestamp == double(3) * 1e-9);
    assert(frame.actors[0].trackers[0].hasPosition);
    assert(bodyOnly.GetDiagnostics().front().sourceTime == 3);
    bodyTimeOffset = -2;
    assert(Acquire(bodyOnly, 3) && bodyOnly.Poll(frame));
    assert(frame.timing.sourceTimestamp == double(1) * 1e-9);
    assert(frame.actors[0].trackers[0].hasPosition);
    bodyTimeOffset = 0;
    badCount = true;
    assert(!Acquire(bodyOnly, 4) && !bodyOnly.Poll(frame));
    assert(bodyOnly.GetDiagnostics().back().subject == "body");
    badCount = false;
    input.body = XR_NULL_HANDLE;
    input.headSpace = XR_NULL_HANDLE;
    OpenXRConnector empty(input, Resolve);
    assert(!empty.Open(config));
    auto invalidConfig = config;
    invalidConfig.sourceProfile = "openxr.hand.v1";
    assert(!head.Open(invalidConfig));
    invalidConfig = config;
    invalidConfig.coordinateConversion = "custom";
    assert(!head.Open(invalidConfig));
    invalidConfig = config;
    invalidConfig.bufferCapacity = 0;
    assert(!head.Open(invalidConfig));
    assert(HasCode(head, DiagnosticCode::ConfigurationInvalid));
    for (DiagnosticCode code : {DiagnosticCode::ConfigurationInvalid,
                                DiagnosticCode::FunctionUnavailable,
                                DiagnosticCode::LocateFailed,
                                DiagnosticCode::TimestampInvalid,
                                DiagnosticCode::TransformInvalid,
                                DiagnosticCode::TrackingUnavailable,
                                DiagnosticCode::BodyTimeMismatch,
                                DiagnosticCode::BufferFull}) {
        assert(std::strncmp(CodeName(code), "OPENXR_", 7) == 0);
        assert(CodeRecoverable(code) == (CodeSeverity(code) == Severity::Warning));
        assert(std::strlen(CodeDetail(code)) > 0);
    }
}
