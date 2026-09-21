// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

namespace motionConnectTool
{

enum class Command
{
    List,
    Dump,
    Inspect,
};

enum class Source
{
    Vmc,
    Mocopi,
    VrchatOsc,
};

struct Options
{
    Command command = Command::List;
    Source source = Source::Vmc;
    bool sourceSpecified = false;
    std::string capturePath;
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 0;
    bool portSpecified = false;
    std::size_t maxFrames = 0;
    double durationSeconds = 0.0;
};

bool ParseOptions(int argc, char** argv, Options* options, bool* showHelp,
                  std::string* error);
void PrintUsage(std::ostream& output);
int Run(const Options& options);

} // namespace motionConnectTool