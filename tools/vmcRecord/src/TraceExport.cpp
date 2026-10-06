// SPDX-License-Identifier: Apache-2.0
#include "TraceExport.h"
#include "motionConnectorVmc/FrameSource.h"
#include "Export.h"
#include "Options.h"
#include "motionConnectorVmc/PacketCapture.h"
#include "motionRecording/CaptureTrace.h"

#include <filesystem>
#include <iostream>
#include <system_error>

namespace vmc = openstrata::connectors::vmc;

namespace vmcRecordTool
{

void
TraceCollector::Observe(const std::vector<vmc::VmcFrame>& frames,
                        const openstrata::motion::SourceMetadata& metadata)
{
    for (const vmc::VmcFrame& frame : frames)
    {
        // A restart opens a session only when there is one to close. The
        // assembler never marks the first frame of a capture, but a collector
        // that assumed so would produce an empty leading session the first time
        // that changed.
        if (_sessions.empty() || (frame.beginsNewSession && !_sessions.back().samples.empty()))
        {
            _sessions.emplace_back();
        }
        openstrata::motion::MotionClip& session = _sessions.back();
        session.samples.push_back(frame.pose);
        session.source = metadata;
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

    // A push that delivered nothing can leave a session opened and never
    // filled; a capture whose every datagram was refused leaves one and only
    // one. Either way an empty animation is not a recording.
    for (std::size_t i = _sessions.size(); i-- != 0;)
    {
        if (_sessions[i].samples.empty())
        {
            _sessions.erase(_sessions.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

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

// Writes the canonical trace the export flag asked for. Returns false when
// nothing could be written, having said why -- the caller prints its report
// either way, for the reason `RunRecord` gives about the capture file.
//
// A capture holding two sessions is refused rather than resolved. Picking the
// first would silently discard a recording, and concatenating them would
// manufacture a continuity the sender's clock denies (TraceExport.h).
bool
ExportTrace(const Options& options, const vmc::PacketCapture& capture)
{
    // The second half of the refusal `ParseOptions` makes on the spelling. That
    // one catches `--inspect x --export-trace x`; this catches the same file
    // named two ways, which no comparison of strings can see. `equivalent`
    // answers only when both paths exist, and a trace path that does not exist
    // yet cannot be the capture, so the error code is discarded: "not the same
    // file" and "one of them is not there" are the same answer here.
    std::error_code aliased;
    if (std::filesystem::equivalent(options.inspectPath, options.traceExportPath, aliased)) {
        std::cerr << "vmc_record: " << options.traceExportPath
                  << " is the capture being read, named differently; writing "
                     "the trace there would destroy it\n";
        return false;
    }

    vmc::VmcFrameSource source(options.frame);
    source.SetSource(capture.peerEndpoint.empty() ? options.inspectPath : capture.peerEndpoint);
    TraceCollector collector;
    for (const vmc::RecordedDatagram& datagram : capture.datagrams) {
        source.PushDatagram(datagram.bytes, datagram.receiveTime);
        collector.Observe(source.GetFramesFromLastPush(), source.GetSourceMetadata());
    }
    source.Flush();
    collector.Observe(source.GetFramesFromLastPush(), source.GetSourceMetadata());
    collector.Close();
    const std::vector<openstrata::motion::MotionClip>& sessions = collector.GetSessions();

    if (sessions.empty()) {
        std::cerr << "vmc_record: nothing decoded into a frame, so there is no "
                     "trace to write\n";
        return false;
    }

    std::size_t index = 0;
    if (options.senderSession != 0) {
        if (options.senderSession > sessions.size()) {
            std::cerr << "vmc_record: --sender-session " << options.senderSession
                      << ": this recording holds " << sessions.size() << " session(s)\n";
            return false;
        }
        index = options.senderSession - 1;
    } else if (sessions.size() > 1) {
        std::cerr << "vmc_record: the sender restarted, so this recording holds " << sessions.size()
                  << " sessions whose clocks overlap; one trace is one session, "
                     "so name the one to export with --sender-session 1.."
                  << sessions.size() << "\n";
        return false;
    }

    const openstrata::motion::MotionClip& session = sessions[index];
    if (!openstrata::motion::WriteCaptureTraceFile(options.traceExportPath, session)) {
        // The writer refuses before its first byte when a frame carries an
        // expression name the format cannot spell, so a refusal here leaves the
        // path untouched rather than half-written.
        std::cerr << "vmc_record: could not write " << options.traceExportPath << "\n";
        return false;
    }
    if (!options.quiet) {
        std::cerr << "vmc_record: wrote " << session.samples.size() << " delivered frame(s)";
        if (sessions.size() > 1) {
            std::cerr << " of session " << (index + 1) << " of " << sessions.size();
        }
        std::cerr << " to " << options.traceExportPath << "\n";
    }
    return true;
}

} // namespace vmcRecordTool
