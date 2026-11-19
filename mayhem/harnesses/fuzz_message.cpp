// Fuzz the snapcast binary wire protocol: BaseMessage header + msg::factory::createMessage for every
// message type (Hello, ClientInfo, ServerSettings, Time, CodecHeader, WireChunk/PcmChunk, Error), as
// both snapserver (stream_session_tcp.cpp) and snapclient (client_connection.cpp) read it from the socket.
// The input is a stream of messages: 26-byte header, then `size` payload bytes.
#include "fuzz_common.hpp"
#include "common/message/factory.hpp"
#include "common/message/message.hpp"

#include <cstdint>
#include <cstring>
#include <exception>
#include <sstream>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    msg::BaseMessage base;
    const size_t header_size = base.getSize();
    size_t pos = 0;
    std::vector<char> header(header_size);
    while (size - pos >= header_size)
    {
        memcpy(header.data(), data + pos, header_size);
        pos += header_size;
        base.deserialize(header.data());
        // The TCP sessions (server/stream_session_tcp.cpp, client/client_connection.cpp) drop a message
        // whose type is > kLast or whose size is > msg::max_size before reading its payload.
        if (base.type > message_type::kLast || base.size > msg::max_size)
            return 0;
        // Then they read exactly `size` payload bytes; a short read closes the connection.
        if (size - pos < base.size)
            return 0;
        // Exactly `size` bytes, so ASan sees any read past the payload.
        std::vector<char> payload(data + pos, data + pos + base.size);
        pos += base.size;
        try
        {
            auto message = msg::factory::createMessage(base, payload.data());
            if (message)
            {
                // Re-serialize, as both peers do when they reply or forward.
                std::ostringstream os;
                message->serialize(os);
            }
        }
        catch (const std::exception&)
        {
            // nlohmann::json parse errors in the JSON messages are handled the same way by the peers.
        }
    }
    return 0;
}
