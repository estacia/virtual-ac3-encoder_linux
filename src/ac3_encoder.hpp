#pragma once

#include "dsp.hpp"
#include "iec61937.hpp"

#include <array>
#include <cstdint>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

namespace vac3 {

class Ac3Encoder {
public:
    Ac3Encoder() = default;
    ~Ac3Encoder();
    Ac3Encoder(const Ac3Encoder&) = delete;
    Ac3Encoder& operator=(const Ac3Encoder&) = delete;

    bool init(int bitrate);
    bool encode(const PcmBlock& pcm, std::uint8_t* burst, std::size_t burstBytes);
    const char* codec_name() const noexcept;
    int frame_size() const noexcept;

private:
    bool try_open(const AVCodec* codec, AVSampleFormat sampleFormat, int bitrate);
    void close() noexcept;

    const AVCodec* codec_ = nullptr;
    AVCodecContext* context_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    std::int64_t nextPts_ = 0;
};

} // namespace vac3
