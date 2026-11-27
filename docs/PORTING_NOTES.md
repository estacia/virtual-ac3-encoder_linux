# Porting notes and verification checklist

## Windows components deliberately replaced

- WASAPI capture/render and `IAudioClient3` are replaced by PipeWire streams.
- WDM WaveRT virtual endpoint is replaced by a PipeWire 5.1 virtual sink.
- COM endpoint enumeration is replaced by PipeWire node targeting (`target.object`).
- DirectX 11 Control Center is deferred until the native engine is validated.

## Audio path

1. PipeWire presents a six-channel 48 kHz planar-float virtual sink.
2. The RT callback copies frames into a fixed SPSC ring and signals the worker semaphore.
3. The worker waits for one 1,536-sample AC-3 frame plus the selected safe-frame cushion on startup.
4. The worker applies the selected DSP mode, encodes a 640 kbit/s AC-3 frame, and packs the result into a 6,144-byte IEC 61937 burst.
5. A four-slot SPSC burst queue hands complete bursts to the PipeWire playback callback.
6. Before the first encoded frame the playback callback emits zero carrier data; after playback begins it emits complete IEC 61937 pause bursts during underruns, preserving the 1,536-frame AC-3 cadence.
7. The playback stream negotiates `SPA_MEDIA_SUBTYPE_iec958` with `SPA_AUDIO_IEC958_CODEC_AC3` rather than ordinary raw PCM, and uses `PW_STREAM_FLAG_NO_CONVERT`.

## Before a stable release

- Build and run CI on CachyOS/Arch with the current PipeWire and FFmpeg packages.
- Confirm the installed module is discovered by PipeWire.
- Verify the ALSA HDMI node accepts the IEC 61937 carrier format, not merely PCM stereo.
- Verify AC-3 lock on the Samsung TV and WiiM Ultra eARC path.
- Run a 60-minute 5.1 test with games, movies, stereo music, and source switching.
- Measure input-to-output latency using a loopback capture or electrical/optical capture; do not infer it from PipeWire quantum alone.
- Stress-test SPSC overflow, burst underrun, device suspend/resume, and HDMI hotplug.
- Only then add GUI, VU meters, live gain control, and a settings daemon.
