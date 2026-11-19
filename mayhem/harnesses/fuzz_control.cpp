// Fuzz snapserver's JSON-RPC control API (TCP 1705, HTTP/WebSocket 1780): one message as
// ControlSessionTcp/Http/Ws hand it to Server::onMessageReceived — the jsonrpcpp parser (requests,
// notifications, batches), ControlRequestFactory and every Client.*/Group.*/Server.*/Stream.* request.
// Input: segments (fuzz_common.hpp), each one control message on the same session, against a server
// rebuilt for every input (server_fixture.hpp). Stream.AddStream URIs are confined to the fixture's
// scratch dir first (ServerFixture::confine_add_stream): process://, tcp:// and the other schemes
// that start programs or open sockets are rejected, pipe:// and file:// paths are moved into it.
#include "fuzz_common.hpp"
#include "server_fixture.hpp"

#include <memory>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto& f = fuzz::fixture();
    auto segments = fuzz::split_segments(data, size);
    if (segments.empty())
        return -1;
    for (auto& message : segments)
        if (!f.confine_add_stream(message))
            return -1;
    f.reset();
    auto session = std::make_shared<fuzz::FakeControlSession>(f.control(), f.settings);
    for (const auto& message : segments)
    {
        f.control()->onMessageReceived(session, message, [](const std::string& response) { (void)response; });
        f.io_context().poll();
        f.io_context().restart();
    }
    return 0;
}
