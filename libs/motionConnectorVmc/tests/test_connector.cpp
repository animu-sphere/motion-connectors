// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorVmc/Connector.h"

#include "motionConnectorVmc/SkeletonMap.h"
#include "motionConnectorVmc/VmcMessage.h"

#include <cassert>

namespace
{

openstrata::connectors::vmc::VmcPacket
Packet(float timestamp)
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

    assert(connector.PushPacket(Packet(1.0f), 0.1) == 0);
    assert(connector.PushPacket(Packet(2.0f), 0.2) == 1);

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
    assert(connector.GetState() == ConnectorState::Connected);

    connector.Close();
    assert(connector.GetState() == ConnectorState::Disconnected);
    return 0;
}