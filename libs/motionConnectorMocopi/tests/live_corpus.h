// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "corpus.h"
#include "LiveSource.h"

namespace motionConnectorMocopiTests {

// What one datagram produced. The frames are copies rather than references
// because the next push replaces them, and a caller comparing two replays holds
// both.
struct PushedDatagram {
    std::size_t admitted = 0;
    std::vector<mocopi::MocopiFrame> frames;
    bool restartLatched = false;
    // The pose a consumer sampled, present exactly when a frame was admitted and
    // the buffer had something to answer with.
    std::optional<openstrata::motion::MotionPose> sampled;
};

// One datagram through the whole bridge: push, poison, latch the restart, sample
// at the delivered frame's stored timestamp.
//
// `bytes` is filled with `0xcd` before anything reads what the push produced, so
// the claim that a datagram need not outlive the call is checked by the poses a
// caller compares rather than by an assertion about pointers. It is a pointer
// rather than a value for that reason alone.
inline PushedDatagram
PushDatagram(mocopi::MocopiLiveSource* source, std::vector<std::uint8_t>* bytes, double receiveTime,
             std::vector<mocopi::Diagnostic>* diagnostics)
{
    PushedDatagram out;
    out.admitted = source->PushDatagram(*bytes, receiveTime, diagnostics);
    std::fill(bytes->begin(), bytes->end(), std::uint8_t{0xcd});

    out.restartLatched = source->ConsumeSessionRestart();
    out.frames = source->GetFramesFromLastPush();
    if (out.admitted == 0 || out.frames.empty()) {
        return out;
    }

    // `GetFramesFromLastPush()` is a vector because the sibling adapter can emit
    // several frames from one push. This protocol cannot — one datagram is one
    // frame, measured — and the sampling below relies on it: it attributes the
    // sampled pose to `frames.back()`, which is only the admitted frame while
    // there is exactly one. Asserted rather than assumed, so an assembler that
    // ever emitted two would fail loudly here instead of silently dropping the
    // earlier frame out of a caller's comparison.
    assert(out.frames.size() == 1);

    // Sampled at the pose's **stored** timestamp rather than at one recomputed
    // from the frame rate. `time` is binary32 on the wire, so a recomputed
    // instant falls *between* two stored ones and the buffer interpolates —
    // which would compare one interpolation against another and measure the
    // arithmetic rather than the layer under test (MOTION_CONTRACT.md).
    const openstrata::motion::PoseSampleResult result =
        source->Sample(out.frames.back().pose.timestamp);
    if (result.pose) {
        out.sampled = *result.pose;
    }
    return out;
}

// The three tallies every replay reads at the end, from the three objects that
// keep them. Read together because they are only meaningful together: the
// bridge's, the assembler's, and the intake's.
struct ReplayStats {
    mocopi::MocopiLiveSourceStats source;
    mocopi::MocopiFrameStats frame;
    openstrata::motion::LiveCaptureStats intake;
};

inline ReplayStats
ReadStats(const mocopi::MocopiLiveSource& source)
{
    ReplayStats out;
    out.source = source.GetStats();
    out.frame = source.GetAssembler().GetStats();
    out.intake = source.GetIntake().GetStats();
    return out;
}

} // namespace motionConnectorMocopiTests
