// SPDX-License-Identifier: Apache-2.0
#include "TraceExport.h"
#include "motionConnectorVrchatOsc/TrackerMessage.h"
#include "Export.h"
#include "Options.h"
#include "motionConnectorVrchatOsc/PacketCapture.h"
#include "motionRecording/CaptureTrace.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <system_error>

#include <algorithm>
#include <string_view>
#include <utility>

namespace vrchatOsc = openstrata::connectors::vrchatOsc;
namespace tracking = openstrata::connectors::tracking;

namespace vrchatOscRecordTool
{
namespace
{

std::size_t
RefusalIndex(tracking::TrackerSolveRefusal refusal) noexcept
{
    const auto index = static_cast<std::size_t>(refusal);
    // A value outside the enum cannot come from `SolveTrackerPose`, and the
    // clamp is here so that a future enumerator added without a bump to
    // `TrackerSolveRefusalCount` writes into a bucket rather than past the
    // array. It is cheaper than the alternative and it is not a policy: nothing
    // reads the bucket back as a refusal name.
    return index < tracking::TrackerSolveRefusalCount ? index : 0;
}

std::size_t
RegionIndex(tracking::TrackerRegion region) noexcept
{
    const auto index = static_cast<std::size_t>(region);
    return index < tracking::TrackerRegionCount ? index : tracking::TrackerRegionCount;
}

void
Tally(std::array<std::size_t, tracking::TrackerRegionCount>& counts,
      const std::vector<tracking::TrackerRegion>& regions)
{
    for (const tracking::TrackerRegion region : regions)
    {
        const std::size_t index = RegionIndex(region);
        if (index < counts.size())
        {
            ++counts[index];
        }
    }
}

// One "region count, region count" line, or the word that says there were none.
// Written rather than left blank: a report whose line is missing and one whose
// line is empty read the same to somebody scrolling, and only one of them is a
// measurement.
void
PrintRegionCounts(std::FILE* out, const char* label,
                  const std::array<std::size_t, tracking::TrackerRegionCount>& counts)
{
    std::fprintf(out, "  %s:", label);
    bool any = false;
    for (std::size_t i = 0; i < counts.size(); ++i)
    {
        if (counts[i] == 0)
        {
            continue;
        }
        const std::string_view name =
            tracking::TrackerRegionName(static_cast<tracking::TrackerRegion>(i));
        std::fprintf(out, "%s %.*s %zu", any ? "," : "", static_cast<int>(name.size()), name.data(),
                     counts[i]);
        any = true;
    }
    std::fprintf(out, "%s\n", any ? "" : " none");
}

} // namespace

TraceCollector::TraceCollector(tracking::TrackerAssignmentSpec assignment,
                               tracking::TrackerSolveConfig solve)
    : _assignment(std::move(assignment)), _solve(solve)
{
}

void
TraceCollector::_OpenSession()
{
    _sessions.emplace_back();
}

void
TraceCollector::Observe(const std::vector<vrchatOsc::TrackerFrame>& frames,
                        const openstrata::motion::SourceMetadata& metadata)
{
    // Observing after `Close` re-opens it, which is the sibling collector's
    // rule and matters more here than it does there. `Close` sizes `_hips` to
    // the sessions it found and `GetHipsMotion()` promises to be indexed
    // alongside `GetSessions()`, so a collector that stayed closed would grow
    // the second and not the first, and the caller's next
    // `GetHipsMotion()[index]` would read past the end. The tool observes then
    // closes once and never comes back, so this costs nothing; what it buys is
    // that the only way to read a stale derived field is to not call `Close` at
    // all, which `GetSessions` already documents.
    _closed = false;

    for (const vrchatOsc::TrackerFrame& frame : frames)
    {
        ++_report.framesObserved;

        // A restart opens a session only when there is one to close. The
        // assembler never marks the first frame of a capture, but a collector
        // that assumed so would produce an empty leading session the first time
        // that changed -- and a session whose every frame refused is not one to
        // close either, which is why the test is on the poses rather than on
        // the count.
        if (_sessions.empty() || (frame.beginsNewSession && !_sessions.back().samples.empty()))
        {
            _OpenSession();
        }

        // The conversion this file exists for: two types, four fields, no
        // arithmetic. See the header on the two that do not cross.
        std::vector<tracking::TrackerObservation> observed;
        observed.reserve(frame.samples.size());
        for (const vrchatOsc::TrackerSample& sample : frame.samples)
        {
            tracking::TrackerObservation observation;
            observation.tracker = sample.tracker;
            observation.position = sample.position;
            observation.rotation = sample.rotation;
            observation.hasPosition = sample.hasPosition;
            observation.hasRotation = sample.hasRotation;
            observed.push_back(std::move(observation));
        }

        // `TrackerIdentities` rather than a hand-built list, because a binding
        // holds an index into the array the assignment was made from: building
        // the identities separately is how the two calls drift apart and bind a
        // region to a device nobody wore (TrackerObservation.h).
        const tracking::TrackerAssignment assignment = tracking::AssignTrackers(
            _assignment, tracking::TrackerIdentities(observed));

        // Filled whatever the refusal, which is that layer's rule -- so these
        // are read before the solve rather than under its success. They
        // **accumulate**; see the header on what taking the last frame's said
        // instead.
        Tally(_report.absent, assignment.absent);
        for (const std::size_t index : assignment.unplaced)
        {
            if (index >= observed.size())
            {
                continue;
            }
            const std::string& identity = observed[index].tracker;
            if (std::find(_report.unplaced.begin(), _report.unplaced.end(), identity) ==
                _report.unplaced.end())
            {
                _report.unplaced.push_back(identity);
            }
        }

        const tracking::TrackerSolve solve =
            tracking::SolveTrackerPose(assignment, observed, frame.receiveTime, _solve);

        const std::size_t refusal = RefusalIndex(solve.refusal);
        ++_report.refusals[refusal];
        if (_report.firstDetail[refusal].empty() && !solve.detail.empty())
        {
            _report.firstDetail[refusal] = solve.detail;
        }

        if (!solve.Solved())
        {
            continue;
        }

        ++_report.framesSolved;
        Tally(_report.placed, solve.placed);
        Tally(_report.unsolved, solve.unsolved);
        Tally(_report.withoutRotation, solve.withoutRotation);
        Tally(_report.withheldWithParent, solve.withheldWithParent);
        Tally(_report.positionsUnused, solve.positionsUnused);

        openstrata::motion::MotionClip& session = _sessions.back();
        session.samples.push_back(solve.pose);
        session.source = metadata;
        ++_poses;
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

    // A restart can leave a session opened and never filled, and a capture
    // whose every frame refused leaves one and only one. Either way an empty
    // animation is not a recording.
    for (std::size_t i = _sessions.size(); i-- != 0;)
    {
        if (_sessions[i].samples.empty())
        {
            _sessions.erase(_sessions.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    _hips.assign(_sessions.size(), HipsMotion());
    for (std::size_t i = 0; i < _sessions.size(); ++i)
    {
        openstrata::motion::MotionClip& session = _sessions[i];
        session.startTime = session.samples.front().timestamp;
        session.endTime = session.samples.back().timestamp;

        // CaptureTrace.h's derivation, deliberately duplicated rather than
        // approximated: a trace this writes declares the rate its reader would
        // otherwise have measured, so the file is a fixed point.
        const double span = session.endTime - session.startTime;
        const std::size_t intervals = session.samples.size() - 1;
        session.nominalFrameRate =
            (span > 0.0 && intervals > 0) ? static_cast<double>(intervals) / span : 30.0;

        // The hips path, measured over the poses that carry one. A session
        // exported with `--no-root-motion` carries none at all and reports its
        // whole length under `framesWithoutRoot`, which is the honest reading
        // of a flag that turned the measurement off.
        HipsMotion& hips = _hips[i];
        bool started = false;
        pxr::GfVec3f first(0.0f);
        pxr::GfVec3f previous(0.0f);
        for (const openstrata::motion::MotionPose& pose : session.samples)
        {
            if (!pose.root.hasPosition)
            {
                ++hips.framesWithoutRoot;
                continue;
            }
            if (!started)
            {
                started = true;
                first = pose.root.worldPosition;
            }
            else
            {
                hips.pathMetres +=
                    static_cast<double>((pose.root.worldPosition - previous).GetLength());
            }
            previous = pose.root.worldPosition;
        }
        if (started)
        {
            hips.netMetres = static_cast<double>((previous - first).GetLength());
        }
    }
}

void
PrintSolveReport(std::FILE* out, const SolveReport& report)
{
    std::fprintf(out, "solve: %zu of %zu frame(s)\n", report.framesSolved, report.framesObserved);

    for (std::size_t i = 0; i < report.refusals.size(); ++i)
    {
        const auto refusal = static_cast<tracking::TrackerSolveRefusal>(i);
        if (refusal == tracking::TrackerSolveRefusal::None || report.refusals[i] == 0)
        {
            continue;
        }
        const std::string_view name = tracking::TrackerSolveRefusalName(refusal);
        std::fprintf(out, "  refused %.*s: %zu frame(s)", static_cast<int>(name.size()),
                     name.data(), report.refusals[i]);
        if (!report.firstDetail[i].empty())
        {
            std::fprintf(out, "  first: %s", report.firstDetail[i].c_str());
        }
        std::fprintf(out, "\n");
    }

    // Five lines, always, in the order a reader asks the questions: what
    // reached a joint, what was worn and reached none, what was worn and sent
    // no orientation, what was held back because something above it did, and
    // what sent a position nothing read.
    PrintRegionCounts(out, "placed", report.placed);
    PrintRegionCounts(out, "unsolved", report.unsolved);
    PrintRegionCounts(out, "withoutRotation", report.withoutRotation);
    PrintRegionCounts(out, "withheldWithParent", report.withheldWithParent);
    PrintRegionCounts(out, "positionsUnused", report.positionsUnused);

    // A count, like the five above it: a region stated for a strap nobody wore
    // is absent in every frame, and one whose tracker dropped out halfway is
    // absent in half of them. Those are different sessions and a bare list
    // cannot tell them apart.
    PrintRegionCounts(out, "stated but absent", report.absent);

    std::fprintf(out, "  observed but unplaced:");
    if (report.unplaced.empty())
    {
        std::fprintf(out, " none");
    }
    for (std::size_t i = 0; i < report.unplaced.size(); ++i)
    {
        std::fprintf(out, "%s %s", i == 0 ? "" : ",", report.unplaced[i].c_str());
    }
    std::fprintf(out, "\n");
}

// Decodes a capture, solves each frame against the operator's statement, and
// writes the canonical trace `--export-trace` asked for. Returns false when
// nothing could be written, having said why; the caller prints its report
// either way, for the reason `RunRecord` gives about the capture file.
//
// **This runs a second pass over the same datagrams, and the repetition is the
// point.** The envelope report and the address inventory are derived from bytes
// alone, so nothing a decoder or a solve makes of a packet can move a number in
// either — the rule this tool is built on, kept in the one mode that decodes at
// all (TraceExport.h). Folding the passes together would save two loops and
// cost the only claim `--inspect` has.
bool
ExportTrace(const Options& options,
            const vrchatOsc::PacketCapture& capture)
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
    if (std::filesystem::equivalent(options.inspectPath, options.traceExportPath, aliased))
    {
        std::cerr << "vrchat_osc_record: " << options.traceExportPath
                  << " is the capture being read, named differently; writing "
                     "the trace there would destroy it\n";
        return false;
    }

    vrchatOsc::TrackerFrameAssembler assembler;
    // The capture's own peer, so a replayed session's diagnostics name what the
    // live one's would have named. A capture that recorded none falls back to
    // its path, which is what the corpus tests read.
    assembler.SetSource(capture.peerEndpoint.empty() ? options.inspectPath : capture.peerEndpoint);

    // The provenance the adapter refuses to invent and the operator already
    // stated. `protocol` is this file's to fill because no type in the adapter
    // holds one: `motionConnectorVrchatOsc` produces no pose, so it carries no
    // `SourceMetadata` for a frame assembler to stamp — which is the
    // library's edge set showing through rather than an omission.
    openstrata::motion::SourceMetadata metadata;
    metadata.kind = openstrata::motion::MotionSourceKind::LiveCapture;
    metadata.protocol = "vrchat-osc";
    metadata.provider = capture.sender;
    metadata.sourceId = capture.sourceId;

    vrchatOscRecordTool::TraceCollector trace(options.assignment, options.solve);
    std::vector<vrchatOsc::TrackerFrame> frames;
    std::vector<vrchatOsc::Diagnostic> log;
    // First of each code, and how many there were. An eight-datagram frame that
    // is short one address raises one diagnostic per frame, so a 2000-frame
    // session with a strap off would otherwise write 2000 lines over the report
    // an operator ran this for.
    std::map<vrchatOsc::DiagnosticCode, std::pair<std::string, std::size_t>> seen;
    const auto drain = [&seen, &log]()
    {
        for (const vrchatOsc::Diagnostic& diagnostic : log)
        {
            auto& entry = seen[diagnostic.code];
            if (entry.second == 0)
            {
                entry.first = vrchatOsc::FormatDiagnostic(diagnostic);
            }
            ++entry.second;
        }
        log.clear();
    };

    for (const vrchatOsc::RecordedDatagram& datagram : capture.datagrams)
    {
        const vrchatOsc::TrackerPacket packet =
            vrchatOsc::DecodeTrackerDatagram(datagram.bytes);
        // The decoder's own refusals, which it raises without a source or a
        // timestamp because it knows neither. Stamped here, where both are
        // known, exactly as `InventoryAddresses` stamps them.
        for (vrchatOsc::Diagnostic diagnostic : packet.diagnostics)
        {
            diagnostic.source = assembler.GetSource();
            diagnostic.timestamp = datagram.receiveTime;
            log.push_back(std::move(diagnostic));
        }
        drain();

        frames.clear();
        // The peer-carrying overload. A capture written before the format could
        // say who sent a datagram passes empty throughout and never sees a
        // restart, which is the assembler's stated behaviour rather than a
        // fallback (FrameAssembler.h).
        assembler.Push(packet, datagram.receiveTime,
                       datagram.peer.empty() ? capture.peerEndpoint : datagram.peer, &frames, &log);
        drain();
        trace.Observe(frames, metadata);
    }

    // The frame still open at the end of the stream, which on this wire is the
    // ordinary case rather than an edge one: a frame closes on the next frame's
    // first repeat or on a gap, and a capture that ends mid-burst has neither.
    // The sibling recorder has no line here because one mocopi datagram is one
    // frame; this one does, and the difference is the frame policy.
    frames.clear();
    assembler.Flush(&frames, &log);
    drain();
    trace.Observe(frames, metadata);

    trace.Close();
    const std::vector<openstrata::motion::MotionClip>& sessions = trace.GetSessions();

    if (!options.quiet)
    {
        for (const auto& entry : seen)
        {
            std::cerr << "vrchat_osc_record: " << entry.second.first;
            if (entry.second.second > 1)
            {
                std::cerr << " (and " << (entry.second.second - 1) << " more of "
                          << vrchatOsc::DiagnosticCodeString(entry.first) << ")";
            }
            std::cerr << "\n";
        }
    }

    // The solve report goes to stdout with the rest of the report, and it is
    // printed whether or not a trace was written. A session that solved nothing
    // is the one an operator most needs it for: it is what tells a misspelled
    // tracker identity from a strap that was never worn.
    vrchatOscRecordTool::PrintSolveReport(stdout, trace.GetReport());

    if (sessions.empty())
    {
        std::cerr << "vrchat_osc_record: no frame reached a pose, so there is "
                     "no trace to write; the solve lines above say whether the "
                     "assignment or the traffic is why\n";
        return false;
    }

    std::size_t index = 0;
    if (options.sourceSession != 0)
    {
        if (options.sourceSession > sessions.size())
        {
            std::cerr << "vrchat_osc_record: --source-session " << options.sourceSession
                      << ": this capture holds " << sessions.size() << " session(s)\n";
            return false;
        }
        index = options.sourceSession - 1;
    }
    else if (sessions.size() > 1)
    {
        // Refused rather than resolved. Picking the first would silently
        // discard a recording, and concatenating them would assert a continuity
        // of tracking *space* across a restart that nothing here can check --
        // which is this wire's version of the sibling tools' refusal and not
        // theirs, because the receiver's clock does not go back (TraceExport.h).
        std::cerr << "vrchat_osc_record: the sender restarted, so this capture "
                     "holds "
                  << sessions.size()
                  << " sessions from different peers, each calibrated on its "
                     "own; one trace is one session, so name the one to export "
                     "with --source-session 1.."
                  << sessions.size() << "\n";
        return false;
    }

    const openstrata::motion::MotionClip& session = sessions[index];
    if (!openstrata::motion::WriteCaptureTraceFile(options.traceExportPath, session))
    {
        // The writer refuses before its first byte when a value cannot be
        // spelled in that format, so a refusal here leaves the path untouched
        // rather than half-written.
        std::cerr << "vrchat_osc_record: could not write " << options.traceExportPath << "\n";
        return false;
    }
    if (!options.quiet)
    {
        std::cerr << "vrchat_osc_record: wrote " << session.samples.size() << " solved frame(s)";
        if (sessions.size() > 1)
        {
            std::cerr << " of session " << (index + 1) << " of " << sessions.size();
        }
        std::cerr << " over " << (session.endTime - session.startTime) << " s at "
                  << session.nominalFrameRate << " Hz to " << options.traceExportPath << "\n";

        // The largest thing the trace carries beside the rotations, said at the
        // point it crosses -- the sibling tool's line, for its reason. Here it
        // is also the one number that says whether `--no-root-motion` did what
        // was asked: a session exported under it reports every frame as
        // carrying no root record.
        const vrchatOscRecordTool::HipsMotion& hips = trace.GetHipsMotion()[index];
        std::cerr << "vrchat_osc_record: the trace carries " << hips.pathMetres
                  << " m of hips path (" << hips.netMetres << " m net) as root motion";
        if (hips.framesWithoutRoot != 0)
        {
            std::cerr << "; " << hips.framesWithoutRoot
                      << " frame(s) carried no root record and are not in that "
                         "sum";
        }
        std::cerr << "\n";
    }
    return true;
}

} // namespace vrchatOscRecordTool
