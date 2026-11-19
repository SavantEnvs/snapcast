// Fuzz snapserver's HTTP/WebSocket front end on port 1780: ControlSessionHttp (server/control_session_http.cpp)
// reading raw client bytes from a real TCP socket, exactly as ControlServer hands it an accepted
// connection. That covers Beast's request parsing, the Authorization header (AuthInfo), the request-target
// checks (absolute, no ".."), the /__image_cache?name= lookup, path_cat + doc_root file serving (GET/HEAD,
// keep-alive request sequences), and the WebSocket upgrade to ControlSessionWebsocket on /jsonrpc
// (frames after the handshake). The JSON-RPC bodies themselves are fuzzed by `control`; here they go to a
// receiver that answers every message with a fixed response, so no server state is involved.
//
// Input: the bytes a client sends on one connection, then end of stream (client shutdown(send)).
// Kept across inputs on purpose: the io_context, a loopback acceptor and a read-only doc_root fixture
// (an index.html and one asset, written once); every input gets a new connection and session.
#include "fuzz_common.hpp"
#include "control_session.hpp"
#include "control_session_http.hpp"
#include "server_fixture.hpp"
#include "server_settings.hpp"
#include "stream_session.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

namespace
{

using boost::asio::ip::tcp;

class StubReceiver : public ControlMessageReceiver
{
public:
    void onMessageReceived(std::shared_ptr<ControlSession> session, const std::string& message, const ResponseHandler& response_handler) override
    {
        (void)session;
        (void)message;
        response_handler(R"({"id":1,"jsonrpc":"2.0","result":"ok"})");
    }
    // ControlServer::onNewSession starts the session it is handed (here: a WebSocket on /jsonrpc).
    void onNewSession(std::shared_ptr<ControlSession> session) override
    {
        session->start();
    }
    // The /stream WebSocket carries the binary stream protocol, fuzzed by `stream-session`; the upgraded
    // session is not started (it would need a Server as its StreamMessageReceiver).
    void onNewSession(std::shared_ptr<StreamSession> session) override
    {
        (void)session;
    }
};

struct HttpFixture
{
    boost::asio::io_context ioc;
    tcp::acceptor acceptor{ioc, tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0)};
    ServerSettings settings;
    StubReceiver receiver;

    HttpFixture()
    {
        std::string root = fuzz::make_scratch_dir() + "/www";
        std::filesystem::create_directories(root + "/css");
        std::ofstream(root + "/index.html") << "<html><body>snapweb</body></html>\n";
        std::ofstream(root + "/css/app.css") << "body{}\n";
        settings.http.doc_root = root;
        settings.http.ssl_enabled = false;
    }
};

HttpFixture& http_fixture()
{
    static HttpFixture* f = new HttpFixture();
    return *f;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto& f = http_fixture();
    tcp::socket client(f.ioc);
    client.connect(f.acceptor.local_endpoint());
    tcp::socket server_side(f.ioc);
    f.acceptor.accept(server_side);

    auto session = std::make_shared<ControlSessionHttp>(&f.receiver, std::move(server_side), f.settings);
    session->start();

    // The client: send the input, half-close, and drain whatever the server answers until it closes.
    std::string request(reinterpret_cast<const char*>(data), size);
    boost::asio::async_write(client, boost::asio::buffer(request), [&client](boost::system::error_code ec, size_t)
    {
        boost::system::error_code ignored;
        if (!ec)
            client.shutdown(tcp::socket::shutdown_send, ignored);
    });
    std::array<char, 16384> sink;
    std::function<void(boost::system::error_code, size_t)> drain = [&](boost::system::error_code ec, size_t)
    {
        if (!ec)
            client.async_read_some(boost::asio::buffer(sink), drain);
    };
    client.async_read_some(boost::asio::buffer(sink), drain);

    // Drop our reference: the session lives exactly as long as its pending reads/writes, as in the server.
    session.reset();
    f.ioc.run();
    f.ioc.restart();
    return 0;
}
