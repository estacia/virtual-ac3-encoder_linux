#include <cstdio>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

int main() {
    const AVCodec* codec = avcodec_find_encoder_by_name("ac3");
    if (!codec) codec = avcodec_find_encoder_by_name("ac3_fixed");
    if (!codec) {
        std::fprintf(stderr, "No AC-3 encoder is present in the linked FFmpeg runtime\n");
        return 1;
    }

    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (!context) return 2;

    context->sample_rate = 48000;
    context->bit_rate = 640000;
    context->time_base = AVRational{1, 48000};
    context->thread_count = 1;
    context->sample_fmt = AV_SAMPLE_FMT_FLTP;
    const AVChannelLayout layout = AV_CHANNEL_LAYOUT_5POINT1_BACK;
    if (av_channel_layout_copy(&context->ch_layout, &layout) < 0) {
        avcodec_free_context(&context);
        return 3;
    }

    int result = avcodec_open2(context, codec, nullptr);
    if (result < 0) {
        std::fprintf(stderr, "AC-3 encoder open failed: %d\n", result);
        avcodec_free_context(&context);
        return 4;
    }

    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    if (!frame || !packet) {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&context);
        return 5;
    }

    frame->format = context->sample_fmt;
    frame->sample_rate = context->sample_rate;
    frame->nb_samples = context->frame_size;
    if (av_channel_layout_copy(&frame->ch_layout, &context->ch_layout) < 0 ||
        av_frame_get_buffer(frame, 0) < 0 ||
        av_samples_set_silence(frame->extended_data, 0, frame->nb_samples,
                               context->ch_layout.nb_channels, context->sample_fmt) < 0) {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&context);
        return 6;
    }

    result = avcodec_send_frame(context, frame);
    if (result >= 0) result = avcodec_receive_packet(context, packet);
    const bool encoded = result >= 0 && packet->size > 0;
    if (encoded) {
        std::printf("AC-3 encoder smoke test passed: codec=%s frame=%d samples packet=%d bytes\n",
                    codec->name, context->frame_size, packet->size);
    } else {
        std::fprintf(stderr, "AC-3 frame encode failed: %d\n", result);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&context);
    return encoded ? 0 : 7;
}
