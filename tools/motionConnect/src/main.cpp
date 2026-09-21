// SPDX-License-Identifier: Apache-2.0

#include "Commands.h"

#include <iostream>

int
main(int argc, char** argv)
{
    motionConnectTool::Options options;
    bool showHelp = false;
    std::string error;
    if (!motionConnectTool::ParseOptions(argc, argv, &options, &showHelp, &error))
    {
        std::cerr << "motion_connect: " << error << '\n';
        motionConnectTool::PrintUsage(std::cerr);
        return 2;
    }
    if (showHelp)
    {
        motionConnectTool::PrintUsage(std::cout);
        return 0;
    }
    return motionConnectTool::Run(options);
}