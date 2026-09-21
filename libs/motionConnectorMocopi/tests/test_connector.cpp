// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorMocopi/Connector.h"

#include "fixtures.h"

#include <cassert>

int
main()
{
    using namespace openstrata::connectors;
    using core::BufferMode;
    using core::ConnectorCapability;
    using core::ConnectorConfig;
    using core::ConnectorState;
    using core::MotionFrame;
    using mocopi::MocopiConnector;
    using namespace motionConnectorMocopiTests;

    MocopiConnector invalid;
    ConnectorConfig invalidConfig;
    assert(!invalid.Open(invalidConfig));
    assert(invalid.GetState() == ConnectorState::Error);

    MocopiConnector connector;
    ConnectorConfig config;
    config.bindAddress = "127.0.0.1";
    config.port = 0;
    config.sourceProfile = "mocopi.body.v1";
    config.bufferMode = BufferMode::Ordered;
    config.bufferCapacity = 4;

    assert(connector.Open(config));
    assert(connector.GetState() == ConnectorState::Connecting);
    assert(connector.PushDatagram(nullptr, 0, 0.0) == 0);
    assert(!connector.GetDiagnostics().empty());
    assert(connector.GetDiagnostics().back().source.rfind("127.0.0.1:", 0) == 0);
    assert(connector.GetCapabilities().Has(ConnectorCapability::Body));
    assert(connector.GetCapabilities().Has(ConnectorCapability::RootMotion));
    assert(connector.GetCapabilities().Has(ConnectorCapability::SourceTimestamps));
    assert(!connector.GetCapabilities().Has(ConnectorCapability::Hands));

    assert(connector.PushPacket(SkeletonPacket(), 0.0) == 0);
    assert(connector.PushPacket(FrameAt(1, 1.0), 0.1) == 1);

    MotionFrame frame;
    assert(connector.Poll(frame));
    assert(frame.frameNumber == 1);
    assert(frame.sourceProfile == "mocopi.body.v1");
    assert(frame.timing.sourceTimestamp.has_value());
    assert(*frame.timing.sourceTimestamp == 1.0);
    assert(frame.timing.receiveTimestamp == 0.1);
    assert(frame.actors.size() == 1);
    assert(frame.actors[0].actor == "mocopi:0");
    assert(frame.actors[0].pose.has_value());
    assert(frame.actors[0].pose->metadata.protocol == "mocopi");
    assert(frame.actors[0].pose->root.hasPosition);

    assert(connector.PushPacket(FrameAt(3, 3.0), 0.2) == 1);
    MotionFrame packetLoss;
    assert(connector.Poll(packetLoss));
    assert(connector.GetState() == ConnectorState::Degraded);

    auto incomplete = FrameAt(4, 4.0);
    DropJoint(&incomplete, 10);
    assert(connector.PushPacket(incomplete, 0.3) == 1);

    MotionFrame degraded;
    assert(connector.Poll(degraded));
    assert(connector.GetState() == ConnectorState::Degraded);

    connector.Close();
    assert(connector.GetState() == ConnectorState::Disconnected);
    return 0;
}