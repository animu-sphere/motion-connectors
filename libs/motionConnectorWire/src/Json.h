// SPDX-License-Identifier: Apache-2.0
//
// The strict JSON layer underneath the frame codec: a reader that refuses
// everything FRAME_WIRE_FORMAT §6 says a reader refuses at the text level, and
// the few writing primitives the encoder needs. Private to this library.
//
// It is written here rather than taken from a JSON package for the reason
// DEPENDENCIES.md §4 leaves open and this library closes: §6's refusals are
// *properties of the parse*. A duplicate key has to be seen before a DOM keeps
// one of the two, nesting has to be bounded before the recursion that would
// overflow, and a number has to stay text until the field it lands in says
// whether it is a `float` or a `double`. The packages that do all three are
// configuration surfaces larger than this file, and the browser side reads the
// same message with `JSON.parse` and no package at all (§2).
#pragma once

#include "motionConnectorWire/FrameWire.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openstrata::connectors::wire::json
{

struct Member;

// One parsed value. A number keeps the token exactly as written, because only
// the field it is read into knows its C++ type; a string holds its decoded
// UTF-8. An object keeps its members in message order, keys unique.
struct Value
{
    enum class Kind : std::uint8_t
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object,
    };

    Kind kind = Kind::Null;
    bool boolean = false;
    std::string text;
    std::vector<Value> elements;
    std::vector<Member> members;
};

struct Member
{
    std::string key;
    Value value;
};

// Where a refusal is, as the subject a `WireError` carries: `$`, then `.key`
// for an identifier, `["key"]` for anything else, and `[index]`.
class Path final
{
  public:
    void PushKey(std::string_view key);
    void PushIndex(std::size_t index);
    void Pop() noexcept;

    std::string ToString() const;

  private:
    struct Segment
    {
        std::string key;
        std::size_t index = 0;
        bool isIndex = false;
    };

    std::vector<Segment> _segments;
};

// Parses `text` as exactly one JSON value. Containers nest at most `maxDepth`
// deep -- the outermost object is depth 1 -- so the recursion is bounded by the
// format rather than by the sender. Refuses invalid UTF-8 (overlong forms,
// surrogates and lone surrogate escapes included), unescaped control
// characters, a byte order mark, trailing content and a duplicate key.
bool Parse(std::string_view text, std::size_t maxDepth, Value* root, WireError* error);

// Whether `text` is well-formed UTF-8 by the same rules the reader applies.
bool IsValidUtf8(std::string_view text) noexcept;

// Appends `text` as a JSON string. `text` must be valid UTF-8.
void AppendString(std::string& out, std::string_view text);

// Appends the shortest decimal that reads back to exactly `value`. `value`
// must be finite.
void AppendFloat(std::string& out, float value);
void AppendDouble(std::string& out, double value);

// Reads a number token the parser accepted. False when it overflows the type;
// an underflow to a subnormal or zero is a value, not a refusal.
bool ParseFloat(std::string_view token, float* value);
bool ParseDouble(std::string_view token, double* value);

} // namespace openstrata::connectors::wire::json
