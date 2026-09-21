// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorVrchatOsc/Connector.h"

#include <cassert>

namespace
{

openstrata::connectors::vrchatOsc::TrackerPacket
CompletePacket(float x)
{
    using namespace openstrata::connectors::vrchatOsc;
    TrackerPacket packet;

    TrackerMessage position;
    position.tracker.segment = "head";
    position.channel = TrackerChannel::Position;
    position.values = {{x, 1.5f, 0.25f}};
    packet.messages.push_back(position);

    TrackerMessage rotation;
    rotation.tracker.segment = "head";
    rotation.channel = TrackerChannel::Rotation;
    rotation.values = {{0.0f, 0.0f, 0.0f}};
    packet.messages.push_back(rotation);

    packet.messagesSeen = packet.messages.size();
    return packet;
}

openstrata::connectors::vrchatOsc::TrackerPacket
PositionOnlyPacket(float x)
{
    using namespace openstrata::connectors::vrchatOsc;
    TrackerPacket packet;
    TrackerMessage position;
    position.tracker.segment = "head";
    position.channel = TrackerChannel::Position;
    position.values = {{x, 1.5f, 0.25f}};
    packet.messages.push_back(position);
    packet.messagesSeen = 1;
    return packet;
}

void
TestReplayContextSurvivesTheConnectorBoundary(
    const openstrata::connectors::core::ConnectorConfig& config)
{
    using namespace openstrata::connectors;
    using core::ConnectorState;
    using vrchatOsc::DiagnosticCode;
    using vrchatOsc::VrchatOscConnector;

    VrchatOscConnector replay;
    assert(replay.Open(config));
    assert(replay.PushPacket(CompletePacket(0.0f), 0.0, "peer-a") == 0);
    assert(replay.PushPacket(CompletePacket(0.0f), 0.02, "peer-b") == 1);
    assert(replay.GetState() == ConnectorState::Connected);
    assert(!replay.GetDiagnostics().empty());
    const auto& restart = replay.GetDiagnostics().back();
    assert(restart.code == DiagnosticCode::SourceRestarted);
    assert(restart.timestamp.has_value());
    assert(*restart.timestamp == 0.02);
    assert(restart.sequence.has_value());
    assert(*restart.sequence == 2);

    VrchatOscConnector malformed;
    assert(malformed.Open(config));
    const std::uint8_t bytes[] = {0x00};
    assert(malformed.PushDatagram(bytes, sizeof(bytes), 0.25) == 0);
    assert(malformed.GetDiagnostics().size() == 1);
    const auto& refusal = malformed.GetDiagnostics().front();
    assert(refusal.code == DiagnosticCode::PacketMalformed);
    assert(refusal.source.rfind("127.0.0.1:", 0) == 0);
    assert(refusal.timestamp.has_value());
    assert(*refusal.timestamp == 0.25);
    assert(refusal.sequence.has_value());
    assert(*refusal.sequence == 1);
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
    using vrchatOsc::DiagnosticCode;
    using vrchatOsc::VrchatOscConnector;

    VrchatOscConnector invalid;
    ConnectorConfig invalidConfig;
    assert(!invalid.Open(invalidConfig));
    assert(invalid.GetState() == ConnectorState::Error);

    VrchatOscConnector connector;
    ConnectorConfig config;
    config.bindAddress = "127.0.0.1";
    config.port = 0;
    config.sourceProfile = "vrchat-osc.trackers.v1";
    config.bufferMode = BufferMode::Ordered;
    config.bufferCapacity = 4;

    assert(connector.Open(config));
    assert(connector.GetState() == ConnectorState::Connecting);
    assert(connector.GetCapabilities().Has(ConnectorCapability::Trackers));
    assert(!connector.GetCapabilities().Has(ConnectorCapability::Body));
    assert(!connector.GetCapabilities().Has(ConnectorCapability::SourceTimestamps));

    assert(connector.PushPacket(CompletePacket(0.25f), 0.0) == 0);
    assert(connector.PushPacket(CompletePacket(0.5f), 0.02) == 1);

    MotionFrame frame;
    assert(connector.Poll(frame));
    assert(frame.frameNumber == 1);
    assert(frame.sourceProfile == "vrchat-osc.trackers.v1");
    assert(frame.timing.receiveTimestamp == 0.0);
    assert(!frame.timing.sourceTimestamp.has_value());
    assert(frame.timing.sourceClock == core::ClockDomain::None);
    assert(frame.actors.size() == 1);
    assert(frame.actors[0].actor == "vrchat-osc:0");
    assert(frame.actors[0].pose == std::nullopt);
    assert(frame.actors[0].trackers.size() == 1);
    assert(frame.actors[0].trackers[0].trackerId == "head");
    assert(frame.actors[0].trackers[0].hasPosition);
    assert(frame.actors[0].trackers[0].hasRotation);
    assert(frame.actors[0].trackers[0].position[0] == -0.25f);
    assert(frame.actors[0].trackers[0].position[1] == 1.5f);

    assert(connector.PushPacket(PositionOnlyPacket(0.75f), 0.04) == 1);
    assert(connector.PushPacket(CompletePacket(1.0f), 0.06) == 1);
    assert(connector.GetState() == ConnectorState::Degraded);
    assert(connector.PushPacket(CompletePacket(1.25f), 0.08) == 1);

    MotionFrame complete;
    assert(connector.Poll(complete));
    assert(complete.frameNumber == 2);
    assert(connector.GetState() == ConnectorState::Connected);

    MotionFrame partial;
    assert(connector.Poll(partial));
    assert(partial.frameNumber == 3);
    assert(partial.actors[0].trackers.size() == 1);
    assert(partial.actors[0].trackers[0].hasPosition);
    assert(!partial.actors[0].trackers[0].hasRotation);
    assert(connector.GetState() == ConnectorState::Connected);
    assert(!connector.GetDiagnostics().empty());
    assert(connector.GetDiagnostics().back().code == DiagnosticCode::TrackerPartial);
    assert(connector.GetDiagnostics().back().timestamp.has_value());
    assert(*connector.GetDiagnostics().back().timestamp == 0.04);

    TestReplayContextSurvivesTheConnectorBoundary(config);

    connector.Close();
    assert(connector.GetState() == ConnectorState::Disconnected);
    return 0;
}
