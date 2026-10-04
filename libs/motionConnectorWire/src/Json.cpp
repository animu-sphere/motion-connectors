// SPDX-License-Identifier: Apache-2.0
#include "Json.h"

#include <algorithm>
#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <utility>

namespace openstrata::connectors::wire::json
{

namespace
{

bool
IsIdentifier(std::string_view key) noexcept
{
    if (key.empty())
    {
        return false;
    }
    const auto start = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    if (!start(key.front()))
    {
        return false;
    }
    return std::all_of(key.begin() + 1, key.end(),
                       [&](char c) { return start(c) || (c >= '0' && c <= '9'); });
}

// The length of the UTF-8 sequence starting at `bytes`, or 0 when it is not
// well formed: RFC 3629's table, which refuses overlong forms, UTF-16
// surrogates and anything past U+10FFFF by bounding the second byte.
std::size_t
Utf8SequenceLength(const unsigned char* bytes, std::size_t remaining) noexcept
{
    const unsigned char lead = bytes[0];
    if (lead < 0x80)
    {
        return 1;
    }
    std::size_t length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF)
    {
        length = 2;
    }
    else if (lead == 0xE0)
    {
        length = 3;
        low = 0xA0;
    }
    else if ((lead >= 0xE1 && lead <= 0xEC) || lead == 0xEE || lead == 0xEF)
    {
        length = 3;
    }
    else if (lead == 0xED)
    {
        length = 3;
        high = 0x9F;
    }
    else if (lead == 0xF0)
    {
        length = 4;
        low = 0x90;
    }
    else if (lead >= 0xF1 && lead <= 0xF3)
    {
        length = 4;
    }
    else if (lead == 0xF4)
    {
        length = 4;
        high = 0x8F;
    }
    else
    {
        return 0;
    }
    if (remaining < length || bytes[1] < low || bytes[1] > high)
    {
        return 0;
    }
    for (std::size_t index = 2; index < length; ++index)
    {
        if (bytes[index] < 0x80 || bytes[index] > 0xBF)
        {
            return 0;
        }
    }
    return length;
}

void
AppendCodePoint(std::string& out, std::uint32_t codePoint)
{
    if (codePoint < 0x80)
    {
        out.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else if (codePoint < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

class Parser final
{
  public:
    Parser(std::string_view text, std::size_t maxDepth, WireError* error)
        : _text(text), _maxDepth(maxDepth), _error(error)
    {
    }

    bool
    Run(Value* root)
    {
        // A byte order mark is not whitespace (RFC 8259 §8.1): a writer must
        // not emit one, and reading past it would accept two spellings of one
        // message.
        SkipWhitespace();
        if (!ParseValue(root, 0))
        {
            return false;
        }
        SkipWhitespace();
        if (_position != _text.size())
        {
            return Malformed("unexpected content after the message");
        }
        return true;
    }

  private:
    bool
    Fail(WireErrorCode code, std::string subject, std::string detail)
    {
        if (_error)
        {
            _error->code = code;
            _error->subject = std::move(subject);
            _error->detail = std::move(detail);
        }
        return false;
    }

    bool
    Malformed(std::string detail)
    {
        return Fail(WireErrorCode::MessageMalformed, "byte " + std::to_string(_position),
                    std::move(detail));
    }

    bool
    AtEnd() const noexcept
    {
        return _position >= _text.size();
    }

    char
    Peek() const noexcept
    {
        return _text[_position];
    }

    void
    SkipWhitespace() noexcept
    {
        while (!AtEnd())
        {
            const char c = Peek();
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            {
                return;
            }
            ++_position;
        }
    }

    bool
    ParseValue(Value* value, std::size_t depth)
    {
        if (AtEnd())
        {
            return Malformed("the message ends where a value was expected");
        }
        switch (Peek())
        {
        case '{':
            return ParseObject(value, depth + 1);
        case '[':
            return ParseArray(value, depth + 1);
        case '"':
            value->kind = Value::Kind::String;
            return ParseString(&value->text);
        case 't':
            value->kind = Value::Kind::Boolean;
            value->boolean = true;
            return ParseLiteral("true");
        case 'f':
            value->kind = Value::Kind::Boolean;
            value->boolean = false;
            return ParseLiteral("false");
        case 'n':
            value->kind = Value::Kind::Null;
            return ParseLiteral("null");
        default:
            value->kind = Value::Kind::Number;
            return ParseNumber(&value->text);
        }
    }

    bool
    ParseLiteral(std::string_view literal)
    {
        if (_text.substr(_position, literal.size()) != literal)
        {
            return Malformed("not a JSON value");
        }
        _position += literal.size();
        return true;
    }

    bool
    ParseNumber(std::string* token)
    {
        const std::size_t start = _position;
        const auto digit = [this] { return !AtEnd() && Peek() >= '0' && Peek() <= '9'; };
        const auto digits = [&] {
            if (!digit())
            {
                return false;
            }
            while (digit())
            {
                ++_position;
            }
            return true;
        };

        if (!AtEnd() && Peek() == '-')
        {
            ++_position;
        }
        if (!AtEnd() && Peek() == '0')
        {
            ++_position;
        }
        else if (!digits())
        {
            return Malformed("not a JSON value");
        }
        if (!AtEnd() && Peek() == '.')
        {
            ++_position;
            if (!digits())
            {
                return Malformed("a fraction needs a digit");
            }
        }
        if (!AtEnd() && (Peek() == 'e' || Peek() == 'E'))
        {
            ++_position;
            if (!AtEnd() && (Peek() == '+' || Peek() == '-'))
            {
                ++_position;
            }
            if (!digits())
            {
                return Malformed("an exponent needs a digit");
            }
        }
        token->assign(_text.substr(start, _position - start));
        return true;
    }

    bool
    ParseHex4(std::uint32_t* unit)
    {
        if (_text.size() - _position < 4)
        {
            return Malformed("a \\u escape needs four hex digits");
        }
        std::uint32_t result = 0;
        for (int index = 0; index < 4; ++index)
        {
            const char c = Peek();
            result <<= 4;
            if (c >= '0' && c <= '9')
            {
                result |= static_cast<std::uint32_t>(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                result |= static_cast<std::uint32_t>(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                result |= static_cast<std::uint32_t>(c - 'A' + 10);
            }
            else
            {
                return Malformed("a \\u escape needs four hex digits");
            }
            ++_position;
        }
        *unit = result;
        return true;
    }

    bool
    ParseEscape(std::string* out)
    {
        // At the backslash.
        ++_position;
        if (AtEnd())
        {
            return Malformed("the message ends inside an escape");
        }
        const char c = Peek();
        ++_position;
        switch (c)
        {
        case '"':
        case '\\':
        case '/':
            out->push_back(c);
            return true;
        case 'b':
            out->push_back('\b');
            return true;
        case 'f':
            out->push_back('\f');
            return true;
        case 'n':
            out->push_back('\n');
            return true;
        case 'r':
            out->push_back('\r');
            return true;
        case 't':
            out->push_back('\t');
            return true;
        case 'u':
            break;
        default:
            --_position;
            return Malformed("not a JSON escape");
        }

        const std::size_t escapeStart = _position - 2;
        std::uint32_t unit = 0;
        if (!ParseHex4(&unit))
        {
            return false;
        }
        if (unit >= 0xDC00 && unit <= 0xDFFF)
        {
            _position = escapeStart;
            return Malformed("a low surrogate escape without a high one");
        }
        if (unit >= 0xD800 && unit <= 0xDBFF)
        {
            std::uint32_t low = 0;
            if (_text.substr(_position, 2) != "\\u")
            {
                _position = escapeStart;
                return Malformed("a high surrogate escape without a low one");
            }
            _position += 2;
            if (!ParseHex4(&low))
            {
                return false;
            }
            if (low < 0xDC00 || low > 0xDFFF)
            {
                _position = escapeStart;
                return Malformed("a high surrogate escape without a low one");
            }
            unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
        }
        AppendCodePoint(*out, unit);
        return true;
    }

    bool
    ParseString(std::string* out)
    {
        // At the opening quote.
        ++_position;
        out->clear();
        while (true)
        {
            if (AtEnd())
            {
                return Malformed("the message ends inside a string");
            }
            const auto byte = static_cast<unsigned char>(Peek());
            if (byte == '"')
            {
                ++_position;
                return true;
            }
            if (byte == '\\')
            {
                if (!ParseEscape(out))
                {
                    return false;
                }
                continue;
            }
            if (byte < 0x20)
            {
                return Malformed("an unescaped control character in a string");
            }
            const std::size_t length = Utf8SequenceLength(
                reinterpret_cast<const unsigned char*>(_text.data()) + _position,
                _text.size() - _position);
            if (length == 0)
            {
                return Malformed("not UTF-8");
            }
            out->append(_text.substr(_position, length));
            _position += length;
        }
    }

    bool
    Nest(std::size_t depth)
    {
        if (depth > _maxDepth)
        {
            return Fail(WireErrorCode::NestingTooDeep, "byte " + std::to_string(_position),
                        "nested deeper than the format has");
        }
        return true;
    }

    bool
    ParseArray(Value* value, std::size_t depth)
    {
        if (!Nest(depth))
        {
            return false;
        }
        value->kind = Value::Kind::Array;
        ++_position;
        SkipWhitespace();
        if (!AtEnd() && Peek() == ']')
        {
            ++_position;
            return true;
        }
        while (true)
        {
            _path.PushIndex(value->elements.size());
            value->elements.emplace_back();
            SkipWhitespace();
            if (!ParseValue(&value->elements.back(), depth))
            {
                return false;
            }
            _path.Pop();
            SkipWhitespace();
            if (AtEnd())
            {
                return Malformed("the message ends inside an array");
            }
            if (Peek() == ']')
            {
                ++_position;
                return true;
            }
            if (Peek() != ',')
            {
                return Malformed("expected ',' or ']'");
            }
            ++_position;
        }
    }

    bool
    ParseObject(Value* value, std::size_t depth)
    {
        if (!Nest(depth))
        {
            return false;
        }
        value->kind = Value::Kind::Object;
        ++_position;
        SkipWhitespace();
        if (!AtEnd() && Peek() == '}')
        {
            ++_position;
            return true;
        }
        while (true)
        {
            SkipWhitespace();
            if (AtEnd() || Peek() != '"')
            {
                return Malformed("expected a key");
            }
            Member& member = value->members.emplace_back();
            if (!ParseString(&member.key))
            {
                return false;
            }
            SkipWhitespace();
            if (AtEnd() || Peek() != ':')
            {
                return Malformed("expected ':'");
            }
            ++_position;
            SkipWhitespace();
            _path.PushKey(member.key);
            if (!ParseValue(&member.value, depth))
            {
                return false;
            }
            _path.Pop();
            SkipWhitespace();
            if (AtEnd())
            {
                return Malformed("the message ends inside an object");
            }
            if (Peek() == '}')
            {
                ++_position;
                return CheckUniqueKeys(*value);
            }
            if (Peek() != ',')
            {
                return Malformed("expected ',' or '}'");
            }
            ++_position;
        }
    }

    // Sorted rather than compared pairwise, so a message of many keys costs
    // n log n and not n^2. The member reported is the first, in message order,
    // whose key appeared before it -- the same answer a reader walking the
    // message would give.
    bool
    CheckUniqueKeys(const Value& object)
    {
        std::vector<std::size_t> order(object.members.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return object.members[a].key < object.members[b].key;
        });
        std::size_t repeat = object.members.size();
        for (std::size_t index = 1; index < order.size(); ++index)
        {
            if (object.members[order[index]].key == object.members[order[index - 1]].key)
            {
                repeat = std::min(repeat, order[index]);
            }
        }
        if (repeat == object.members.size())
        {
            return true;
        }
        _path.PushKey(object.members[repeat].key);
        std::string subject = _path.ToString();
        _path.Pop();
        return Fail(WireErrorCode::KeyDuplicate, std::move(subject), "a key appears twice");
    }

    std::string_view _text;
    std::size_t _maxDepth;
    WireError* _error;
    std::size_t _position = 0;
    Path _path;
};

// strtod and strtof are correctly rounded on all three lanes, and libc++'s
// floating-point from_chars arrived after the toolchains this builds on (as
// usd-motion-plugins' BvhParser.cpp found). Their one hazard is LC_NUMERIC,
// which belongs to the host process: under a comma-decimal locale they would
// stop at the '.'. The token is JSON, so its point is always '.', and it is
// translated to whatever the process is using rather than assumed.
template <class T, class Convert>
bool
ParseNumberToken(std::string_view token, T* value, Convert convert)
{
    std::string buffer(token);
    const char point = *std::localeconv()->decimal_point;
    if (point != '.')
    {
        std::replace(buffer.begin(), buffer.end(), '.', point);
    }
    char* end = nullptr;
    const T result = convert(buffer.c_str(), &end);
    if (end != buffer.c_str() + buffer.size() || !std::isfinite(result))
    {
        return false;
    }
    *value = result;
    return true;
}

template <class T>
void
AppendShortest(std::string& out, T value)
{
    char buffer[64];
    const auto [end, status] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, status == std::errc() ? end : buffer);
}

} // namespace

void
Path::PushKey(std::string_view key)
{
    _segments.push_back({std::string(key), 0, false});
}

void
Path::PushIndex(std::size_t index)
{
    _segments.push_back({std::string(), index, true});
}

void
Path::Pop() noexcept
{
    _segments.pop_back();
}

std::string
Path::ToString() const
{
    std::string result = "$";
    for (const Segment& segment : _segments)
    {
        if (segment.isIndex)
        {
            result += '[' + std::to_string(segment.index) + ']';
        }
        else if (IsIdentifier(segment.key))
        {
            result += '.' + segment.key;
        }
        else
        {
            result += '[';
            // A key the encoder was handed may not be UTF-8, and the subject
            // must still be text; the reader never gets here with one.
            if (IsValidUtf8(segment.key))
            {
                AppendString(result, segment.key);
            }
            else
            {
                result += "\"?\"";
            }
            result += ']';
        }
    }
    return result;
}

bool
Parse(std::string_view text, std::size_t maxDepth, Value* root, WireError* error)
{
    Value result;
    if (!Parser(text, maxDepth, error).Run(&result))
    {
        return false;
    }
    *root = std::move(result);
    return true;
}

bool
IsValidUtf8(std::string_view text) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    std::size_t position = 0;
    while (position < text.size())
    {
        const std::size_t length = Utf8SequenceLength(bytes + position, text.size() - position);
        if (length == 0)
        {
            return false;
        }
        position += length;
    }
    return true;
}

void
AppendString(std::string& out, std::string_view text)
{
    static constexpr char kHex[] = "0123456789abcdef";
    out.push_back('"');
    for (const char c : text)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                out += "\\u00";
                out.push_back(kHex[static_cast<unsigned char>(c) >> 4]);
                out.push_back(kHex[static_cast<unsigned char>(c) & 0xF]);
            }
            else
            {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

void
AppendFloat(std::string& out, float value)
{
    AppendShortest(out, value);
}

void
AppendDouble(std::string& out, double value)
{
    AppendShortest(out, value);
}

bool
ParseFloat(std::string_view token, float* value)
{
    return ParseNumberToken(token, value, [](const char* text, char** end) {
        return std::strtof(text, end);
    });
}

bool
ParseDouble(std::string_view token, double* value)
{
    return ParseNumberToken(token, value, [](const char* text, char** end) {
        return std::strtod(text, end);
    });
}

} // namespace openstrata::connectors::wire::json
