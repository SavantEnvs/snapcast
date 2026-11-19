// Fuzz the stream-plugin JSON model (server/streamreader/properties.cpp, metadata.cpp): what a stream
// control script sends in Plugin.Stream.Player.Properties, and what Stream.SetProperty carries.
#include "fuzz_common.hpp"
#include "streamreader/metadata.hpp"
#include "streamreader/properties.hpp"

#include <exception>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    json j = json::parse(data, data + size, nullptr, false);
    if (j.is_discarded())
        return 0;
    try
    {
        Properties props(j);
        (void)props.toJson().dump();
    }
    catch (const std::exception&)
    {
    }
    try
    {
        Metadata meta(j);
        (void)meta.toJson().dump();
    }
    catch (const std::exception&)
    {
    }
    if (j.is_object() && j.contains("metadata"))
    {
        try
        {
            Metadata meta(j["metadata"]);
            (void)meta.toJson().dump();
        }
        catch (const std::exception&)
        {
        }
    }
    return 0;
}
