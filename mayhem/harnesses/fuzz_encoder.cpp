// Fuzz the snapserver encoders (server/encoder/*) through EncoderFactory::createEncoder, as a stream
// source's `codec=` setting selects them, fed with the PCM a pipe/tcp/file/process source delivers.
// Input: segments (fuzz_common.hpp):
//   [0] codec setting string, "<codec>[:<options>]" (pcm, flac, ogg, opus, null)
//   [1] sample format selector: byte 0 = rate, byte 1 = bits, byte 2 = channels (indexes below)
//   [2..] PCM chunk payloads
#include "common/message/pcm_chunk.hpp"
#include "common/sample_format.hpp"
#include "encoder/encoder.hpp"
#include "encoder/encoder_factory.hpp"
#include "fuzz_common.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>

// Formats a snapserver stream can be configured with (sampleformat=<rate>:<bits>:<channels>).
static const uint32_t kRates[] = {8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 192000};
static const uint16_t kBits[] = {8, 16, 24, 32};
static const uint16_t kChannels[] = {1, 2, 3, 4, 5, 6, 7, 8};

template <typename T, size_t N>
static T pick(const T (&arr)[N], const std::string& s, size_t i)
{
    return arr[(i < s.size() ? static_cast<uint8_t>(s[i]) : 0) % N];
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto segments = fuzz::split_segments(data, size);
    if (segments.size() < 2)
        return -1;

    SampleFormat format(pick(kRates, segments[1], 0), pick(kBits, segments[1], 1), pick(kChannels, segments[1], 2));

    encoder::EncoderFactory factory;
    std::unique_ptr<encoder::Encoder> enc;
    try
    {
        enc = factory.createEncoder(segments[0]);
        enc->init([](const encoder::Encoder& /*e*/, std::shared_ptr<msg::PcmChunk> /*chunk*/, double /*duration*/) {}, format);
        auto header = enc->getHeader();
        (void)header;
    }
    catch (const std::exception&)
    {
        // An unknown codec or bad option makes snapserver reject the stream.
        return 0;
    }

    for (size_t i = 2; i < segments.size(); ++i)
    {
        // Stream readers hand the encoder whole frames only.
        size_t len = segments[i].size() - (segments[i].size() % format.frameSize());
        msg::PcmChunk chunk(format, 0);
        free(chunk.payload);
        chunk.payload = static_cast<char*>(malloc(len == 0 ? 1 : len));
        chunk.payloadSize = static_cast<uint32_t>(len);
        if (len != 0)
            memcpy(chunk.payload, segments[i].data(), len);
        try
        {
            enc->encode(chunk);
        }
        catch (const std::exception&)
        {
        }
    }
    return 0;
}
