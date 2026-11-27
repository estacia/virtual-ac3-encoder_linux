# Third-party notices and project provenance

## Windows project lineage

This Linux implementation is an independent native port of the behavior and design goals of
[`estacia/virtual-ac3-encoder_Dual`](https://github.com/estacia/virtual-ac3-encoder_Dual).
The Windows implementation contains MIT-licensed code with the notice:

> Copyright (c) 2026 strepto42

The Linux-specific implementation in this repository was authored separately; it does not
include the Windows WDM/WaveRT driver or WASAPI implementation. The original Windows project
and its notices remain available in its own repository.

## FFmpeg

This project dynamically links the system FFmpeg libraries (`libavcodec` and `libavutil`).
FFmpeg is licensed under LGPL-2.1-or-later or GPL-2.0-or-later depending on build configuration.
No FFmpeg source or binary is bundled here. See <https://ffmpeg.org/legal.html>.

## PipeWire

This project dynamically links the system PipeWire libraries. PipeWire is licensed under the
MIT license. No PipeWire source or binary is bundled here. See <https://pipewire.org/>.

## Linux port scope

The Linux port uses the public PipeWire module API and FFmpeg encoder API. It does not include
code from the separate `bassdr/pipewire-module-spdif-encode` project; that project was consulted
as a public architectural reference only.
