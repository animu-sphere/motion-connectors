// SPDX-License-Identifier: Apache-2.0
//
// The `MotionFrame` wire format, version 1: one `MotionFrame` as one UTF-8
// JSON message identified as `openstrata.motion.frame/v1`
// (docs/design/FRAME_WIRE_FORMAT.md). This layer spells a frame as bytes and
// reads it back. It opens no socket, keeps no session and knows no connector:
// the WebSocket connector, `motion_connect bridge` and a test harness each call
// it, and only the first of them owns a connection (§8).
//
// Three rules are worth stating before the API, because each is a decision
// rather than a detail.
//
// **The codec is a pure function both ways** (§7). It reads and writes every
// field, `receiveTimestamp` and `frameNumber` included, and decode after encode
// returns a frame equal under the motion contract's `==`. Deciding that a
// received frame takes the receiver's clock and its own frame number is the
// receiving connector's job, one step up.
//
// **A message decodes entirely or not at all** (§6). On a refusal the output
// frame is left untouched, so a caller can never act on half a message.
//
// **The writer refuses what the reader would refuse.** A non-finite number, a
// zero or non-unit quaternion, a repeated actor, tracker or channel, and a
// string that is not UTF-8 are refused at encode time rather than emitted, so
// encode-then-decode cannot fail on a message this library wrote. Each of those
// would already be a connector bug (CONNECTOR_CONTRACT §10); the refusal names
// where it is.
#pragma once

#include "motionConnectorWire/api.h"

#include "motionConnectorCore/Types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openstrata::connectors::wire
{

// The only format this version reads and writes (§2, §5).
inline constexpr std::string_view FrameFormatV1 = "openstrata.motion.frame/v1";

// The declared maximum message size, checked before a byte is parsed (§6). A
// full 55-joint pose with a face's worth of channels is a few kilobytes, so a
// mebibyte leaves room for many actors and is still a bound.
inline constexpr std::size_t DefaultMaxMessageBytes = std::size_t{1} << 20;

// The codec's diagnostic namespace (docs/reference/DIAGNOSTICS.md). Every code
// is a warning and recoverable: a refused message is a dropped message and the
// session continues. The stable string is the contract; the enumerator spelling
// is not. Append only.
enum class WireErrorCode : std::uint8_t
{
    MessageTooLarge,    // WIRE_MESSAGE_TOO_LARGE
    MessageMalformed,   // WIRE_MESSAGE_MALFORMED: not JSON text, or not UTF-8
    NestingTooDeep,     // WIRE_NESTING_TOO_DEEP
    KeyDuplicate,       // WIRE_KEY_DUPLICATE
    FormatUnknown,      // WIRE_FORMAT_UNKNOWN
    KeyUnknown,         // WIRE_KEY_UNKNOWN
    KeyMissing,         // WIRE_KEY_MISSING
    TypeMismatch,       // WIRE_TYPE_MISMATCH: the wrong JSON type or array length
    ValueUnknown,       // WIRE_VALUE_UNKNOWN: a word outside its vocabulary
    JointUnknown,       // WIRE_JOINT_UNKNOWN
    ConfidenceMismatch, // WIRE_CONFIDENCE_MISMATCH
    NumberOutOfRange,   // WIRE_NUMBER_OUT_OF_RANGE
    QuaternionInvalid,  // WIRE_QUATERNION_INVALID
    CounterInvalid,     // WIRE_COUNTER_INVALID
    IdDuplicate,        // WIRE_ID_DUPLICATE
    ValueNotFinite,     // WIRE_VALUE_NOT_FINITE: raised by the writer only
};

// What a refusal says. `subject` is a path into the message -- `$` for the
// whole of it, `$.actors[0].pose.joints.leftHand`, or `$.actors[0].pose
// .channels["vrm:aa"]` for a key that is not an identifier -- or `byte N` when
// the text is not JSON and there is no path to name. Tests assert the code and
// the subject, never the detail.
struct WireError
{
    WireErrorCode code = WireErrorCode::MessageMalformed;
    std::string subject;
    std::string detail;
};

// The stable string for `code`, or empty for a value outside the enum.
MOTIONCONNECTORWIRE_API std::string_view WireErrorCodeName(WireErrorCode code) noexcept;
MOTIONCONNECTORWIRE_API std::optional<WireErrorCode>
FindWireErrorCode(std::string_view name) noexcept;

struct DecodeOptions
{
    std::size_t maxMessageBytes = DefaultMaxMessageBytes;
};

// Writes `frame` as one message, replacing `*message`. On a refusal `*message`
// is left untouched and `error`, when given, says why. The output is
// deterministic (§4.5): keys in the format's table order, joints in vocabulary
// order, channels and confidence in name order, and every number in the
// shortest form that reads back to the same `float` or `double`.
MOTIONCONNECTORWIRE_API bool EncodeFrame(const core::MotionFrame& frame, std::string* message,
                                         WireError* error = nullptr);

// Reads one message into `*frame`. On a refusal `*frame` is left untouched and
// `error`, when given, says why.
MOTIONCONNECTORWIRE_API bool DecodeFrame(std::string_view message, core::MotionFrame* frame,
                                         WireError* error = nullptr,
                                         const DecodeOptions& options = {});

} // namespace openstrata::connectors::wire
