// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionConnectorCore/Types.h"

#include <deque>
#include <mutex>

namespace openstrata::connectors::core
{

class MOTIONCONNECTORCORE_API FrameBuffer final
{
  public:
    explicit FrameBuffer(std::size_t capacity = 1, BufferMode mode = BufferMode::Latest) noexcept;

    FramePushResult Push(MotionFrame frame);
    bool Poll(MotionFrame& out);

    void Clear() noexcept;
    std::size_t Size() const noexcept;
    std::size_t Capacity() const noexcept;
    BufferMode Mode() const noexcept;
    FrameBufferStats GetStats() const noexcept;

  private:
    mutable std::mutex _mutex;
    std::deque<MotionFrame> _frames;
    std::size_t _capacity;
    BufferMode _mode;
    FrameBufferStats _stats;
};

} // namespace openstrata::connectors::core