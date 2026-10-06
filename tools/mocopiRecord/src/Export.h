// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionConnectorMocopi/PacketCapture.h"

namespace mocopiRecordTool {
struct Options;

// Semantic export is invoked only for --inspect --export-trace.
// Raw capture and reporting do not construct a motion intake or recorder.
bool ExportTrace(const Options& options,
                 const openstrata::connectors::mocopi::PacketCapture& capture);
} // namespace mocopiRecordTool
