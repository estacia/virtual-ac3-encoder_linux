#include "dsp.hpp"
#include "iec61937.hpp"
#include "rt_ring.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
}

int main() {
    using namespace vac3;

    std::array<std::uint8_t, 5> payload{0x0B, 0x77, 0x12, 0x34, 0x56};
    std::array<std::uint8_t, kIecBurstBytes> burst{};
    check(make_ac3_burst(payload.data(), payload.size(), burst.data()), "AC-3 burst creation");
    check(burst[0] == 0x72 && burst[1] == 0xF8, "IEC Pa word");
    check(burst[2] == 0x1F && burst[3] == 0x4E, "IEC Pb word");
    check(burst[4] == 0x01 && burst[5] == 0x00, "IEC AC-3 data type");
    check(burst[6] == 40 && burst[7] == 0, "IEC payload bit length");
    check(burst[8] == 0x77 && burst[9] == 0x0B, "IEC payload word byte swap");
    check(burst[10] == 0x34 && burst[11] == 0x12, "IEC second payload word byte swap");
    check(burst[12] == 0 && burst[13] == 0x56, "IEC odd payload byte padding");

    make_pause_burst(burst.data());
    check(burst[4] == 0x03 && burst[5] == 0 && burst[6] == 0 && burst[7] == 0,
          "IEC pause burst header");

    PcmBlock stereo{};
    for (auto& frame : stereo) {
        frame[0] = 0.5f;
        frame[1] = -0.5f;
    }
    DspProcessor matrix(UpmixMode::DynamicTimeDomain);
    matrix.process(stereo);
    check(std::abs(stereo[0][2]) < 0.0001f, "anti-phase centre cancellation");
    check(stereo[0][4] > 0.34f && stereo[0][5] < -0.34f, "difference rear channels");
    check(stereo[0][3] == 0.0f, "anti-phase input cancels LFE feed");
    PcmBlock bass{};
    for (auto& frame : bass) { frame[0] = 0.5f; frame[1] = 0.5f; }
    DspProcessor bassMatrix(UpmixMode::DynamicTimeDomain);
    bassMatrix.process(bass);
    check(bass[0][3] > 0.0f, "low-pass LFE generation");

    PcmBlock native{};
    native[0][0] = 0.1f;
    native[0][1] = 0.2f;
    native[0][2] = 0.3f;
    native[0][3] = 0.4f;
    native[0][4] = 0.5f;
    native[0][5] = 0.6f;
    const auto before = native[0];
    DspProcessor nativeProcessor(UpmixMode::DynamicTimeDomain);
    nativeProcessor.process(native);
    check(native[0] == before, "native 5.1 frame is untouched");

    PcmBlock dual{};
    dual[0][0] = 0.25f;
    dual[0][1] = -0.25f;
    DspProcessor dualProcessor(UpmixMode::DualStereo);
    dualProcessor.process(dual);
    check(dual[0][4] == dual[0][0] && dual[0][5] == dual[0][1], "dual stereo copy");
    check(dual[0][2] == 0.0f && dual[0][3] == 0.0f, "dual stereo centre and LFE muted");

    SpscRing<std::uint32_t, 8> ring;
    const std::array<std::uint32_t, 5> input{1, 2, 3, 4, 5};
    std::array<std::uint32_t, 5> output{};
    check(ring.write(input.data(), input.size()) == input.size(), "SPSC write");
    check(ring.read(output.data(), output.size()) == output.size(), "SPSC read");
    check(output == input, "SPSC data integrity");

    if (failures) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    std::puts("All core tests passed.");
    return 0;
}
