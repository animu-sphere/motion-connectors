// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionConnectorVmc/PacketCapture.h"

namespace vmcRecordTool {
struct Options;

// Semantic export is invoked only for --inspect --export-trace.
// Raw capture and reporting do not construct a motion intake or recorder.
bool ExportTrace(const Options& options,
                 const openstrata::connectors::vmc::PacketCapture& capture);
} // namespace vmcRecordTool
