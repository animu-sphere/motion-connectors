// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorVmc/FrameSource.h"
#include "motionConnectorVmc/SkeletonMap.h"

#include <cassert>

namespace vmc = openstrata::connectors::vmc;
using openstrata::motion::HumanJoint;

namespace {

vmc::VmcPacket
Packet(double timestamp, bool spine = false, bool duplicate = false)
{
    vmc::VmcPacket packet;
    vmc::VmcMessage time;
    time.kind = vmc::VmcMessageKind::Time;
    time.seconds = timestamp;
    packet.messages.push_back(time);
    vmc::VmcMessage bone;
    bone.kind = vmc::VmcMessageKind::BoneTransform;
    bone.name = vmc::VmcHumanBoneName(HumanJoint::Hips);
    bone.transform.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    packet.messages.push_back(bone);
    if (duplicate) {
        packet.messages.push_back(bone);
    }
    if (spine) {
        bone.name = vmc::VmcHumanBoneName(HumanJoint::Spine);
        packet.messages.push_back(bone);
    }
    return packet;
}

void
TestObservationsSurviveWithoutIntakePolicy()
{
    vmc::VmcFrameSource source;
    std::vector<vmc::Diagnostic> diagnostics;
    assert(source.PushPacket(Packet(30.0, true, true), 0.1, &diagnostics) == 0);
    assert(source.PushPacket(Packet(31.0), 0.2, &diagnostics) == 1);
    assert(source.GetFramesFromLastPush().front().duplicateBones == 1);
    assert(source.PushPacket(Packet(32.0), 0.3, &diagnostics) == 1);
    const auto& incomplete = source.GetFramesFromLastPush().front();
    const auto spine = static_cast<std::size_t>(HumanJoint::Spine);
    assert(incomplete.pose.timestamp == 31.0);
    assert(incomplete.timestampFromSender);
    assert(incomplete.missing.test(spine));
    assert(incomplete.stale.test(spine));
    assert(!incomplete.pose.validRotations.test(spine));

    assert(source.PushPacket(Packet(0.0), 0.4, &diagnostics) == 1);
    assert(source.Flush(&diagnostics) == 1);
    const auto& restarted = source.GetFramesFromLastPush().front();
    assert(restarted.pose.timestamp == 0.0);
    assert(restarted.beginsNewSession);
    assert(restarted.missing.none());
    assert(source.GetStats().framesDelivered == 4);
    assert(source.ConsumeSessionRestart());
    assert(!source.ConsumeSessionRestart());

    // A duplicate source timestamp is still a protocol refusal.
    source.PushPacket(Packet(0.0), 0.5, &diagnostics);
    assert(source.Flush(&diagnostics) == 0);
    assert(source.GetFramesFromLastPush().empty());
    assert(source.GetAssembler().GetStats().framesRefusedOutOfOrder == 1);
}

void
TestRefusalsClearDeliveryAndKeepDatagramIdentity()
{
    vmc::VmcFrameSource source;
    source.SetSource("fixture");
    source.PushPacket(Packet(1.0), 0.1);
    assert(source.Flush() == 1);
    std::vector<vmc::Diagnostic> diagnostics;
    const std::vector<std::uint8_t> malformed{0xff};
    assert(source.PushDatagram(malformed, 0.2, &diagnostics) == 0);
    assert(source.GetFramesFromLastPush().empty());
    assert(diagnostics.size() == 1);
    assert(diagnostics.back().source == "fixture");
    assert(diagnostics.back().sequence == 1);
    source.Reset();
    assert(source.GetStats().framesDelivered == 1);
    source.ResetStats();
    assert(source.GetStats().framesDelivered == 0);
    source.PushDatagram(malformed, 0.3, &diagnostics);
    assert(diagnostics.back().sequence == 2);
    assert(source.GetStats().datagramsRefused == 1);

    // Source provenance is acquisition state even before a pose is delivered.
    vmc::VmcPacket handshake;
    vmc::VmcMessage model;
    model.kind = vmc::VmcMessageKind::Model;
    model.title = "Example Avatar";
    handshake.messages.push_back(model);
    source.PushPacket(handshake, 0.4);
    assert(source.GetSourceMetadata().sourceId == "Example Avatar");
    source.Reset();
    assert(source.GetSourceMetadata().sourceId.empty());

    // A source without its own clock keeps the first observation's receive time.
    auto packet = Packet(0.0);
    packet.messages.erase(packet.messages.begin());
    source.PushPacket(packet, 7.0);
    assert(source.Flush() == 1);
    assert(source.GetFramesFromLastPush().front().pose.timestamp == 7.0);
    assert(!source.GetFramesFromLastPush().front().timestampFromSender);
}

} // namespace

int
main()
{
    TestObservationsSurviveWithoutIntakePolicy();
    TestRefusalsClearDeliveryAndKeepDatagramIdentity();
}
