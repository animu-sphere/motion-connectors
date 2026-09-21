// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionConnectorCore/Types.h"

namespace openstrata::connectors::core
{

class MOTIONCONNECTORCORE_API IMotionConnector
{
  public:
    virtual ~IMotionConnector() = default;

    virtual Status Open(const ConnectorConfig&) = 0;
    virtual void Close() = 0;

    virtual ConnectorState GetState() const = 0;
    virtual ConnectorCapabilities GetCapabilities() const = 0;

    // Non-blocking. True when one frame was written to out.
    virtual bool Poll(MotionFrame& out) = 0;
};

} // namespace openstrata::connectors::core