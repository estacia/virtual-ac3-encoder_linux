# Virtual AC-3 Encoder for Linux

Linux-native continuation of `estacia/virtual-ac3-encoder_Dual`, intended for CachyOS and other Arch-based distributions using PipeWire.

> **Status: experimental alpha.** The first implementation contains a native PipeWire module, a real-time AC-3/IEC 61937 path, a 5.1 virtual sink, and core unit tests. It has not yet been compiled or exercised on a physical CachyOS HDMI/eARC chain. Do not treat this alpha as a validated replacement for the Windows build.

## Design goals

- Dolby Digital / AC-3 at 48 kHz, up to 640 kbit/s.
- 5.1 PCM virtual sink (`FL`, `FR`, `FC`, `LFE`, `RL`, `RR`).
- IEC 61937 framing to stereo S16LE for HDMI or S/PDIF hardware paths.
- Native multichannel material is passed through without DSP modification.
- Dynamic time-domain stereo-to-5.1 matrix (no FFT or surround FIFO).
- Dual Stereo Copy (front L/R copied to rear L/R; centre and LFE muted).
- Fixed-capacity SPSC audio and burst queues; no locks or heap allocation in PipeWire process callbacks.
- EXTREME latency preset starts with 128 safe frames (2.67 ms at 48 kHz) beyond the required AC-3 frame accumulation.

The AC-3 codec still operates on 1,536-sample frames. At 48 kHz that is 32 ms of input audio; the Linux port cannot remove this codec framing requirement. The initial target is to eliminate unnecessary additional buffering while keeping the output stable.

## Current implementation

| Component | Implementation |
|---|---|
| Virtual input endpoint | PipeWire module stream, 6-channel F32P |
| AC-3 encoder | FFmpeg `libavcodec` (`ac3`, fallback `ac3_fixed`) |
| Surround processing | Native C++ time-domain DSP |
| IEC 61937 | Explicit AC-3 burst packer |
| Input queue | Lock-free SPSC, 32,768 sample frames |
| Encoded output | Four-slot burst queue; one pending burst targeted |
| Output endpoint | PipeWire IEC958 AC-3 stream carrying IEC 61937 bursts |
| GUI / telemetry | Not included in alpha; CLI/module properties first |
| Kernel driver | Not needed; PipeWire provides the virtual sink |

## Build on CachyOS

Install the build dependencies:

```bash
sudo pacman -S --needed base-devel git meson ninja pkgconf pipewire ffmpeg
```

Build and run the core tests:

```bash
meson setup build --buildtype=release
meson compile -C build
meson test -C build --print-errorlogs
```

Install the module and example PipeWire configuration:

```bash
sudo meson install -C build
```

The configuration is installed under the system PipeWire configuration directory. Review it before enabling the module. For a user-only configuration, copy it into your home directory instead:

```bash
mkdir -p ~/.config/pipewire/pipewire.conf.d
cp config/50-virtual-ac3-encoder.conf \
  ~/.config/pipewire/pipewire.conf.d/50-virtual-ac3-encoder.conf
```

If the system-wide configuration was installed, avoid enabling a duplicate user copy.

Restart the user audio services:

```bash
systemctl --user restart pipewire pipewire-pulse wireplumber
```

Inspect the devices:

```bash
wpctl status
pw-cli ls Node | grep -A3 -B2 -i 'virtual-ac3\|AC-3'
```

The virtual device should appear as **Virtual AC-3 Encoder (5.1)**. Select it as the playback device for games, movies, or Moonlight. The module negotiates `SPA_MEDIA_SUBTYPE_iec958` with the `AC3` codec and requests `PW_STREAM_FLAG_NO_CONVERT` to prevent PipeWire from resampling or remixing the encoded carrier. Its output stream is attached to the default PipeWire output unless `target.object` is set.

## Configuration

Edit `config/50-virtual-ac3-encoder.conf` before enabling the module:

