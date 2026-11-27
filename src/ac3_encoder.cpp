#include "ac3_encoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
}

namespace vac3 {

Ac3Encoder::~Ac3Encoder() { close(); }

void Ac3Encoder::close() noexcept {
    if (packet_) av_packet_free(&packet_);
    if (frame_) av_frame_free(&frame_);
    if (context_) avcodec_free_context(&context_);
    codec_ = nullptr;
    nextPts_ = 0;
}

bool Ac3Encoder::try_open(const AVCodec* codec, AVSampleFormat sampleFormat, int bitrate) {
    context_ = avcodec_alloc_context3(codec);
    if (!context_) return false;

    context_->sample_rate = kSampleRate;
    context_->bit_rate = bitrate;
    context_->sample_fmt = sampleFormat;
    context_->time_base = AVRational{1, kSampleRate};
    context_->thread_count = 1;
    av_channel_layout_uninit(&context_->ch_layout);
    const AVChannelLayout inputLayout = AV_CHANNEL_LAYOUT_5POINT1_BACK;
    if (av_channel_layout_copy(&context_->ch_layout, &inputLayout) < 0) {
        close();
        return false;
    }

    int result = avcodec_open2(context_, codec, nullptr);
    if (result < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, error, sizeof(error));
        std::fprintf(stderr, "[vac3] avcodec_open2(%s, %s): %s\n",
                     codec->name, av_get_sample_fmt_name(sampleFormat), error);
        close();
        return false;
    }

    if (context_->frame_size != static_cast<int>(kAc3FrameSamples)) {
        std::fprintf(stderr, "[vac3] unexpected AC-3 frame size: %d\n", context_->frame_size);
        close();
        return false;
    }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !packet_) {
        close();
        return false;
    }

    frame_->format = context_->sample_fmt;
    frame_->sample_rate = kSampleRate;
    frame_->nb_samples = static_cast<int>(kAc3FrameSamples);
    frame_->pts = 0;
    if (av_channel_layout_copy(&frame_->ch_layout, &context_->ch_layout) < 0 ||
        av_frame_get_buffer(frame_, 0) < 0) {
        close();
        return false;
    }
    codec_ = codec;
    return true;
}

bool Ac3Encoder::init(int bitrate) {
    close();
    if (bitrate < 96000 || bitrate > 640000) {
        std::fprintf(stderr, "[vac3] bitrate must be between 96000 and 640000 bps\n");
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name("ac3");
    if (!codec) codec = avcodec_find_encoder_by_name("ac3_fixed");
    if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_AC3);
    if (!codec) {
        std::fprintf(stderr, "[vac3] FFmpeg AC-3 encoder is unavailable\n");
        return false;
    }

    // Prefer float processing. Some distro builds expose only the fixed-point
    // AC-3 encoder, so retain a packed-quality integer fallback.
    if (try_open(codec, AV_SAMPLE_FMT_FLTP, bitrate)) return true;
    if (try_open(codec, AV_SAMPLE_FMT_S16P, bitrate)) return true;
    return false;
}

bool Ac3Encoder::encode(const PcmBlock& pcm, std::uint8_t* burst, std::size_t burstBytes) {
    if (!context_ || !frame_ || !packet_ || !burst) return false;
    if (av_frame_make_writable(frame_) < 0) return false;
    frame_->pts = nextPts_;

    for (std::size_t ch = 0; ch < kChannels; ++ch) {
        if (context_->sample_fmt == AV_SAMPLE_FMT_FLTP) {
            auto* dst = reinterpret_cast<float*>(frame_->extended_data[ch]);
            for (std::size_t i = 0; i < kAc3FrameSamples; ++i) dst[i] = pcm[i][ch];
        } else if (context_->sample_fmt == AV_SAMPLE_FMT_S16P) {
            auto* dst = reinterpret_cast<std::int16_t*>(frame_->extended_data[ch]);
            for (std::size_t i = 0; i < kAc3FrameSamples; ++i) {
                const float sample = std::clamp(pcm[i][ch], -1.0f, 1.0f);
                dst[i] = static_cast<std::int16_t>(std::lrint(sample * 32767.0f));
            }
        } else {
            return false;
        }
    }

    const int sendResult = avcodec_send_frame(context_, frame_);
    if (sendResult < 0) return false;
    const int receiveResult = avcodec_receive_packet(context_, packet_);
    if (receiveResult < 0) return false;

    const bool ok = make_ac3_burst(packet_->data, static_cast<std::size_t>(packet_->size),
                                   burst, burstBytes);
    av_packet_unref(packet_);
    if (ok) nextPts_ += static_cast<std::int64_t>(kAc3FrameSamples);
    return ok;
}

const char* Ac3Encoder::codec_name() const noexcept {
    return codec_ ? codec_->name : "uninitialized";
}

int Ac3Encoder::frame_size() const noexcept {
    return context_ ? context_->frame_size : 0;
}

} // namespace vac3
