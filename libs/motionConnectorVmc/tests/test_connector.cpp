// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorVmc/Connector.h"

#include "motionConnectorVmc/SkeletonMap.h"
#include "motionConnectorVmc/VmcMessage.h"

#include "motionRecording/LiveCaptureSource.h"

#include <cassert>

namespace
{

openstrata::connectors::vmc::VmcPacket
Packet(float timestamp, bool includeSpine = false)
{
    using namespace openstrata::connectors::vmc;
    VmcPacket packet;

    VmcMessage time;
    time.kind = VmcMessageKind::Time;
    time.seconds = timestamp;
    packet.messages.push_back(time);

    VmcMessage bone;
    bone.kind = VmcMessageKind::BoneTransform;
    bone.name = VmcHumanBoneName(openstrata::motion::HumanJoint::Hips);
    bone.transform.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    packet.messages.push_back(bone);

    if (includeSpine)
    {
        VmcMessage spine = bone;
        spine.name = VmcHumanBoneName(openstrata::motion::HumanJoint::Spine);
        packet.messages.push_back(spine);
    }
    return packet;
}

} // namespace

int
main()
{
    using namespace openstrata::connectors;
    using core::BufferMode;
    using core::ConnectorCapability;
    using core::ConnectorConfig;
    using core::ConnectorState;
    using core::MotionFrame;
    using vmc::VmcConnector;

    VmcConnector invalid;
    ConnectorConfig invalidConfig;
    assert(!invalid.Open(invalidConfig));
    assert(invalid.GetState() == ConnectorState::Error);

    VmcConnector connector;
    ConnectorConfig config;
    config.bindAddress = "127.0.0.1";
    config.port = 0;
    config.sourceProfile = "vmc.v1";
    config.bufferMode = BufferMode::Ordered;
    config.bufferCapacity = 4;

    assert(connector.Open(config));
    assert(connector.GetState() == ConnectorState::Connecting);
    assert(connector.GetCapabilities().Has(ConnectorCapability::Body));
    assert(connector.GetCapabilities().Has(ConnectorCapability::Face));

    assert(connector.PushPacket(Packet(1.0f, true), 0.1) == 0);
    assert(connector.PushPacket(Packet(2.0f), 0.2) == 1);
    assert(connector.PushPacket(Packet(3.0f), 0.3) == 1);

    MotionFrame frame;
    assert(connector.Poll(frame));
    assert(frame.frameNumber == 1);
    assert(frame.sourceProfile == "vmc.v1");
    assert(frame.timing.sourceTimestamp.has_value());
    assert(*frame.timing.sourceTimestamp == 1.0);
    assert(frame.timing.receiveTimestamp == 0.2);
    assert(frame.actors.size() == 1);
    assert(frame.actors[0].actor == "vmc:0");
    assert(frame.actors[0].pose.has_value());
    assert(frame.actors[0].pose->metadata.protocol == "vmc");

    // A connector is polled for MotionFrame values; each actor's canonical
    // pose is pushed into the motion layer's timestamped intake. The two
    // buffers serve different purposes and must compose without another pose
    // conversion or a source-specific stream interface.
    openstrata::motion::LiveCaptureSource intake;
    intake.SetSourceMetadata(frame.actors[0].pose->metadata);
    assert(intake.Push(*frame.actors[0].pose));

    MotionFrame degraded;
    assert(connector.Poll(degraded));
    assert(degraded.actors.size() == 1);
    assert(degraded.actors[0].pose.has_value());
    assert(intake.Push(*degraded.actors[0].pose));
    assert(intake.GetStats().framesAccepted == 2);
    openstrata::motion::IMotionSource& stream = intake;
    const auto sample = stream.Sample(frame.actors[0].pose->timestamp);
    assert(sample.IsValid());
    assert(sample.pose->metadata.protocol == "vmc");
    assert(sample.pose->metadata.sourceTimestamp == frame.timing.sourceTimestamp);
    assert(connector.GetState() == ConnectorState::Degraded);

    connector.Close();
    assert(connector.GetState() == ConnectorState::Disconnected);
    return 0;
}
