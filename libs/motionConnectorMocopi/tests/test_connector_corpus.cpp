// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorMocopi/Connector.h"
#include "motionConnectorMocopi/PacketCapture.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace mocopi = openstrata::connectors::mocopi;

namespace
{

struct Expected
{
    const char* file;
    std::size_t frames;
};

constexpr Expected kExpected[] = {
    {"arms-lowered-60hz.mocopipackets", 3},
    {"extended-form.mocopipackets", 0},
    {"frame-loss-60hz.mocopipackets", 5},
    {"incomplete-frame-60hz.mocopipackets", 2},
    {"malformed-container.mocopipackets", 0},
    {"malformed-packets.mocopipackets", 0},
    {"neutral-standing-60hz.mocopipackets", 5},
    {"refused-bones-60hz.mocopipackets", 0},
    {"session-restart-60hz.mocopipackets", 5},
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
        if (entry.is_regular_file() && entry.path().extension() == ".mocopipackets")
        {
            captures.push_back(entry.path());
        }
    }
    std::sort(captures.begin(), captures.end());
    if (captures.empty())
    {
        std::fprintf(stderr, "no mocopi captures in %s\n", directory.string().c_str());
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

        mocopi::PacketCapture capture;
        mocopi::PacketCaptureError parseError;
        if (!mocopi::ReadPacketCaptureFile(path.string(), &capture, &parseError))
        {
            std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), parseError.line,
                         parseError.message.c_str());
            ++failures;
            continue;
        }

        mocopi::MocopiConnector connector;
        std::size_t frames = 0;
        std::uint64_t expectedFrameNumber = 1;
        bool validFrames = true;
        auto drain = [&]()
        {
            openstrata::connectors::core::MotionFrame frame;
            while (validFrames && connector.Poll(frame))
            {
                ++frames;
                if (frame.frameNumber != expectedFrameNumber ||
                    frame.sourceProfile != "mocopi.body.v1" ||
                    frame.actors.size() != 1 || !frame.actors.front().pose.has_value())
                {
                    std::fprintf(stderr, "%s: invalid MotionFrame at index %zu\n",
                                 name.c_str(), frames);
                    validFrames = false;
                    break;
                }
                ++expectedFrameNumber;
            }
        };

        for (const mocopi::RecordedDatagram& datagram : capture.datagrams)
        {
            connector.PushDatagram(datagram.bytes.data(), datagram.bytes.size(),
                                   datagram.receiveTime);
            drain();
        }
        if (frames != expected->frames)
        {
            std::fprintf(stderr, "%s: %zu frame(s), expected %zu\n", name.c_str(),
                         frames, expected->frames);
            ++failures;
        }
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
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: test_connector_corpus <corpus>\n");
        return 2;
    }
    return CheckCorpus(std::filesystem::path(argv[1]));
}