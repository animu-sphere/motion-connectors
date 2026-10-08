// SPDX-License-Identifier: Apache-2.0
//
// Compiled against every installed package's public header (includes.h, which
// CMakeLists.txt generates from packages.json) and linked against every
// exported target. It prints how many it consumed, which the lane compares
// with packages.json, so a package that was silently skipped cannot pass.
#include "includes.h"

#ifdef MOTIONCONNECTORS_CONSUMER_TRACKING
#include "motionConnectorCore/Types.h"
#include "motionConnectorTracking/TrackerAssignment.h"
#endif

#include <cstdio>

int
main()
{
#ifdef MOTIONCONNECTORS_CONSUMER_TRACKING
    // Compose core acquisition identities with assignment, without projecting
    // geometry into the legacy solve input or linking those libraries together.
    namespace core = openstrata::connectors::core;
    namespace tracking = openstrata::connectors::tracking;
    core::ActorFrame actor;
    core::TrackerObservation first;
    first.trackerId = "t1";
    first.hasPosition = true;
    first.confidence = 0.75f;
    core::TrackerObservation second;
    second.trackerId = "t2";
    actor.trackers = {first, second};
    tracking::TrackerAssignmentSpec spec;
    if (!tracking::ParseTrackerAssignmentSpec("t1=head t2=leftHand", &spec)) {
        return 1;
    }
    const std::vector<std::string_view> identities = {actor.trackers[0].trackerId,
                                                      actor.trackers[1].trackerId};
    const tracking::TrackerAssignment assignment = tracking::AssignTrackers(spec, identities);
    if (!tracking::ValidateTrackerAssignmentObservation(assignment, identities) ||
        tracking::ValidateTrackerAssignmentObservation(assignment,
                                                       {identities[1], identities[0]})) {
        return 1;
    }
#endif
    std::printf("consumed %d package(s)\n", MOTIONCONNECTORS_CONSUMER_PACKAGES);
    return 0;
}
