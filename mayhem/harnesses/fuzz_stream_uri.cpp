// Fuzz StreamUri (common/stream_uri.cpp): the stream source URI parser used for snapserver's
// `source =` settings and for JSON-RPC Stream.AddStream.
#include "fuzz_common.hpp"
#include "common/stream_uri.hpp"

#include <exception>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    std::string input(reinterpret_cast<const char*>(data), size);
    try
    {
        StreamUri uri(input);
        (void)uri.toJson().dump();
        (void)uri.toString();
        (void)uri.getQuery("name", "");
        (void)uri.getQuery("codec", "flac");
        (void)uri.getQuery("sampleformat", "48000:16:2");
        StreamUri again(uri.toString());
        (void)again.toJson().dump();
    }
    catch (const std::exception&)
    {
    }
    return 0;
}
