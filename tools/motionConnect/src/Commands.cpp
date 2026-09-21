// SPDX-License-Identifier: Apache-2.0

#include "Commands.h"

#include "motionConnectorCore/Types.h"
#include "motionConnectorMocopi/Connector.h"
#include "motionConnectorMocopi/PacketCapture.h"
#include "motionConnectorTransport/PacketCapture.h"
#include "motionConnectorVmc/Connector.h"
#include "motionConnectorVmc/PacketCapture.h"
#include "motionConnectorVrchatOsc/Connector.h"
#include "motionConnectorVrchatOsc/PacketCapture.h"

#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace motionConnectTool
{
namespace
{

using openstrata::connectors::core::BufferMode;
using openstrata::connectors::core::ConnectorCapabilities;
using openstrata::connectors::core::ConnectorCapability;
using openstrata::connectors::core::ConnectorConfig;
using openstrata::connectors::core::MotionFrame;
using openstrata::connectors::transport::PacketCapture;
using openstrata::connectors::transport::PacketCaptureError;

struct ConnectorInfo
{
    std::string_view name;
    std::string_view profile;
    std::uint16_t port;
    ConnectorCapabilities capabilities;
};

volatile std::sig_atomic_t interrupted = 0;

extern "C" void
OnInterrupt(int)
{
    interrupted = 1;
}

std::array<ConnectorInfo, 3>
GetConnectors()
{
    openstrata::connectors::vmc::VmcConnector vmc;
    openstrata::connectors::mocopi::MocopiConnector mocopi;
    openstrata::connectors::vrchatOsc::VrchatOscConnector vrchatOsc;
    return {{{"vmc", "vmc.v1", 39539, vmc.GetCapabilities()},
             {"mocopi", "mocopi.body.v1", 12351, mocopi.GetCapabilities()},
             {"vrchat-osc", "vrchat-osc.trackers.v1", 9001,
              vrchatOsc.GetCapabilities()}}};
}

const ConnectorInfo&
GetConnectorInfo(Source source)
{
    static const std::array<ConnectorInfo, 3> connectors = GetConnectors();
    return connectors[static_cast<std::size_t>(source)];
}

bool
ParseSource(std::string_view value, Source* source)
{
    if (value == "vmc")
    {
        *source = Source::Vmc;
        return true;
    }
    if (value == "mocopi")
    {
        *source = Source::Mocopi;
        return true;
    }
    if (value == "vrchat-osc")
    {
        *source = Source::VrchatOsc;
        return true;
    }
    return false;
}

bool
TakeValue(int argc, char** argv, int* index, std::string_view flag, std::string* value,
          std::string* error)
{
    if (*index + 1 >= argc)
    {
        *error = std::string(flag) + " requires a value";
        return false;
    }
    *value = argv[++(*index)];
    return true;
}

bool
ParseCount(std::string_view text, std::size_t* value)
{
    try
    {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(std::string(text), &consumed);
        if (consumed != text.size() || parsed > static_cast<unsigned long long>(
                                                     std::numeric_limits<std::size_t>::max()))
        {
            return false;
        }
        *value = static_cast<std::size_t>(parsed);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool
ParsePort(std::string_view text, std::uint16_t* value)
{
    std::size_t parsed = 0;
    if (!ParseCount(text, &parsed) || parsed > 65535)
    {
        return false;
    }
    *value = static_cast<std::uint16_t>(parsed);
    return true;
}

bool
ParseDuration(std::string_view text, double* value)
{
    try
    {
        std::size_t consumed = 0;
        const double parsed = std::stod(std::string(text), &consumed);
        if (consumed != text.size() || !std::isfinite(parsed) || parsed < 0.0)
        {
            return false;
        }
        *value = parsed;
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void
PrintCapabilities(const ConnectorCapabilities& capabilities)
{
    constexpr std::array<std::pair<ConnectorCapability, std::string_view>, 10> known = {{
        {ConnectorCapability::Body, "body"},
        {ConnectorCapability::Hands, "hands"},
        {ConnectorCapability::Face, "face"},
        {ConnectorCapability::Eyes, "eyes"},
        {ConnectorCapability::RootMotion, "root-motion"},
        {ConnectorCapability::Trackers, "trackers"},
        {ConnectorCapability::Controllers, "controllers"},
        {ConnectorCapability::SourceTimestamps, "source-timestamps"},
        {ConnectorCapability::Confidence, "confidence"},
        {ConnectorCapability::MultipleActors, "multiple-actors"},
    }};

    bool first = true;
    for (const auto& capability : known)
    {
        if (!capabilities.Has(capability.first))
        {
            continue;
        }
        if (!first)
        {
            std::cout << ", ";
        }
        std::cout << capability.second;
        first = false;
    }
    std::cout << '\n';
}

void
PrintFrame(const MotionFrame& frame)
{
    std::size_t poses = 0;
    std::size_t trackers = 0;
    for (const auto& actor : frame.actors)
    {
        poses += actor.pose.has_value() ? 1 : 0;
        trackers += actor.trackers.size();
    }
    std::cout << "frame " << frame.frameNumber << " profile=" << frame.sourceProfile
              << " receive=" << std::fixed << std::setprecision(6)
              << frame.timing.receiveTimestamp << " actors=" << frame.actors.size()
              << " poses=" << poses << " trackers=" << trackers << '\n';
}

template <typename Connector>
void
DrainFrames(Connector& connector, std::size_t* total, std::size_t maxFrames)
{
    MotionFrame frame;
    while ((maxFrames == 0 || *total < maxFrames) && connector.Poll(frame))
    {
        PrintFrame(frame);
        ++(*total);
    }
}

bool
ReadCapture(Source source, const std::string& path, PacketCapture* capture,
            PacketCaptureError* error)
{
    switch (source)
    {
    case Source::Vmc:
        return openstrata::connectors::vmc::ReadPacketCaptureFile(path, capture, error);
    case Source::Mocopi:
        return openstrata::connectors::mocopi::ReadPacketCaptureFile(path, capture, error);
    case Source::VrchatOsc:
        return openstrata::connectors::vrchatOsc::ReadPacketCaptureFile(path, capture, error);
    }
    return false;
}

void
FlushReplay(openstrata::connectors::vmc::VmcConnector& connector, double receiveTimestamp)
{
    connector.Flush(receiveTimestamp);
}

void
FlushReplay(openstrata::connectors::mocopi::MocopiConnector&, double)
{
}

void
FlushReplay(openstrata::connectors::vrchatOsc::VrchatOscConnector& connector, double)
{
    connector.Flush();
}

template <typename Connector, typename Push>
int
InspectCapture(const PacketCapture& capture, Source source, Connector& connector, Push push)
{
    std::cout << "source: " << GetConnectorInfo(source).profile << '\n';
    std::size_t frames = 0;
    double lastReceiveTimestamp = 0.0;
    for (const auto& datagram : capture.datagrams)
    {
        lastReceiveTimestamp = datagram.receiveTime;
        push(datagram);
        DrainFrames(connector, &frames, 0);
    }
    FlushReplay(connector, lastReceiveTimestamp);
    DrainFrames(connector, &frames, 0);
    std::cout << "frames: " << frames << '\n';
    return 0;
}

int
RunInspect(const Options& options)
{
    PacketCapture capture;
    PacketCaptureError error;
    if (!ReadCapture(options.source, options.capturePath, &capture, &error))
    {
        std::cerr << "motion_connect: " << options.capturePath;
        if (error.line != 0)
        {
            std::cerr << ":" << error.line;
        }
        std::cerr << ": " << error.message << '\n';
        return 1;
    }

    switch (options.source)
    {
    case Source::Vmc:
    {
        openstrata::connectors::vmc::VmcConnector connector;
        return InspectCapture(capture, options.source, connector,
                              [&connector](const auto& datagram)
                              {
                                  connector.PushDatagram(datagram.bytes.data(),
                                                         datagram.bytes.size(),
                                                         datagram.receiveTime);
                              });
    }
    case Source::Mocopi:
    {
        openstrata::connectors::mocopi::MocopiConnector connector;
        return InspectCapture(capture, options.source, connector,
                              [&connector](const auto& datagram)
                              {
                                  connector.PushDatagram(datagram.bytes.data(),
                                                         datagram.bytes.size(),
                                                         datagram.receiveTime);
                              });
    }
    case Source::VrchatOsc:
    {
        openstrata::connectors::vrchatOsc::VrchatOscConnector connector;
        return InspectCapture(capture, options.source, connector,
                              [&connector](const auto& datagram)
                              {
                                  connector.PushDatagram(datagram.bytes.data(),
                                                         datagram.bytes.size(),
                                                         datagram.receiveTime,
                                                         datagram.peer);
                              });
    }
    }
    return 1;
}

ConnectorConfig
MakeDumpConfig(const Options& options)
{
    ConnectorConfig config;
    config.bindAddress = options.bindAddress;
    config.port = options.portSpecified ? options.port : GetConnectorInfo(options.source).port;
    config.sourceProfile = GetConnectorInfo(options.source).profile;
    config.bufferMode = BufferMode::Ordered;
    config.bufferCapacity = 64;
    return config;
}

template <typename Connector>
int
RunDump(const Options& options)
{
    Connector connector;
    const auto status = connector.Open(MakeDumpConfig(options));
    if (!status)
    {
        std::cerr << "motion_connect: " << status.message << '\n';
        return 1;
    }

    interrupted = 0;
    std::signal(SIGINT, OnInterrupt);
    std::cout << "source: " << GetConnectorInfo(options.source).profile << '\n';
    const auto started = std::chrono::steady_clock::now();
    std::size_t frames = 0;
    while (!interrupted && (options.maxFrames == 0 || frames < options.maxFrames))
    {
        DrainFrames(connector, &frames, options.maxFrames);
        const double elapsed = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - started)
                                    .count();
        if (options.durationSeconds != 0.0 && elapsed >= options.durationSeconds)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    connector.Close();
    std::cout << "frames: " << frames << '\n';
    return 0;
}

int
RunDumpBySource(const Options& options)
{
    switch (options.source)
    {
    case Source::Vmc:
        return RunDump<openstrata::connectors::vmc::VmcConnector>(options);
    case Source::Mocopi:
        return RunDump<openstrata::connectors::mocopi::MocopiConnector>(options);
    case Source::VrchatOsc:
        return RunDump<openstrata::connectors::vrchatOsc::VrchatOscConnector>(options);
    }
    return 1;
}

} // namespace

bool
ParseOptions(int argc, char** argv, Options* options, bool* showHelp, std::string* error)
{
    *showHelp = false;
    if (argc < 2)
    {
        *error = "a command is required";
        return false;
    }

    const std::string_view command = argv[1];
    if (command == "list")
    {
        options->command = Command::List;
    }
    else if (command == "dump")
    {
        options->command = Command::Dump;
    }
    else if (command == "inspect")
    {
        options->command = Command::Inspect;
    }
    else if (command == "--help" || command == "-h")
    {
        *showHelp = true;
        return true;
    }
    else
    {
        *error = "unknown command '" + std::string(command) + "'";
        return false;
    }

    for (int index = 2; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h")
        {
            *showHelp = true;
            return true;
        }

        std::string value;
        if (argument == "--source")
        {
            if (!TakeValue(argc, argv, &index, argument, &value, error))
            {
                return false;
            }
            if (!ParseSource(value, &options->source))
            {
                *error = "--source expects vmc, mocopi, or vrchat-osc";
                return false;
            }
            options->sourceSpecified = true;
        }
        else if (argument == "--capture")
        {
            if (!TakeValue(argc, argv, &index, argument, &options->capturePath, error))
            {
                return false;
            }
        }
        else if (argument == "--listen")
        {
            if (!TakeValue(argc, argv, &index, argument, &options->bindAddress, error))
            {
                return false;
            }
        }
        else if (argument == "--port")
        {
            if (!TakeValue(argc, argv, &index, argument, &value, error))
            {
                return false;
            }
            if (!ParsePort(value, &options->port))
            {
                *error = "--port expects a number between 0 and 65535";
                return false;
            }
            options->portSpecified = true;
        }
        else if (argument == "--max-frames")
        {
            if (!TakeValue(argc, argv, &index, argument, &value, error))
            {
                return false;
            }
            if (!ParseCount(value, &options->maxFrames))
            {
                *error = "--max-frames expects a whole number";
                return false;
            }
        }
        else if (argument == "--duration")
        {
            if (!TakeValue(argc, argv, &index, argument, &value, error))
            {
                return false;
            }
            if (!ParseDuration(value, &options->durationSeconds))
            {
                *error = "--duration expects a non-negative number";
                return false;
            }
        }
        else
        {
            *error = "unknown option '" + std::string(argument) + "'";
            return false;
        }
    }

    if (options->command == Command::List)
    {
        if (argc != 2)
        {
            *error = "list takes no options";
            return false;
        }
    }
    else if (!options->sourceSpecified)
    {
        *error = "--source is required for dump and inspect";
        return false;
    }
    else if (options->command == Command::Inspect && options->capturePath.empty())
    {
        *error = "--capture is required for inspect";
        return false;
    }
    else if (options->command == Command::Dump && !options->capturePath.empty())
    {
        *error = "--capture is only valid for inspect";
        return false;
    }
    return true;
}

void
PrintUsage(std::ostream& output)
{
    output << "motion_connect - inspect the shared motion connector contract\n\n"
              "Usage:\n"
              "  motion_connect list\n"
              "  motion_connect dump --source <vmc|mocopi|vrchat-osc> [options]\n"
              "  motion_connect inspect --source <vmc|mocopi|vrchat-osc> "
              "--capture <path>\n\n"
              "Dump options:\n"
              "  --listen ADDR          Bind address (default 127.0.0.1).\n"
              "  --port N               Listen port (source default).\n"
              "  --max-frames N         Stop after N frames; 0 means unlimited.\n"
              "  --duration S           Stop after S seconds; 0 means unlimited.\n"
              "  -h, --help             Show this message.\n";
}

int
Run(const Options& options)
{
    if (options.command == Command::List)
    {
        std::cout << "connectors:\n";
        for (const ConnectorInfo& connector : GetConnectors())
        {
            std::cout << "  " << connector.name << '\n'
                      << "    profile: " << connector.profile << '\n'
                      << "    capabilities: ";
            PrintCapabilities(connector.capabilities);
        }
        return 0;
    }
    if (options.command == Command::Inspect)
    {
        return RunInspect(options);
    }
    return RunDumpBySource(options);
}

} // namespace motionConnectTool