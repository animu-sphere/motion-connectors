// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorVmc/Connector.h"
#include "motionConnectorVmc/PacketCapture.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace vmc = openstrata::connectors::vmc;

namespace
{

struct Expected
{
    const char* file;
    std::size_t frames;
};

constexpr Expected kExpected[] = {
    {"arm-raise-30hz.vmcpackets", 5},
    {"extended-forms.vmcpackets", 1},
    {"malformed-forms.vmcpackets", 2},
    {"malformed-packets.vmcpackets", 0},
    {"mixed-traffic-30hz.vmcpackets", 3},
    {"neutral-standing-30hz.vmcpackets", 5},
    {"sender-restart-30hz.vmcpackets", 6},
};

const Expected*
FindExpected(const std::string& name)
{
    for (const Expected& expected : kExpected)
    {
        if (name == expected.file)
        {
            return &expected;
        }
    }
    return nullptr;
}

int
CheckCorpus(const std::filesystem::path& directory)
{
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
    {
        std::fprintf(stderr, "not a corpus directory: %s\n", directory.string().c_str());
        return 1;
    }

    std::vector<std::filesystem::path> captures;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".vmcpackets")
        {
            captures.push_back(entry.path());
        }
    }
    std::sort(captures.begin(), captures.end());
    if (captures.empty())
    {
        std::fprintf(stderr, "no VMC captures in %s\n", directory.string().c_str());
        return 1;
    }

    int failures = 0;
    for (const auto& path : captures)
    {
        const std::string name = path.filename().string();
        const Expected* expected = FindExpected(name);
        if (!expected)
        {
            std::fprintf(stderr, "%s: no expected frame count\n", name.c_str());
            ++failures;
            continue;
        }

        vmc::PacketCapture capture;
        vmc::PacketCaptureError parseError;
        if (!vmc::ReadPacketCaptureFile(path.string(), &capture, &parseError))
        {
            std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), parseError.line,
                         parseError.message.c_str());
            ++failures;
            continue;
        }

        openstrata::connectors::vmc::VmcConnector connector;
        openstrata::connectors::core::ConnectorConfig config;
        config.bindAddress = "127.0.0.1";
        config.port = 0;
        config.sourceProfile = "vmc.v1";
        config.bufferMode = openstrata::connectors::core::BufferMode::Ordered;
        config.bufferCapacity = 128;
        assert(connector.Open(config));

        double lastReceiveTimestamp = 0.0;
        for (const vmc::RecordedDatagram& datagram : capture.datagrams)
        {
            lastReceiveTimestamp = datagram.receiveTime;
            connector.PushDatagram(datagram.bytes.data(), datagram.bytes.size(),
                                   datagram.receiveTime);
        }
        connector.Flush(lastReceiveTimestamp);

        std::size_t frames = 0;
        std::uint64_t expectedFrameNumber = 1;
        openstrata::connectors::core::MotionFrame frame;
        while (connector.Poll(frame))
        {
            ++frames;
            if (frame.frameNumber != expectedFrameNumber ||
                frame.sourceProfile != "vmc.v1" || frame.actors.size() != 1 ||
                !frame.actors.front().pose.has_value())
            {
                std::fprintf(stderr, "%s: invalid MotionFrame at index %zu\n",
                             name.c_str(), frames);
                ++failures;
                break;
            }
            ++expectedFrameNumber;
        }
        if (frames != expected->frames)
        {
            std::fprintf(stderr, "%s: %zu frame(s), expected %zu\n", name.c_str(),
                         frames, expected->frames);
            ++failures;
        }
        connector.Close();
    }

    for (const Expected& expected : kExpected)
    {
        const auto found = std::find_if(
            captures.begin(), captures.end(), [&expected](const auto& path)
            { return path.filename() == expected.file; });
        if (found == captures.end())
        {
            std::fprintf(stderr, "%s: expected capture is missing\n", expected.file);
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

} // namespace

int
main(int argc, char** argv)
{
    assert(argc == 2);
    return CheckCorpus(std::filesystem::path(argv[1]));
}