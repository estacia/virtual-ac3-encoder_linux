#include "ac3_encoder.hpp"
#include "dsp.hpp"
#include "iec61937.hpp"
#include "rt_ring.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <string>
#include <thread>

extern "C" {
#include <pipewire/pipewire.h>
#include <pipewire/impl-module.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
}

namespace {
using namespace vac3;
constexpr std::size_t kInputRingFrames = 32768;
constexpr std::size_t kBurstSlots = 4;
constexpr std::uint32_t kMinSafeFrames = 32;
constexpr std::uint32_t kMaxSafeFrames = 2048;

std::uint32_t latency_safe_frames(const char* value) {
    const std::string mode = value ? value : "extreme";
    if (mode == "fast") return 384;
    if (mode == "balanced") return 512;
    if (mode == "safe") return 768;
    if (mode == "relaxed") return 1024;
    return 128; // Extreme
}

struct ModuleData {
    pw_impl_module* module = nullptr;
    pw_context* context = nullptr;
    pw_stream* capture = nullptr;
    pw_stream* playback = nullptr;
    spa_hook moduleListener{};

    SpscRing<AudioFrame, kInputRingFrames> inputRing;
    BurstRing<kIecBurstBytes, kBurstSlots> outputRing;
    sem_t workSignal{};
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::atomic<std::uint64_t> inputDropped{0};
    std::atomic<std::uint64_t> outputUnderruns{0};
    std::atomic<std::uint64_t> encodedFrames{0};

    Ac3Encoder encoder;
    DspProcessor dsp;
    std::uint32_t safeFrames = 128;
    std::uint32_t quantum = 128;
    int bitrate = 640000;
    bool preRollComplete = false;
    bool everSentAc3 = false; // output callback thread only

    PcmBlock pcm{};
    std::array<AudioFrame, 8192> captureScratch{};
    std::array<std::uint8_t, kIecBurstBytes> encodedBurst{};
    std::array<std::uint8_t, kIecBurstBytes> currentBurst{};
    std::array<std::uint8_t, kIecBurstBytes> pauseBurst{};
    std::size_t currentOffset = kIecBurstBytes;
    std::size_t pauseOffset = 0;

    ModuleData(std::uint32_t safe, std::uint32_t q, int br, UpmixMode mode)
        : safeFrames(std::clamp(safe, kMinSafeFrames, kMaxSafeFrames)),
          quantum(std::clamp(q, 64u, 1024u)), bitrate(br), dsp(mode) {
        sem_init(&workSignal, 0, 0);
        make_pause_burst(pauseBurst.data());
    }

    ~ModuleData() {
        // Disconnect first so no further RT callbacks can reference this object.
        if (capture) pw_stream_disconnect(capture);
        if (playback) pw_stream_disconnect(playback);
        stopping.store(true, std::memory_order_release);
        sem_post(&workSignal);
        if (worker.joinable()) worker.join();
        if (capture) pw_stream_destroy(capture);
        if (playback) pw_stream_destroy(playback);
        sem_destroy(&workSignal);
    }

