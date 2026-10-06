// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorMocopi/Connector.h"
#include "motionConnectorMocopi/FrameSource.h"
#include "motionConnectorMocopi/PacketCapture.h"

#include "fixtures.h"

#include <cassert>
#include <filesystem>

namespace mocopi = openstrata::connectors::mocopi;
using namespace motionConnectorMocopiTests;

namespace {

void
TestSourceFactsWithoutIntakePolicy()
{
    mocopi::MocopiFrameSource source;
    source.SetSource("fixture");
    std::vector<mocopi::Diagnostic> diagnostics;
    assert(source.PushPacket(SkeletonPacket(), 0.0) == 0);
    assert(source.GetSkeletonMap());
    assert(source.PushPacket(FrameAt(3000, 20.0), 0.1) == 1);
    auto incomplete = FrameAt(3003, 20.05);
    DropJoint(&incomplete, 20);
    assert(source.PushPacket(incomplete, 0.2, &diagnostics) == 1);
    const auto& frame = source.GetFramesFromLastPush().front();
    const auto leg = static_cast<std::size_t>(openstrata::motion::HumanJoint::LeftLowerLeg);
    assert(frame.frameNumber == 3003);
    assert(frame.lostFrames == 2);
    assert(frame.missing.test(leg));
    assert(!frame.pose.validRotations.test(leg));
    assert(!frame.pose.root.hasLinearVelocity);
    assert(frame.pose.timestamp == static_cast<double>(incomplete.frame->streamSeconds));
    // A duplicate is a source refusal; the previous delivery is not repeated.
    assert(source.PushPacket(incomplete, 0.3, &diagnostics) == 0);
    assert(source.GetFramesFromLastPush().empty());

    // The backwards stream clock and advancing wall clock describe a restart.
    assert(source.PushPacket(FramePacket(1, 0.0, kEpoch + 21.0), 0.4, &diagnostics) == 0);
    assert(!source.GetSkeletonMap());
    assert(!source.ConsumeSessionRestart());
    assert(source.PushPacket(SkeletonPacket(), 0.5) == 0);
    assert(source.PushPacket(FramePacket(2, 1.0 / kFrameRate, kEpoch + 21.0 + 1.0 / kFrameRate),
                             0.6) == 1);
    assert(source.GetFramesFromLastPush().front().beginsNewSession);
    assert(source.GetFramesFromLastPush().front().pose.timestamp < 1.0);
    assert(source.GetStats().framesDelivered == 3);
    assert(source.ConsumeSessionRestart());
    assert(!source.ConsumeSessionRestart());

    const std::vector<std::uint8_t> malformed{0xff};
    assert(source.PushDatagram(malformed, 0.7, &diagnostics) == 0);
    assert(source.GetFramesFromLastPush().empty());
    assert(diagnostics.back().source == "fixture");
    assert(diagnostics.back().sequence == 1);
    source.Reset();
    assert(!source.GetSkeletonMap());
    assert(!source.ConsumeSessionRestart());
    assert(source.GetStats().framesDelivered == 3);
    source.ResetStats();
    assert(source.GetStats().framesDelivered == 0);
    source.PushDatagram(malformed, 0.8, &diagnostics);
    assert(diagnostics.back().sequence == 2);
    assert(source.GetStats().datagramsRefused == 1);
}

void
TestSharedConnectorParity(const std::filesystem::path& directory)
{
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".mocopipackets") {
            continue;
        }
        mocopi::PacketCapture capture;
        mocopi::PacketCaptureError error;
        assert(mocopi::ReadPacketCaptureFile(entry.path().string(), &capture, &error));
        mocopi::MocopiFrameSource source;
        mocopi::MocopiConnector connector;
        std::vector<mocopi::Diagnostic> diagnostics;
        for (const auto& datagram : capture.datagrams) {
            const auto emitted =
                source.PushDatagram(datagram.bytes, datagram.receiveTime, &diagnostics);
            assert(connector.PushDatagram(datagram.bytes.data(),
                                          datagram.bytes.size(),
                                          datagram.receiveTime) == emitted);
            for (const auto& observation : source.GetFramesFromLastPush()) {
                openstrata::connectors::core::MotionFrame frame;
                assert(connector.Poll(frame));
                assert(frame.timing.receiveTimestamp == datagram.receiveTime);
                assert(frame.timing.sourceTimestamp == observation.pose.timestamp);
                assert(frame.actors.front().pose);
                auto expected = observation.pose;
                expected.metadata = source.GetSourceMetadata();
                expected.metadata.sourceTimestamp = observation.pose.timestamp;
                assert(*frame.actors.front().pose == expected);
            }
            openstrata::connectors::core::MotionFrame extra;
            assert(!connector.Poll(extra));
        }
        assert(diagnostics.size() == connector.GetDiagnostics().size());
        for (std::size_t i = 0; i < diagnostics.size(); ++i) {
            assert(diagnostics[i].code == connector.GetDiagnostics()[i].code);
            assert(diagnostics[i].sequence == connector.GetDiagnostics()[i].sequence);
            assert(diagnostics[i].source == connector.GetDiagnostics()[i].source);
            assert(diagnostics[i].subject == connector.GetDiagnostics()[i].subject);
            assert(diagnostics[i].timestamp == connector.GetDiagnostics()[i].timestamp);
            assert(diagnostics[i].severity == connector.GetDiagnostics()[i].severity);
            assert(diagnostics[i].recoverable == connector.GetDiagnostics()[i].recoverable);
        }
    }
}

} // namespace

int
main(int argc, char** argv)
{
    TestSourceFactsWithoutIntakePolicy();
    if (argc == 2) {
        TestSharedConnectorParity(argv[1]);
    }
}
