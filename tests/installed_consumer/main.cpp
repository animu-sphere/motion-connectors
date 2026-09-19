// SPDX-License-Identifier: Apache-2.0
//
// Compiled against every installed package's public header (includes.h, which
// CMakeLists.txt generates from packages.json) and linked against every
// exported target. It prints how many it consumed, which the lane compares
// with packages.json, so a package that was silently skipped cannot pass.
#include "includes.h"

#include <cstdio>

int
main()
{
    std::printf("consumed %d package(s)\n", MOTIONCONNECTORS_CONSUMER_PACKAGES);
    return 0;
}
