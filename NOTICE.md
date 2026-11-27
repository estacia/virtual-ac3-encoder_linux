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

The source build links against the FFmpeg libraries supplied by the build system. The portable alpha2 release bundles a minimal shared FFmpeg 9.1 runtime containing libavcodec and libavutil with only the AC-3 encoders enabled. It is built without GPL components and without x86 assembly, and is distributed under LGPL-2.1-or-later. The release includes the matching FFmpeg 9.1 source archive and the LGPL license text. See FFmpeg-BUNDLE-NOTICE.txt and the release assets.

## PipeWire

The module dynamically links to the host PipeWire daemon's libpipewire-0.3 ABI. PipeWire itself is deliberately not bundled: a PipeWire module must be loaded into the already-running host daemon, and loading a second private PipeWire copy into that process is not ABI-safe. PipeWire remains an external runtime dependency and retains its MIT license. See <https://pipewire.org/>.


## Linux port scope

The Linux port uses the public PipeWire module API and FFmpeg encoder API. It does not include
code from the separate `bassdr/pipewire-module-spdif-encode` project; that project was consulted
as a public architectural reference only.
