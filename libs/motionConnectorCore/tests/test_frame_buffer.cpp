// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorCore/FrameBuffer.h"

#include <cassert>

namespace
{

openstrata::connectors::core::MotionFrame
Frame(std::uint64_t number)
{
    openstrata::connectors::core::MotionFrame frame;
    frame.frameNumber = number;
    return frame;
}

void
TestLatest()
{
    using namespace openstrata::connectors::core;

    FrameBuffer buffer(4, BufferMode::Latest);
    assert(buffer.Push(Frame(1)) == FramePushResult::Accepted);
    assert(buffer.Push(Frame(2)) == FramePushResult::Accepted);

    MotionFrame out;
    assert(buffer.Poll(out));
    assert(out.frameNumber == 2);
    assert(!buffer.Poll(out));

    const FrameBufferStats stats = buffer.GetStats();
    assert(stats.pushedFrames == 2);
    assert(stats.polledFrames == 1);
    assert(stats.droppedFrames == 1);
    assert(stats.skippedFrames == 1);
}

void
TestOrderedDropsOldest()
{
    using namespace openstrata::connectors::core;

    FrameBuffer buffer(2, BufferMode::Ordered);
    buffer.Push(Frame(1));
    buffer.Push(Frame(2));
    assert(buffer.Push(Frame(3)) == FramePushResult::Accepted);

    MotionFrame out;
    assert(buffer.Poll(out) && out.frameNumber == 2);
    assert(buffer.Poll(out) && out.frameNumber == 3);
    assert(!buffer.Poll(out));

    const FrameBufferStats stats = buffer.GetStats();
    assert(stats.pushedFrames == 3);
    assert(stats.droppedFrames == 1);
    assert(stats.skippedFrames == 1);
}

void
TestLosslessReportsBackpressure()
{
    using namespace openstrata::connectors::core;

    FrameBuffer buffer(2, BufferMode::Lossless);
    buffer.Push(Frame(1));
    buffer.Push(Frame(2));
    assert(buffer.Push(Frame(3)) == FramePushResult::Backpressure);

    MotionFrame out;
    assert(buffer.Poll(out) && out.frameNumber == 1);
    assert(buffer.Poll(out) && out.frameNumber == 2);
    assert(!buffer.Poll(out));

    const FrameBufferStats stats = buffer.GetStats();
    assert(stats.pushedFrames == 2);
    assert(stats.droppedFrames == 0);
    assert(stats.rejectedFrames == 1);
}

void
TestContractValues()
{
    using namespace openstrata::connectors::core;

    ConnectorCapabilities capabilities(ConnectorCapability::Body);
    capabilities.Add(ConnectorCapability::SourceTimestamps);
    assert(capabilities.Has(ConnectorCapability::Body));
    assert(capabilities.Has(ConnectorCapability::SourceTimestamps));
    assert(!capabilities.Has(ConnectorCapability::Trackers));

    const ConnectorConfig config;
    assert(config.bindAddress == "127.0.0.1");
    assert(config.bufferMode == BufferMode::Latest);
    assert(config.bufferCapacity == 1);

    const Status failure = Status::Failure("bind failed");
    assert(!failure);
    assert(failure.message == "bind failed");
}

} // namespace

int
main()
{
    TestLatest();
    TestOrderedDropsOldest();
    TestLosslessReportsBackpressure();
    TestContractValues();
    return 0;
}