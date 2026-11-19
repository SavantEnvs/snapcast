// Fuzz one snapclient audio decoder (client/decoder/*): the server sends a CodecHeader (setHeader) and
// then WireChunks that the client decodes in place (decode). The codec is fixed at compile time by
// SNAP_DECODER (OpusDecoder, PcmDecoder). FlacDecoder and OggDecoder have no target: both spin forever
// on a truncated codec header (upstream hangs, recorded in the integration PR).
// Input: segments (fuzz_common.hpp): the first is the codec header payload, every further one a chunk.
#include "client/decoder/decoder.hpp"
#include "client/decoder/flac_decoder.hpp"
#include "client/decoder/ogg_decoder.hpp"
#include "client/decoder/opus_decoder.hpp"
#include "client/decoder/pcm_decoder.hpp"
#include "common/message/codec_header.hpp"
#include "common/message/pcm_chunk.hpp"
#include "fuzz_common.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <new>

#ifndef SNAP_DECODER
#error "define SNAP_DECODER"
#endif
#ifndef SNAP_CODEC
#error "define SNAP_CODEC"
#endif

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto segments = fuzz::split_segments(data, size);
    if (segments.empty())
        return -1;

    auto header = std::make_unique<msg::CodecHeader>(SNAP_CODEC, static_cast<uint32_t>(segments[0].size()));
    if (!segments[0].empty())
        memcpy(header->payload, segments[0].data(), segments[0].size());

    auto decoder = std::make_unique<decoder::SNAP_DECODER>();
    SampleFormat format;
    try
    {
        // snapclient's Controller drops the stream when setHeader throws.
        format = decoder->setHeader(header.get());
    }
    catch (const std::exception&)
    {
        return 0;
    }

    for (size_t i = 1; i < segments.size(); ++i)
    {
        msg::PcmChunk chunk;
        chunk.format = format;
        chunk.payloadSize = static_cast<uint32_t>(segments[i].size());
        chunk.payload = static_cast<char*>(malloc(segments[i].size()));
        if (!segments[i].empty())
            memcpy(chunk.payload, segments[i].data(), segments[i].size());
        try
        {
            if (!decoder->decode(&chunk))
                continue;
        }
        catch (const std::exception&)
        {
        }
    }
    return 0;
}
