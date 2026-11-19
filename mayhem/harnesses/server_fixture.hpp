// An in-process snapserver for the fuzz harnesses: a real Server (server/server.cpp) with its
// StreamManager, ControlRequestFactory and Config, but with every listener (TCP stream, TCP control,
// HTTP) disabled, so no socket is opened. One pipe stream source, created in a private temp dir, is the
// default stream that Hello assigns to clients. Inputs are delivered through the Server's public
// ControlMessageReceiver / StreamMessageReceiver interfaces, as ControlSession / StreamSession do.
//
// Per-input state: reset() builds a fresh io_context, Server and StreamManager (with its default stream)
// for every input, clears Config's groups/clients and empties the scratch stream dir, so streams that
// one input adds, removes or reconfigures (Stream.AddStream/RemoveStream/SetProperty/Control) never
// reach the next. Kept across inputs on purpose: the settings (read-only), the scratch dir and its
// default fifo (recreated identically by every Server), and Config's server.json path (Config::init
// reads that file only once, at startup). ImageCache is not reset: it is only filled from a stream's
// control script, and the plug-in dir below holds none.
#pragma once

#include "fuzz_common.hpp"
#include "common/json.hpp"
#include "common/stream_uri.hpp"
#include "config.hpp"
#include "control_session.hpp"
#include "server.hpp"
#include "server_settings.hpp"
#include "stream_session.hpp"

#include <boost/asio/io_context.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace fuzz
{

class FakeControlSession : public ControlSession
{
public:
    FakeControlSession(ControlMessageReceiver* receiver, const ServerSettings& settings) : ControlSession(receiver, settings)
    {
    }
    void start() override
    {
    }
    void stop() override
    {
    }
    void sendAsync(const std::string& message) override
    {
        (void)message;
    }
};

class FakeStreamSession : public StreamSession
{
public:
    FakeStreamSession(const boost::asio::any_io_executor& executor, const ServerSettings& settings, StreamMessageReceiver* receiver)
        : StreamSession(executor, settings, receiver)
    {
    }
    std::string getIP() override
    {
        return "127.0.0.1";
    }
    void start() override
    {
    }
    void stop() override
    {
    }

protected:
    void sendAsync(const shared_const_buffer& buffer, WriteHandler&& handler) override
    {
        if (handler)
            handler({}, buffer.message().data.size());
    }
};

/// A private scratch dir under $TMPDIR (fallback /tmp).
inline std::string make_scratch_dir()
{
    const char* tmp = getenv("TMPDIR");
    std::string tmpl = std::string((tmp != nullptr && *tmp != '\0') ? tmp : "/tmp") + "/snapfuzz.XXXXXX";
    char* dir = mkdtemp(tmpl.data());
    if (dir == nullptr)
        abort();
    return dir;
}

struct ServerFixture
{
    std::string datadir;
    std::string streamdir; // where Stream.AddStream's pipe:// and file:// paths are confined
    ServerSettings settings;
    std::unique_ptr<boost::asio::io_context> io_ctx;
    std::unique_ptr<Server> server;

    ServerFixture() : datadir(make_scratch_dir()), streamdir(datadir + "/streams")
    {
        settings.server.data_dir = datadir;
        settings.server.mdns_enabled = false;
        settings.tcp_stream.enabled = false;
        settings.tcp_control.enabled = false;
        settings.http.enabled = false;
        settings.http.ssl_enabled = false;
        settings.stream.sources = {"pipe://" + datadir + "/snapfifo?name=default&mode=create&codec=pcm"};
        // Stream.AddStream only accepts a controlscript inside plugin_dir; this one does not exist.
        settings.stream.plugin_dir = datadir + "/plug-ins";
        Config::instance().init(datadir);
    }

    void teardown()
    {
        if (server)
            server->stop();
        if (io_ctx)
        {
            // Run the handlers the stop cancelled while what they point at is still alive.
            io_ctx->restart();
            io_ctx->poll();
        }
        server.reset();
        io_ctx.reset(); // destroys any handler still queued without running it
    }

    /// Fresh server state for the next input.
    void reset()
    {
        teardown();
        std::error_code ec;
        std::filesystem::remove_all(streamdir, ec);
        std::filesystem::create_directory(streamdir, ec);
        Config::instance().groups.clear();
        io_ctx = std::make_unique<boost::asio::io_context>();
        server = std::make_unique<Server>(*io_ctx, settings);
        server->start();
    }

    boost::asio::io_context& io_context()
    {
        return *io_ctx;
    }

    ControlMessageReceiver* control()
    {
        return server.get();
    }

    StreamMessageReceiver* stream()
    {
        return server.get();
    }

    /// Keep one control message's Stream.AddStream requests inside the scratch dir: a pipe:// or file://
    /// path is moved into `streamdir` (same file name, so the path still reaches StreamUri and the
    /// stream's own open/mkfifo), meta:// only names other streams and is left alone, and any other
    /// scheme (process://, tcp://, airplay://, librespot://, ... which start a program or open a
    /// socket) makes the message unusable: returns false. A message that is not JSON, or has no
    /// AddStream, is left byte-for-byte unchanged; one with an AddStream is re-serialized.
    bool confine_add_stream(std::string& message) const
    {
        using json = nlohmann::json;
        json j = json::parse(message, nullptr, false);
        if (j.is_discarded())
            return true;
        bool changed = false;
        auto fix = [&](json& req) -> bool
        {
            if (!req.is_object())
                return true;
            auto m = req.find("method");
            if (m == req.end() || !m->is_string() || m->get<std::string>() != "Stream.AddStream")
                return true;
            auto p = req.find("params");
            if (p == req.end() || !p->is_object())
                return true;
            auto u = p->find("streamUri");
            if (u == p->end() || !u->is_string())
                return true;
            try
            {
                StreamUri uri(u->get<std::string>());
                if (uri.scheme == "meta")
                    return true;
                if (uri.scheme != "pipe" && uri.scheme != "file")
                    return false;
                // The file name only, reduced to [A-Za-z0-9._-] so toString() + the re-parse in
                // StreamAddRequest (which uri-decodes again) cannot turn it into a path.
                std::string name = std::filesystem::path(uri.path).filename().string();
                for (char& c : name)
                    if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
                        c = '_';
                if (name.find_first_not_of('.') == std::string::npos)
                    name = "stream";
                uri.host.clear();
                uri.path = streamdir + "/" + name;
                *u = uri.toString();
                changed = true;
                return true;
            }
            catch (const std::exception&)
            {
                return true; // StreamAddRequest parses the same string first and throws the same way
            }
        };
        if (j.is_array())
        {
            for (auto& req : j)
                if (!fix(req))
                    return false;
        }
        else if (!fix(j))
            return false;
        if (changed)
            message = j.dump();
        return true;
    }
};

inline ServerFixture& fixture()
{
    static ServerFixture* f = new ServerFixture();
    return *f;
}

} // namespace fuzz
