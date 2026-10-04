// SPDX-License-Identifier: Apache-2.0
//
// The generated message corpus, replayed against its expectations
// (tests/corpus/expectations.txt, written by tools/generate_messages.py).
//
// Three kinds of message, one line each:
//
//     canonical <file>                 decodes, and re-encodes to the same bytes
//     accepted  <file>                 decodes; the spelling is not the writer's
//     refused   <file> <CODE> <subject>
//
// A canonical message is a golden: it pins the writer's key order, number
// spelling and escaping as well as the reader. A refused one pins the code and
// the subject, never the detail. Every `*.frame.json` in the directory must
// have a line, so a message added without an expectation fails here rather
// than passing unread.
#include "motionConnectorWire/FrameWire.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{

namespace wire = openstrata::connectors::wire;
namespace fs = std::filesystem;

std::string
ReadBytes(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream && "a corpus file named by expectations.txt is missing");
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::vector<std::string>
SplitTabs(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab - start));
        if (tab == std::string::npos)
        {
            return fields;
        }
        start = tab + 1;
    }
}

int
Fail(const std::string& file, const std::string& what)
{
    std::fprintf(stderr, "%s: %s\n", file.c_str(), what.c_str());
    return 1;
}

int
Check(const fs::path& directory, const std::vector<std::string>& fields)
{
    const std::string& kind = fields[0];
    const std::string& file = fields[1];
    const std::string message = ReadBytes(directory / file);

    openstrata::connectors::core::MotionFrame frame;
    wire::WireError error;
    const bool decoded = wire::DecodeFrame(message, &frame, &error);

    if (kind == "canonical" || kind == "accepted")
    {
        if (fields.size() != 2)
        {
            return Fail(file, "a " + kind + " line has two fields");
        }
        if (!decoded)
        {
            return Fail(file, "refused: " + std::string(wire::WireErrorCodeName(error.code)) +
                                  " at " + error.subject + ": " + error.detail);
        }
        if (kind == "accepted")
        {
            return 0;
        }
        std::string encoded;
        if (!wire::EncodeFrame(frame, &encoded, &error))
        {
            return Fail(file, "re-encoding refused at " + error.subject + ": " + error.detail);
        }
        if (encoded != message)
        {
            return Fail(file, "re-encodes differently:\n  committed: " + message +
                                  "\n  written:   " + encoded);
        }
        return 0;
    }
    if (kind == "refused")
    {
        if (fields.size() != 4)
        {
            return Fail(file, "a refused line has four fields");
        }
        if (decoded)
        {
            return Fail(file, "decoded, but expected " + fields[2]);
        }
        const std::string_view code = wire::WireErrorCodeName(error.code);
        if (code != fields[2] || error.subject != fields[3])
        {
            return Fail(file, "expected " + fields[2] + " at " + fields[3] + ", got " +
                                  std::string(code) + " at " + error.subject + " (" +
                                  error.detail + ")");
        }
        return 0;
    }
    return Fail(file, "unknown expectation '" + kind + "'");
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <corpus directory>\n", argv[0]);
        return 2;
    }
    const fs::path directory = argv[1];
    std::ifstream expectations(directory / "expectations.txt", std::ios::binary);
    if (!expectations)
    {
        std::fprintf(stderr, "no expectations.txt in %s\n", directory.string().c_str());
        return 2;
    }

    int failures = 0;
    std::set<std::string> named;
    std::size_t counts[3] = {0, 0, 0};
    std::string line;
    while (std::getline(expectations, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        const std::vector<std::string> fields = SplitTabs(line);
        if (fields.size() < 2)
        {
            failures += Fail(line, "an expectation names a kind and a file");
            continue;
        }
        if (!named.insert(fields[1]).second)
        {
            failures += Fail(fields[1], "named twice");
            continue;
        }
        failures += Check(directory, fields);
        counts[fields[0] == "canonical" ? 0 : fields[0] == "accepted" ? 1 : 2] += 1;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(directory))
    {
        const std::string name = entry.path().filename().string();
        const std::string suffix = ".frame.json";
        if (name.size() > suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0 &&
            named.count(name) == 0)
        {
            failures += Fail(name, "has no line in expectations.txt");
        }
    }

    if (failures != 0)
    {
        std::fprintf(stderr, "%d corpus message(s) failed\n", failures);
        return 1;
    }
    std::printf("motionConnectorWire corpus: %zu canonical, %zu accepted, %zu refused\n",
                counts[0], counts[1], counts[2]);
    return 0;
}
