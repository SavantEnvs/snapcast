// Fuzz AuthInfo (server/authinfo.cpp): the "Authorization" value a control client sends
// (HTTP header / Client.Authenticate) or a stream client puts in its Hello message.
// Input: byte 0 selects the entry point, the rest is the credential.
#include "fuzz_common.hpp"
#include "authinfo.hpp"
#include "server_settings.hpp"

#include <exception>
#include <string>

static ServerSettings::Authorization make_settings()
{
    ServerSettings::Authorization auth;
    auth.enabled = true;
    auth.init({"admin:*", "player:Streaming,Client.*", "guest:"}, {"admin:secret:admin", "player:pw:player", "guest::guest"});
    return auth;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return -1;
    static const ServerSettings::Authorization settings = make_settings();
    std::string input(reinterpret_cast<const char*>(data + 1), size - 1);
    AuthInfo auth(settings);
    try
    {
        switch (data[0] % 4)
        {
            case 0:
                (void)auth.authenticate(input);
                break;
            case 1:
            {
                // scheme and param as the Hello message carries them, split at the first space
                auto sp = input.find(' ');
                (void)auth.authenticate(input.substr(0, sp), sp == std::string::npos ? "" : input.substr(sp + 1));
                break;
            }
            case 2:
                (void)auth.authenticateBasic(input);
                break;
            default:
                (void)auth.authenticatePlain(input);
                break;
        }
        (void)auth.isAuthenticated();
        (void)auth.username();
        (void)auth.hasPermission("Streaming");
        (void)auth.hasPermission("Client.SetVolume");
    }
    catch (const std::exception&)
    {
    }
    return 0;
}
