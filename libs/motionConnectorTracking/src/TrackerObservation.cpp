// SPDX-License-Identifier: Apache-2.0
#include "motionConnectorTracking/TrackerObservation.h"

namespace openstrata::connectors::tracking
{

std::vector<std::string_view>
TrackerIdentities(const std::vector<TrackerObservation>& observed)
{
    std::vector<std::string_view> identities;
    identities.reserve(observed.size());
    for (const TrackerObservation& observation : observed)
    {
        identities.emplace_back(observation.tracker);
    }
    return identities;
}

} // namespace openstrata::connectors::tracking
