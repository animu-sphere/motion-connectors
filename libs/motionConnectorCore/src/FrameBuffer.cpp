// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorCore/FrameBuffer.h"

#include <algorithm>

namespace openstrata::connectors::core
{

FrameBuffer::FrameBuffer(std::size_t capacity, BufferMode mode) noexcept
    : _capacity(std::max<std::size_t>(capacity, 1)), _mode(mode)
{
}

FramePushResult
FrameBuffer::Push(MotionFrame frame)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_mode == BufferMode::Latest)
    {
        _stats.droppedFrames += static_cast<std::uint64_t>(_frames.size());
        _stats.skippedFrames += static_cast<std::uint64_t>(_frames.size());
        _frames.clear();
        _frames.emplace_back(std::move(frame));
        ++_stats.pushedFrames;
        return FramePushResult::Accepted;
    }

    if (_frames.size() >= _capacity)
    {
        if (_mode == BufferMode::Lossless)
        {
            ++_stats.rejectedFrames;
            return FramePushResult::Backpressure;
        }

        _frames.pop_front();
        ++_stats.droppedFrames;
        ++_stats.skippedFrames;
    }

    _frames.emplace_back(std::move(frame));
    ++_stats.pushedFrames;
    return FramePushResult::Accepted;
}

bool
FrameBuffer::Poll(MotionFrame& out)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_frames.empty())
    {
        return false;
    }

    out = std::move(_frames.front());
    _frames.pop_front();
    ++_stats.polledFrames;
    return true;
}

void
FrameBuffer::Clear() noexcept
{
    std::lock_guard<std::mutex> lock(_mutex);
    _frames.clear();
}

std::size_t
FrameBuffer::Size() const noexcept
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _frames.size();
}

std::size_t
FrameBuffer::Capacity() const noexcept
{
    return _capacity;
}

BufferMode
FrameBuffer::Mode() const noexcept
{
    return _mode;
}

FrameBufferStats
FrameBuffer::GetStats() const noexcept
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _stats;
}

} // namespace openstrata::connectors::core