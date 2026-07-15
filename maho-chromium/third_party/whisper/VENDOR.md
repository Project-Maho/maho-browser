# Vendored: whisper.cpp (CPU-only subset)

**Upstream:** https://github.com/ggml-org/whisper.cpp
**Version:** v1.9.1
**Commit:** f049fff95a089aa9969deb009cdd4892b3e74916
**License:** MIT (see ./LICENSE and /LICENSES/whisper.cpp.txt, /LICENSES/ggml.txt)
**Vendored:** 2026-07-14

## Purpose
On-device Whisper STT for the desktop `chrome://maho-ai` voice-input feature.
Compiled as a GN `static_library`/`source_set` target (see ./BUILD.gn) using
Chromium's toolchain/libc++, per plan decision G3 in
`.omo/plans/cross-platform-voice-desktop-2026-07-14.md`. The Rust `maho-stt`
crate wraps only whisper's `extern "C"` header; the C++ compile is owned by GN.

## Included (CPU-only)
- `include/whisper.h` — public C API
- `src/whisper.cpp`, `src/whisper-arch.h` — whisper core
- `ggml/include/*.h` — ggml public headers
- `ggml/src/*.{c,cpp,h}` — ggml core (alloc, backend, quants, threading, opt, gguf)
- `ggml/src/ggml-cpu/` — CPU backend (SIMD dispatch, ops, quants, repack)
- `LICENSE`

## Excluded (not needed for CPU-only desktop STT)
- All GPU/accelerator backends: ggml-cuda, ggml-metal, ggml-vulkan, ggml-sycl,
  ggml-cann, ggml-hip, ggml-musa, ggml-opencl, ggml-hexagon, ggml-webgpu,
  ggml-zdnn, ggml-zendnn, ggml-blas, ggml-rpc, ggml-virtgpu, ggml-openvino
- `src/coreml/`, `src/openvino/` (macro-guarded encoder backends — WHISPER_USE_* undefined)
- `src/parakeet*`, `include/parakeet.h` (separate ASR model, unused by whisper)
- CMake/Make build files, examples, bindings, tests, models, samples, media, docs, CI

## Re-vendoring
```
git clone --depth 1 --branch v1.9.1 https://github.com/ggml-org/whisper.cpp
```
Copy the "Included" set above. Do NOT define GGML_USE_CUDA/METAL/VULKAN/etc.
or WHISPER_USE_COREML/OPENVINO in the GN config (keeps it CPU-only, avoids
pulling excluded backends and their system deps).
