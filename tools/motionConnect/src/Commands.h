// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace motionConnectTool {

enum class Command {
    List,
    Dump,
    Inspect,
    Bridge,
};

enum class Source {
    Vmc,
    Mocopi,
    VrchatOsc,
    WebSocket,
};

struct Options {
    Command command = Command::List;
    Source source = Source::Vmc;
    bool sourceSpecified = false;
    std::string capturePath;
    std::string bindAddress = "127.0.0.1";
    bool listenSpecified = false;
    std::uint16_t port = 0;
    bool portSpecified = false;
    std::size_t maxFrames = 0;
    double durationSeconds = 0.0;
    std::string output = "websocket";
    std::string wsAddress = "127.0.0.1";
    bool wsListenSpecified = false;
    bool wsConnectSpecified = false;
    std::uint16_t wsPort = 0;
    bool wsPortSpecified = false;
    std::string wsPath = "/";
    std::vector<std::string> allowedOrigins;
    bool wsOptionsSpecified = false;
};

bool ParseOptions(int argc, char** argv, Options* options, bool* showHelp, std::string* error);
void PrintUsage(std::ostream& output);
int Run(const Options& options);

} // namespace motionConnectTool
