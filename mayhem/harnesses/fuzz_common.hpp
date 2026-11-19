// Shared by the snapcast fuzz harnesses: input framing, and silent logging.
// An input is a sequence of segments, each a little-endian u16 length followed by that many bytes.
// A length running past the end of the input takes the rest of the input.
#pragma once
#include "common/aixlog.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fuzz
{

// snapcast logs through AixLog on std::clog; until AixLog::Log is initialized std::clog still writes
// to stderr. Initializing it with no sinks (as snapserver/snapclient do before adding theirs) drops
// the log lines, which would otherwise dominate every execution.
inline const bool kQuietLogging = (AixLog::Log::init({}), true);

inline std::vector<std::string> split_segments(const uint8_t* data, size_t size)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos + 2 <= size)
    {
        size_t len = static_cast<size_t>(data[pos]) | (static_cast<size_t>(data[pos + 1]) << 8);
        pos += 2;
        if (len > size - pos)
            len = size - pos;
        out.emplace_back(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
    }
    return out;
}

} // namespace fuzz