    void signal_worker() noexcept { sem_post(&workSignal); }
};

void set_worker_realtime() {
    pthread_setname_np(pthread_self(), "vac3-ac3-encoder");
    sched_param param{};
    param.sched_priority = 10;
    // Best effort only. PipeWire/rtkit may grant realtime scheduling separately.
    (void)pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
}

bool encode_next(ModuleData* data) {
    const std::size_t available = data->inputRing.size();
    const std::size_t required = kAc3FrameSamples + (data->preRollComplete ? 0 : data->safeFrames);
    if (available < required || data->outputRing.size() >= 1) return false;

    if (data->inputRing.read(data->pcm.data(), kAc3FrameSamples) != kAc3FrameSamples)
        return false;

    data->dsp.process(data->pcm);
    if (!data->encoder.encode(data->pcm, data->encodedBurst.data(), data->encodedBurst.size())) {
        pw_log_error("spdif-encode: AC-3 frame encoding failed");
        return false;
    }
    if (!data->outputRing.push(data->encodedBurst.data())) return false;
    data->preRollComplete = true;
    data->encodedFrames.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void worker_main(ModuleData* data) {
    set_worker_realtime();
    while (!data->stopping.load(std::memory_order_acquire)) {
        while (sem_wait(&data->workSignal) < 0 && errno == EINTR) {}
        if (data->stopping.load(std::memory_order_acquire)) break;

        // One encoded burst is kept pending at most. With the default 128-frame
        // cushion the next AC-3 packet is prepared about 2.67 ms before the
        // currently transmitted 32 ms burst completes.
        while (!data->stopping.load(std::memory_order_relaxed) && encode_next(data)) {}
    }
}

void on_capture_process(void* userData) {
    auto* data = static_cast<ModuleData*>(userData);
    pw_buffer* buffer = pw_stream_dequeue_buffer(data->capture);
    if (!buffer) return;

    spa_buffer* spaBuffer = buffer->buffer;
    if (spaBuffer && spaBuffer->n_datas >= kChannels && spaBuffer->datas[0].chunk) {
        const std::size_t samples = spaBuffer->datas[0].chunk->size / sizeof(float);
        std::array<const float*, kChannels> channel{};
        bool valid = true;
        for (std::size_t ch = 0; ch < kChannels; ++ch) {
            const auto& plane = spaBuffer->datas[ch];
            if (!plane.data || !plane.chunk) { valid = false; break; }
            channel[ch] = reinterpret_cast<const float*>(
                static_cast<const std::uint8_t*>(plane.data) + plane.chunk->offset);
        }
        if (valid && samples > 0) {
            // Copy the planar PipeWire block into preallocated interleaved scratch,
            // then publish it with one SPSC head update (not one atomic per sample).
            const std::size_t count = std::min({samples, data->inputRing.free_space(), data->captureScratch.size()});
            for (std::size_t i = 0; i < count; ++i) {
                for (std::size_t ch = 0; ch < kChannels; ++ch)
                    data->captureScratch[i][ch] = channel[ch][i];
            }
            const std::size_t written = data->inputRing.write(data->captureScratch.data(), count);
            if (written < samples)
                data->inputDropped.fetch_add(samples - written, std::memory_order_relaxed);
            data->signal_worker();
        }
    }

    pw_stream_queue_buffer(data->capture, buffer);
}

void on_playback_process(void* userData) {
    auto* data = static_cast<ModuleData*>(userData);
    pw_buffer* buffer = pw_stream_dequeue_buffer(data->playback);
    if (!buffer) return;

    spa_buffer* spaBuffer = buffer->buffer;
    if (!spaBuffer || spaBuffer->n_datas < 1 || !spaBuffer->datas[0].data || !spaBuffer->datas[0].chunk) {
        pw_stream_queue_buffer(data->playback, buffer);
        return;
    }

    spa_data& plane = spaBuffer->datas[0];
    const std::size_t maxFrames = plane.maxsize / 4;
    const std::size_t frames = buffer->requested
        ? std::min<std::size_t>(maxFrames, static_cast<std::size_t>(buffer->requested))
        : maxFrames;
    const std::size_t bytes = frames * 4;
    auto* output = static_cast<std::uint8_t*>(plane.data);

    std::size_t done = 0;
    while (done < bytes) {
        if (data->currentOffset >= kIecBurstBytes) {
            // IEC 61937 AC-3 bursts are aligned to 1536 carrier frames. Once
            // playback has started, finish any pause burst before starting a
            // new AC-3 burst; pre-empting a pause mid-frame would break cadence.
            const bool atBurstBoundary = !data->everSentAc3 || data->pauseOffset == 0;
            if (atBurstBoundary && data->outputRing.pop(data->currentBurst.data())) {
                data->currentOffset = 0;
                data->everSentAc3 = true;
            }
        }

        if (data->currentOffset < kIecBurstBytes) {
            const std::size_t n = std::min(bytes - done, kIecBurstBytes - data->currentOffset);
            std::memcpy(output + done, data->currentBurst.data() + data->currentOffset, n);
            data->currentOffset += n;
            done += n;
        } else if (!data->everSentAc3) {
            // Before the first AC-3 packet, keep the carrier silent rather than
            // outputting a full 32 ms pause burst that would add startup latency.
            std::memset(output + done, 0, bytes - done);
            data->outputUnderruns.fetch_add(1, std::memory_order_relaxed);
            done = bytes;
        } else {
            // After starting AC-3, use a standards-compliant pause burst during
            // gaps. A queued AC-3 burst starts only after this full pause burst ends.
            const std::size_t n = std::min(bytes - done, kIecBurstBytes - data->pauseOffset);
            std::memcpy(output + done, data->pauseBurst.data() + data->pauseOffset, n);
            data->pauseOffset += n;
            done += n;
            if (data->pauseOffset == kIecBurstBytes) data->pauseOffset = 0;
            data->outputUnderruns.fetch_add(1, std::memory_order_relaxed);
        }
    }

    *plane.chunk = spa_chunk{.offset = 0, .size = static_cast<std::uint32_t>(bytes), .stride = 4, .flags = 0};
    pw_stream_queue_buffer(data->playback, buffer);
    data->signal_worker();
}

void on_stream_state_changed(void* userData, enum pw_stream_state oldState,
                             enum pw_stream_state state, const char* error) {
    (void)userData;
    (void)oldState;
    if (state == PW_STREAM_STATE_ERROR) {
        pw_log_error("spdif-encode: PipeWire stream error: %s",
                     error ? error : "unknown error (check HDMI/S/PDIF IEC958 AC-3 support)");
    } else if (state == PW_STREAM_STATE_STREAMING) {
        pw_log_info("spdif-encode: PipeWire stream is streaming");
    }
}

void force_unit_volume(pw_stream* stream) {
    const float volumes[2] = {1.0f, 1.0f};
    std::uint8_t storage[256]{};
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
    spa_pod_frame frame{};
    spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    spa_pod_builder_prop(&builder, SPA_PROP_channelVolumes, 0);
    spa_pod_builder_array(&builder, sizeof(float), SPA_TYPE_Float, 2, volumes);
    spa_pod_builder_prop(&builder, SPA_PROP_softVolumes, 0);
    spa_pod_builder_array(&builder, sizeof(float), SPA_TYPE_Float, 2, volumes);
    auto* param = static_cast<spa_pod*>(spa_pod_builder_pop(&builder, &frame));
    if (param) pw_stream_set_param(stream, SPA_PARAM_Props, param);
}

const pw_stream_events captureEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .destroy = nullptr,
    .state_changed = on_stream_state_changed,
    .control_info = nullptr,
    .io_changed = nullptr,
    .param_changed = nullptr,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = on_capture_process,
    .drained = nullptr,
    .command = nullptr,
};

const pw_stream_events playbackEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .destroy = nullptr,
    .state_changed = on_stream_state_changed,
    .control_info = nullptr,
    .io_changed = nullptr,
    .param_changed = nullptr,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = on_playback_process,
    .drained = nullptr,
    .command = nullptr,
};

