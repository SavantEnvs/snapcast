// Fuzz snapserver's handling of what a snapclient sends on the stream port (TCP 1704 / WebSocket):
// messages framed as StreamSessionTcp reads them (26-byte header, then `size` payload bytes), delivered
// to Server::onMessageReceived — Hello (client/group creation, auth), Time, ClientInfo (volume).
#include "fuzz_common.hpp"
#include "common/message/message.hpp"
#include "server_fixture.hpp"

#include <cstring>
#include <memory>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto& f = fuzz::fixture();
    f.reset();
    auto session = std::make_shared<fuzz::FakeStreamSession>(f.io_context().get_executor(), f.settings, f.stream());
    msg::BaseMessage base;
    const size_t header_size = base.getSize();
    std::vector<char> header(header_size);
    size_t pos = 0;
    while (size - pos >= header_size)
    {
        memcpy(header.data(), data + pos, header_size);
        pos += header_size;
        base.deserialize(header.data());
        // StreamSessionTcp::read_next (server/stream_session_tcp.cpp) drops the session on a type > kLast
        // or a size > msg::max_size, and otherwise reads exactly `size` payload bytes.
        if (base.type > message_type::kLast || base.size > msg::max_size || size - pos < base.size)
            break;
        std::vector<char> payload(data + pos, data + pos + base.size);
        pos += base.size;
        tv now;
        base.received = now;
        f.stream()->onMessageReceived(session, base, payload.data());
        f.io_context().poll();
        f.io_context().restart();
    }
    session->stop();
    return 0;
}
