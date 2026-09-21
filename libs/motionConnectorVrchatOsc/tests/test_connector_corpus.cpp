// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorVrchatOsc/Connector.h"
#include "motionConnectorVrchatOsc/PacketCapture.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace vrchatOsc = openstrata::connectors::vrchatOsc;

namespace
{

struct Expected
{
    const char* file;
    std::size_t frames;
};

constexpr Expected kExpected[] = {
    {"bundled-frame.vrchatoscpackets", 3},
    {"calibration-jump.vrchatoscpackets", 6},
    {"duplicate-and-reordered.vrchatoscpackets", 4},
    {"eight-trackers.vrchatoscpackets", 2},
    {"head-absent.vrchatoscpackets", 3},
    {"malformed-forms.vrchatoscpackets", 2},
    {"malformed-packets.vrchatoscpackets", 1},
    {"mixed-traffic.vrchatoscpackets", 2},
    {"one-tracker.vrchatoscpackets", 3},
    {"position-only.vrchatoscpackets", 3},
    {"rig-motion.vrchatoscpackets", 12},
    {"rotation-only.vrchatoscpackets", 3},
    {"session-restart.vrchatoscpackets", 6},
    {"silent-gap.vrchatoscpackets", 6},
    {"three-trackers-58hz.vrchatoscpackets", 7},
    {"tracker-dropout.vrchatoscpackets", 6},
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
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".vrchatoscpackets")
        {
            captures.push_back(entry.path());
        }
    }
    std::sort(captures.begin(), captures.end());
    if (captures.empty())
    {
        std::fprintf(stderr, "no VRChat OSC captures in %s\n", directory.string().c_str());
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

        vrchatOsc::PacketCapture capture;
        vrchatOsc::PacketCaptureError parseError;
        if (!vrchatOsc::ReadPacketCaptureFile(path.string(), &capture, &parseError))
        {
            std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), parseError.line,
                         parseError.message.c_str());
            ++failures;
            continue;
        }

        vrchatOsc::VrchatOscConnector connector;
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
                    frame.sourceProfile != "vrchat-osc.trackers.v1" ||
                    frame.actors.size() != 1 || frame.actors.front().pose.has_value() ||
                    frame.actors.front().trackers.empty())
                {
                    std::fprintf(stderr, "%s: invalid MotionFrame at index %zu\n",
                                 name.c_str(), frames);
                    validFrames = false;
                    break;
                }
                ++expectedFrameNumber;
            }
        };

        for (const vrchatOsc::RecordedDatagram& datagram : capture.datagrams)
        {
            connector.PushDatagram(datagram.bytes.data(), datagram.bytes.size(),
                                   datagram.receiveTime, datagram.peer);
            drain();
        }
        connector.Flush();
        drain();
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