void module_destroy(void* userData) {
    auto* data = static_cast<ModuleData*>(userData);
    spa_hook_remove(&data->moduleListener);
    delete data;
}

const pw_impl_module_events moduleEvents = {
    .version = PW_VERSION_IMPL_MODULE_EVENTS,
    .destroy = module_destroy,
    .free = nullptr,
    .initialized = nullptr,
    .registered = nullptr,
};

} // namespace

extern "C" SPA_EXPORT int pipewire__module_init(pw_impl_module* module, const char* args) {
    pw_properties* properties = args ? pw_properties_new_string(args) : pw_properties_new(nullptr, nullptr);
    if (!properties) return -1;

    const char* upmixArg = pw_properties_get(properties, "upmix");
    const auto upmix = vac3::parse_upmix(upmixArg ? upmixArg : "dynamic-time-domain");
    const auto safeFrames = latency_safe_frames(pw_properties_get(properties, "latency"));
    const char* safeArg = pw_properties_get(properties, "safe.frames");
    const auto customSafe = safeArg ? static_cast<std::uint32_t>(std::strtoul(safeArg, nullptr, 10)) : safeFrames;
    const char* quantumArg = pw_properties_get(properties, "quantum");
    const auto quantum = quantumArg ? static_cast<std::uint32_t>(std::strtoul(quantumArg, nullptr, 10)) : 128u;
    const char* bitrateArg = pw_properties_get(properties, "bitrate");
    const int bitrate = bitrateArg ? std::atoi(bitrateArg) : 640000;

    auto* data = new ModuleData(customSafe, quantum, bitrate, upmix);
    data->module = module;
    data->context = pw_impl_module_get_context(module);

    if (!data->encoder.init(bitrate)) {
        pw_log_error("spdif-encode: failed to initialize FFmpeg AC-3 encoder");
        delete data;
        pw_properties_free(properties);
        return -1;
    }

    pw_impl_module_add_listener(module, &data->moduleListener, &moduleEvents, data);

    char latency[64]{};
    std::snprintf(latency, sizeof(latency), "%u/%u", data->quantum, kSampleRate);

    auto* captureProps = pw_properties_new(
        PW_KEY_NODE_NAME, "virtual-ac3-5_1",
        PW_KEY_NODE_DESCRIPTION, "Virtual AC-3 Encoder (5.1)",
        PW_KEY_MEDIA_CLASS, "Audio/Sink",
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_NODE_LATENCY, latency,
        "audio.channels", "6",
        nullptr);
    data->capture = pw_stream_new_simple(
        pw_context_get_main_loop(data->context), "virtual-ac3-capture",
        captureProps, &captureEvents, data);
    if (!data->capture) {
        pw_log_error("spdif-encode: could not create virtual capture stream");
        spa_hook_remove(&data->moduleListener);
        delete data;
        pw_properties_free(properties);
        return -1;
    }

    std::uint8_t captureStorage[1024]{};
    spa_pod_builder captureBuilder = SPA_POD_BUILDER_INIT(captureStorage, sizeof(captureStorage));
    spa_audio_info_raw captureInfo{};
    captureInfo.format = SPA_AUDIO_FORMAT_F32P;
    captureInfo.rate = kSampleRate;
    captureInfo.channels = 6;
    captureInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
    captureInfo.position[1] = SPA_AUDIO_CHANNEL_FR;
    captureInfo.position[2] = SPA_AUDIO_CHANNEL_FC;
    captureInfo.position[3] = SPA_AUDIO_CHANNEL_LFE;
    captureInfo.position[4] = SPA_AUDIO_CHANNEL_RL;
    captureInfo.position[5] = SPA_AUDIO_CHANNEL_RR;
    const spa_pod* captureParams[] = {
        spa_format_audio_raw_build(&captureBuilder, SPA_PARAM_EnumFormat, &captureInfo)
    };
    const auto flags = static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
        PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);
    int result = pw_stream_connect(data->capture, PW_DIRECTION_INPUT, PW_ID_ANY,
                                   flags, captureParams, 1);
    if (result < 0) {
        pw_log_error("spdif-encode: virtual capture stream connect failed: %s", spa_strerror(result));
        spa_hook_remove(&data->moduleListener);
        delete data;
        pw_properties_free(properties);
        return -1;
    }

    const char* target = pw_properties_get(properties, "target.object");
    auto* playbackProps = pw_properties_new(
        PW_KEY_NODE_NAME, "virtual-ac3-hdmi-output",
        PW_KEY_NODE_DESCRIPTION, "AC-3 IEC61937 Output",
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_CLASS, "Stream/Output/Audio",
        PW_KEY_NODE_LATENCY, latency,
        PW_KEY_NODE_RATE, "1/48000",
        "stream.dont-remix", "true",
        "channelmix.disable", "true",
        "dither.noise", "0",
        nullptr);
    if (target && target[0]) pw_properties_set(playbackProps, PW_KEY_TARGET_OBJECT, target);

    data->playback = pw_stream_new_simple(
        pw_context_get_main_loop(data->context), "virtual-ac3-playback",
        playbackProps, &playbackEvents, data);
    if (!data->playback) {
        pw_log_error("spdif-encode: could not create output stream");
        spa_hook_remove(&data->moduleListener);
        delete data;
        pw_properties_free(properties);
        return -1;
    }

    std::uint8_t playbackStorage[1024]{};
    spa_pod_builder playbackBuilder = SPA_POD_BUILDER_INIT(playbackStorage, sizeof(playbackStorage));
    // Negotiate an encoded IEC958 stream with the AC-3 codec explicitly.
    // The payload remains IEC 61937 bursts carried over the 48 kHz stereo link;
    // the format metadata tells PipeWire/ALSA not to treat it as ordinary PCM.
    spa_audio_info_iec958 playbackInfo{};
    playbackInfo.codec = SPA_AUDIO_IEC958_CODEC_AC3;
    playbackInfo.flags = 0;
    playbackInfo.rate = kSampleRate;
    const spa_pod* playbackParams[] = {
        spa_format_audio_iec958_build(&playbackBuilder, SPA_PARAM_EnumFormat, &playbackInfo)
    };
    const auto playbackFlags = static_cast<pw_stream_flags>(flags | PW_STREAM_FLAG_NO_CONVERT);
    result = pw_stream_connect(data->playback, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                               playbackFlags, playbackParams, 1);
    if (result < 0) {
        pw_log_error("spdif-encode: output stream connect failed: %s", spa_strerror(result));
        spa_hook_remove(&data->moduleListener);
        delete data;
        pw_properties_free(properties);
        return -1;
    }
    force_unit_volume(data->playback);

    data->worker = std::thread(worker_main, data);
    pw_log_info("spdif-encode: loaded (%s, %d bps, 48 kHz, 5.1, safe=%u frames, quantum=%u)",
                data->encoder.codec_name(), bitrate, data->safeFrames, data->quantum);

    pw_properties_free(properties);
    return 0;
}