| Property | Default | Meaning |
|---|---:|---|
| `bitrate` | `640000` | AC-3 bitrate in bits/s (96,000–640,000) |
| `latency` | `extreme` | `extreme`, `fast`, `balanced`, `safe`, `relaxed` |
| `quantum` | `128` | PipeWire node-latency hint in frames; clamped to 64–1024 (not a server-wide quantum override) |
| `safe.frames` | preset | Optional explicit pre-roll cushion (32–2048 frames) |
| `upmix` | `dynamic-time-domain` | `dynamic-time-domain`, `dual-stereo`, or `untouched` |
| `target.object` | default | Exact PipeWire node name for the HDMI/S/PDIF target |

Preset safe-frame values at 48 kHz:

| Preset | Safe frames | Time |
|---|---:|---:|
| Extreme | 128 | 2.67 ms |
| Fast | 384 | 8.00 ms |
| Balanced | 512 | 10.67 ms |
| Safe | 768 | 16.00 ms |
| Relaxed | 1024 | 21.33 ms |

`dynamic-time-domain` detects discrete channel activity and preserves native 5.1. Stereo content is matrixed using a centre signal, difference-derived rear channels, and a 120 Hz one-pole LFE with -10 dB programme calibration. Native multichannel detection uses a -66 dBFS threshold and a three-second hold to avoid mode flutter. `dual-stereo` copies FL to RL and FR to RR for stereo sources. `untouched` performs no upmixing.

## HDMI / eARC notes

For a PC → Samsung TV → eARC → WiiM Ultra chain, `target.object` must identify the PC's HDMI audio sink corresponding to the TV. Enable Dolby Digital / bitstream passthrough on the TV where applicable. The GPU driver, HDMI sink profile, TV EDID/ELD, and PipeWire/ALSA path must all preserve IEC 61937 data. The module requests PipeWire's IEC958 AC-3 format; this does not prove that the physical HDMI path accepts AC-3. The actual ALSA/HDMI/TV path must be verified on the target hardware.

Do not route unrelated PCM streams directly to the same HDMI output while the encoded stream is active. Volume, resampling, channel remixing, or software mixing of an IEC 61937 carrier corrupts the bitstream. The module disables remixing and locks the output volume to unity, but the selected hardware route still needs testing. The `quantum` setting is a node-latency request only; actual graph quantum is controlled by the PipeWire server and session manager.

## Diagnostics and current limitations

- The module currently exposes no GUI, tray icon, channel meters, or live per-channel trims.
- The only encoder is AC-3; DTS is intentionally out of scope for this first Linux milestone.
- The alpha uses a native time-domain matrix, not the Windows FFmpeg `surround` filter / FFT path.
- The worker thread requests `SCHED_FIFO` on a best-effort basis. Without appropriate realtime permissions/rtkit configuration it falls back to normal scheduling.
- The first playback burst is delayed until one AC-3 frame plus the configured safe-frame cushion has been captured.
- Before the first encoded frame, the output callback emits zero carrier data rather than a 32 ms pause burst, avoiding an unnecessary startup wait. After playback starts, complete IEC 61937 pause bursts maintain AC-3 frame cadence during underruns. Repeated underruns indicate the quantum, scheduling, output device, or CPU configuration needs adjustment.
- The implementation does not yet include adaptive input/output clock-drift correction. Long-duration stability must be tested before treating the alpha as production-ready.
- No physical latency measurement has been completed. The 128-frame cushion is a design target, not a measured end-to-end latency result.

## Publishing as an independent GitHub repository

This source tree is intentionally standalone and has no upstream remote configured. To push updates to the already-created `estacia/virtual-ac3-encoder_linux` repository from a machine with Git and GitHub CLI authenticated:

```bash
./scripts/push-to-github.sh
```

The script configures `origin` to this repository if needed and pushes the current `main` branch. It does not add the Windows repository as an upstream and does not configure synchronization.

## License and provenance

MIT. See `LICENSE` and `NOTICE.md`. FFmpeg and PipeWire remain external system dependencies and retain their respective licenses.
