// SPDX-License-Identifier: Apache-2.0
#include "TraceExport.h"
#include "motionConnectorMocopi/FrameSource.h"
#include "Export.h"
#include "Options.h"
#include "motionConnectorMocopi/PacketCapture.h"
#include "motionRecording/CaptureTrace.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <system_error>

namespace mocopiRecordTool
{

void
TraceCollector::Observe(const std::vector<mocopi::MocopiFrame>& frames,
                        const openstrata::motion::SourceMetadata& metadata)
{
    // Observing after `Close` re-opens it, so the derived fields are recomputed
    // rather than left describing the frames this call did not know about. The
    // tool observes then closes once and never comes back, so this costs
    // nothing; what it buys is that the only way to read a stale `startTime` is
    // to not call `Close` at all, which `GetSessions` already documents.
    _closed = false;

    for (const mocopi::MocopiFrame& frame : frames)
    {
        // A restart opens a session only when there is one to close. The
        // assembler never marks the first frame of a capture, but a collector
        // that assumed so would produce an empty leading session the first time
        // that changed.
        //
        // On this protocol a restart's flag lands on the first frame the new
        // session *emits*, which is seconds after the restart was detected —
        // the new session is dark until a skeleton packet declares its rig
        // (FrameAssembler.h). **Measured at 3.8833 s on a real restart**
        // (2026-08-15): 233 frames refused, and the new session's first emitted
        // frame stamped exactly 233/60 s into its own stream clock. That gap
        // belongs to neither session and reaches no trace: the frames in it
        // were refused, so nothing was delivered to observe.
        if (_sessions.empty() || (frame.beginsNewSession && !_sessions.back().samples.empty()))
        {
            _sessions.emplace_back();
            _hips.emplace_back();
            _hipsFirst.emplace_back();
            _hipsLast.emplace_back();
        }
        openstrata::motion::MotionClip& session = _sessions.back();
        session.samples.push_back(frame.pose);
        session.source = metadata;
        ++_frames;

        // Measured on the way past, because this is the place that has both the
        // frames and the reason to care. The pose being appended above now
        // carries the same translation as its root, so a reader can recover the
        // positions -- but not this summary of them, which is the number the
        // export prints and the one that stayed identical across the root/hips
        // record (see the header).
        HipsMotion& hips = _hips.back();
        if (!frame.hipsPosition)
        {
            ++hips.framesWithoutHips;
            continue;
        }
        const pxr::GfVec3f& position = *frame.hipsPosition;
        if (!_hipsFirst.back())
        {
            _hipsFirst.back() = position;
        }
        else
        {
            // Summed step by step rather than measured end to end: a session
            // that walks out and back travels twice the distance it displaces,
            // and the second number alone would call it stationary.
            hips.pathMetres += (position - *_hipsLast.back()).GetLength();
        }
        _hipsLast.back() = position;
        hips.netMetres = (position - *_hipsFirst.back()).GetLength();
    }
}

void
TraceCollector::Close()
{
    if (_closed)
    {
        return;
    }
    _closed = true;

    // **No session here can be empty, so nothing is pruned.** The sibling
    // collector opens with a prune and a sentence about a push that delivered
    // nothing; this class cannot reach that state, because a session is created
    // only in the iteration that immediately appends a sample to it. A capture
    // whose every datagram was refused therefore produces no session at all
    // rather than an empty one, which is the same outcome by a shorter route --
    // and `GetSessions()` being empty is what `ExportTrace` already refuses on.
    //
    // Keeping the loop anyway would have cost more than the lines. The four
    // vectors below are indexed together by every caller, and an erase that
    // pruned two of them would leave the other two describing a different
    // session -- a defect that shows up as a plausible travel distance against
    // the wrong frames rather than as a crash. Unreachable code that has to
    // stay correct in four places is worse than no code.

    for (openstrata::motion::MotionClip& session : _sessions)
    {
        session.startTime = session.samples.front().timestamp;
        session.endTime = session.samples.back().timestamp;

        // CaptureTrace.h's derivation, deliberately duplicated rather than
        // approximated: a trace this writes declares the rate its reader would
        // otherwise have measured, so the file is a fixed point.
        const double span = session.endTime - session.startTime;
        const std::size_t intervals = session.samples.size() - 1;
        session.nominalFrameRate =
            (span > 0.0 && intervals > 0) ? static_cast<double>(intervals) / span : 30.0;
    }
}

// Decodes a capture and writes the canonical trace `--export-trace` asked for.
// Returns false when nothing could be written, having said why; the caller
// prints its report either way, for the reason `RunRecord` gives about the
// capture file.
//
// **This runs a second pass over the same datagrams, and the repetition is the
// point.** The report is derived from bytes alone, so nothing a decoder makes of
// a packet can move a number in it — the rule this tool is built on, kept in the
// one mode that decodes at all (TraceExport.h). Folding the two passes together
// would save a loop and cost the only claim `--inspect` has.
bool
ExportTrace(const Options& options, const mocopi::PacketCapture& capture)
{
    // The second half of the refusal `ParseOptions` makes on the spelling. That
    // one catches `--inspect x --export-trace x`; this catches the same file
    // named two ways — `./x`, an absolute path, a symlink, a hard link — which
    // no comparison of strings can see. `equivalent` answers only when both
    // paths exist, and a trace path that does not exist yet cannot be the
    // capture, so the error code is discarded rather than reported: "these are
    // not the same file" and "one of them is not there" are the same answer
    // here.
    std::error_code aliased;
    if (std::filesystem::equivalent(options.inspectPath, options.traceExportPath, aliased)) {
        std::cerr << "mocopi_record: " << options.traceExportPath
                  << " is the capture being read, named differently; writing "
                     "the trace there would destroy it\n";
        return false;
    }

    mocopi::MocopiFrameSource source;
    // The capture's own peer, so a replayed session's diagnostics name what the
    // live one's would have named. A capture that recorded none falls back to
    // its path, which is what the corpus tests use.
    source.SetSource(capture.peerEndpoint.empty() ? options.inspectPath : capture.peerEndpoint);

    // The provenance the adapter refuses to invent, and the operator already
    // stated. `MocopiFrameAssembler::GetSourceMetadata` leaves `provider` and
    // `sourceId` empty because the only per-session identifier on this wire is
    // `sndf/ipad`, which is unidentified and possibly device-identifying — and
    // the sibling's header gives the general form of the rule: the application
    // that filled the datagrams "is a thing only its operator knows", so
    // guessing it would be provenance that reads as measured and is not. Here
    // the operator did say, on the command line, and the capture header kept it.
    // Copying it forward is not a guess; leaving the trace anonymous when the
    // file beside it is not would be a loss for nothing.
    openstrata::motion::SourceMetadata metadata = source.GetSourceMetadata();
    metadata.provider = capture.sender;
    metadata.sourceId = capture.sourceId;

    mocopiRecordTool::TraceCollector trace;
    std::vector<mocopi::Diagnostic> log;
    // First of each code, and how many there were. A frame short of one bone
    // raises one diagnostic per frame, so a 2000-frame session with a sensor off
    // would otherwise write 2000 lines over the report an operator ran this for.
    std::map<mocopi::DiagnosticCode, std::pair<std::string, std::size_t>> seen;
    for (const mocopi::RecordedDatagram& datagram : capture.datagrams) {
        source.PushDatagram(datagram.bytes, datagram.receiveTime, &log);
        trace.Observe(source.GetFramesFromLastPush(), metadata);
        for (const mocopi::Diagnostic& diagnostic : log) {
            auto& entry = seen[diagnostic.code];
            if (entry.second == 0) {
                entry.first = mocopi::FormatDiagnostic(diagnostic);
            }
            ++entry.second;
        }
        log.clear();
    }
    // There is no `Flush()` on this path and its absence is a measurement: one
    // datagram is one frame, so a capture that ends holds no frame open
    // (FrameAssembler.h). A reader arriving from `vmc_record` will look for the
    // line that would go here.

    trace.Close();
    const std::vector<openstrata::motion::MotionClip>& sessions = trace.GetSessions();

    if (!options.quiet) {
        for (const auto& entry : seen) {
            std::cerr << "mocopi_record: " << entry.second.first;
            if (entry.second.second > 1) {
                std::cerr << " (and " << (entry.second.second - 1) << " more of "
                          << mocopi::DiagnosticCodeString(entry.first) << ")";
            }
            std::cerr << "\n";
        }
    }

    if (sessions.empty()) {
        std::cerr << "mocopi_record: nothing decoded into a frame, so there is "
                     "no trace to write\n";
        return false;
    }

    std::size_t index = 0;
    if (options.sourceSession != 0) {
        if (options.sourceSession > sessions.size()) {
            std::cerr << "mocopi_record: --source-session " << options.sourceSession
                      << ": this capture holds " << sessions.size() << " session(s)\n";
            return false;
        }
        index = options.sourceSession - 1;
    } else if (sessions.size() > 1) {
        // Refused rather than resolved. Picking the first would silently discard
        // a recording, and concatenating them would manufacture a continuity the
        // device's own clock denies (TraceExport.h).
        std::cerr << "mocopi_record: the source restarted, so this capture "
                     "holds "
                  << sessions.size()
                  << " sessions whose stream clocks overlap; one trace is one "
                     "session, so name the one to export with --source-session "
                     "1.."
                  << sessions.size() << "\n";
        return false;
    }

    const openstrata::motion::MotionClip& session = sessions[index];
    if (!openstrata::motion::WriteCaptureTraceFile(options.traceExportPath, session)) {
        // The writer refuses before its first byte when a value cannot be
        // spelled in that format, so a refusal here leaves the path untouched
        // rather than half-written.
        std::cerr << "mocopi_record: could not write " << options.traceExportPath << "\n";
        return false;
    }
    if (!options.quiet) {
        std::cerr << "mocopi_record: wrote " << session.samples.size() << " delivered frame(s)";
        if (sessions.size() > 1) {
            std::cerr << " of session " << (index + 1) << " of " << sessions.size();
        }
        std::cerr << " over " << (session.endTime - session.startTime) << " s at "
                  << session.nominalFrameRate << " Hz to " << options.traceExportPath << "\n";

        // The largest thing the trace carries beside the rotations, said at the
        // point it crosses. It was printed here as a *loss* until the root/hips
        // record was written (MOTION_CONTRACT.md, "Root and hips") and the
        // number did not change when the policy did — which is the useful
        // property: the same measurement that said what was being dropped now
        // says what is being kept, and an operator comparing an export from
        // either side of the record is comparing one quantity.
        //
        // Printed for every export, including the ones that stayed put: "0.02 m
        // of hips path" is the useful answer for a session that did not travel,
        // and a line that appeared only above some threshold would leave a
        // reader unable to tell a still session from an unmeasured one.
        const mocopiRecordTool::HipsMotion& hips = trace.GetHipsMotion()[index];
        std::cerr << "mocopi_record: the trace carries " << hips.pathMetres << " m of hips path ("
                  << hips.netMetres << " m net) as root motion";
        if (hips.framesWithoutHips != 0) {
            std::cerr << "; " << hips.framesWithoutHips
                      << " frame(s) carried no hips record and are not in that "
                         "sum";
        }
        std::cerr << "\n";
    }
    return true;
}

} // namespace mocopiRecordTool
