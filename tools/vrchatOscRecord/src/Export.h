// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionConnectorVrchatOsc/PacketCapture.h"

namespace vrchatOscRecordTool {
struct Options;

// Semantic export is invoked only for --inspect --export-trace.
// Raw capture and reporting do not construct a motion intake or recorder.
bool ExportTrace(const Options& options,
                 const openstrata::connectors::vrchatOsc::PacketCapture& capture);
} // namespace vrchatOscRecordTool
