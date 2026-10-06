// SPDX-License-Identifier: Apache-2.0
//
// The two things every corpus pass in this adapter does before it can make a
// claim: find the captures, and replay one datagram.
//
// Six binaries read `tests/corpus/`, and each had written the scan out for
// itself. That is six copies of four lines, and they had already drifted into
// two behaviours: `std::filesystem::directory_iterator` **throws** on a path
// that is not a directory, nothing in a test file catches it, so the process
// calls `std::terminate` — on Windows an abort with exit `0xC0000409` and no
// message at all, where each of those files has a "no captures in ..." line it
// plainly meant to print. Two copies checked first and four did not, which is
// reachable by a mistyped argument and by a corpus present at configure time and
// absent at test time in a relocated or packaged build tree.
//
// The replay is the other half and the one that could go quietly wrong rather
// than loudly. `test_udp_receiver.cpp` replays every capture twice — once from
// the file and once through a real socket — and asserts the two agree, which is
// how it says the receiver changed nothing about the bytes. Two copies of the
// push sequence are two pipelines; if they drift, that test compares one against
// the other and reports it as a statement about the socket. `test_live_source.cpp`
// held a third copy of the same sequence.
//
// So the sequence is stated once, and the **buffer discipline is not a parameter
// but a property of the call**: `PushDatagram` takes a mutable buffer and
// poisons it the moment the push returns, before anything reads what the push
// produced. A decoder that retained a `string_view` into the caller's bytes
// survives the shape where the next receive happens to overwrite them and
// produces garbage under this one. No caller can opt out of that by writing its
// loop slightly differently, which is what the three copies had each done.
//
// What is deliberately **not** here is anything that decides. Every tolerance,
// every expected count and every assertion lives in the file making the claim —
// the same rule `fixtures.h` states, and for the same reason: two tests sharing
// an expected number agree with each other rather than with the layer. What each
// binary keeps out of a push is its own business too, which is why this returns
// what one datagram produced and stores nothing.
//
// The sibling adapter's tests have this shape and this duplication. The types
// below are mocopi's, so the header is not shareable as it stands; the
// arrangement is, and a VMC copy of it is the same change one namespace over.
#pragma once

#include "motionConnectorMocopi/Diagnostics.h"
#include "motionConnectorMocopi/FrameAssembler.h"

#include "motionCore/MotionPose.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mocopi = openstrata::connectors::mocopi;

namespace motionConnectorMocopiTests {

// Every `.mocopipackets` file in `directory`, sorted, or false with a line on
// `stderr` saying which of the two refusals it was. The sort is what makes a
// corpus pass report the same order on every platform; `is_regular_file` is what
// keeps a directory named like a capture — or a dangling symlink — from being
// read as one, since the corpus README invites an operator to drop a recording
// in here by hand.
inline bool
CollectCaptures(const std::filesystem::path& directory, std::vector<std::filesystem::path>* out)
{
    // Checked rather than assumed, and with the non-throwing overload: this is
    // the crash described in the header, and any unrecognised argument to any of
    // the six binaries reaches this line.
    std::error_code failed;
    if (!std::filesystem::is_directory(directory, failed)) {
        std::fprintf(stderr, "not a corpus directory: %s\n", directory.string().c_str());
        return false;
    }

    out->clear();
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".mocopipackets") {
            out->push_back(entry.path());
        }
    }
    std::sort(out->begin(), out->end());

    if (out->empty()) {
        std::fprintf(stderr, "no captures in %s\n", directory.string().c_str());
        return false;
    }
    return true;
}

} // namespace motionConnectorMocopiTests
