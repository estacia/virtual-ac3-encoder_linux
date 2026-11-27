#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vac3 {

inline constexpr std::size_t kChannels = 6;
inline constexpr std::size_t kAc3FrameSamples = 1536;
inline constexpr int kSampleRate = 48000;
using AudioFrame = std::array<float, kChannels>;
using PcmBlock = std::array<AudioFrame, kAc3FrameSamples>;

enum class UpmixMode {
    Untouched,
    DynamicTimeDomain,
    DualStereo,
};

inline UpmixMode parse_upmix(std::string_view value) {
    if (value == "untouched" || value == "off") return UpmixMode::Untouched;
    if (value == "dual-stereo" || value == "dual_stereo") return UpmixMode::DualStereo;
    return UpmixMode::DynamicTimeDomain;
}

class DspProcessor {
public:
    explicit DspProcessor(UpmixMode mode = UpmixMode::DynamicTimeDomain) : mode_(mode) {}

    void process(PcmBlock& block) noexcept {
        bool discrete = false;
        for (const auto& sample : block) {
            for (std::size_t ch = 2; ch < kChannels; ++ch) {
                if (std::abs(sample[ch]) > kDiscreteThreshold) {
                    discrete = true;
                    break;
                }
            }
            if (discrete) break;
        }

        if (discrete) {
            nativeHoldFrames_ = kNativeHoldFrames;
        } else if (nativeHoldFrames_ > kAc3FrameSamples) {
            nativeHoldFrames_ -= static_cast<std::uint32_t>(kAc3FrameSamples);
        } else {
            nativeHoldFrames_ = 0;
        }

        // Never modify actual multichannel programme material. A three-second
        // hold prevents brief quiet passages in a movie/game from toggling modes.
        if (discrete || nativeHoldFrames_ > 0 || mode_ == UpmixMode::Untouched) return;

        if (mode_ == UpmixMode::DualStereo) {
            for (auto& s : block) {
                s[2] = 0.0f;
                s[3] = 0.0f;
                s[4] = s[0];
                s[5] = s[1];
            }
            return;
        }

        const float alpha = 1.0f - std::exp(-2.0f * kPi * kLfeCutoffHz / kSampleRate);
        for (auto& s : block) {
            const float left = s[0];
            const float right = s[1];
            const float mid = 0.5f * (left + right);
            const float side = 0.5f * (left - right);
            s[2] = mid * kMinus3dB;
            s[4] = side * kMinus3dB;
            s[5] = -side * kMinus3dB;
            lfeState_ += alpha * (mid - lfeState_);
            s[3] = lfeState_ * kLfeProgramGain;
        }
    }

    void reset() noexcept {
        nativeHoldFrames_ = 0;
        lfeState_ = 0.0f;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kDiscreteThreshold = 0.0005011872f; // -66 dBFS
    static constexpr float kMinus3dB = 0.70710678f;
    static constexpr float kLfeProgramGain = 0.31622777f; // -10 dB programme calibration
    static constexpr float kLfeCutoffHz = 120.0f;
    static constexpr std::uint32_t kNativeHoldFrames = 3u * kSampleRate;

    UpmixMode mode_;
    std::uint32_t nativeHoldFrames_ = 0;
    float lfeState_ = 0.0f;
};

} // namespace vac3
