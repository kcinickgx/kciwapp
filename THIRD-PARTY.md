# Third-party components

kciwapp itself is GPL-2.0-or-later (see LICENSE). The packages below are
downloaded by the setup / update check and ship next to the client, or are
compiled into the Go core/server.

| Component | Where | License | Source |
|---|---|---|---|
| whatsmeow (Go WhatsApp Web client) | compiled into `core/kciwapp-core.exe` and the server | MPL-2.0 | https://github.com/tulir/whatsmeow |
| go-sql-driver/mysql | server | MPL-2.0 | https://github.com/go-sql-driver/mysql |
| modernc.org/sqlite | core/server | BSD-3-Clause | https://gitlab.com/cznic/sqlite |
| ffmpeg / ffprobe (`core/ffmpeg.exe`, `core/ffprobe.exe`) | run as separate processes | GPL-2.0-or-later (see `core/LICENSE-ffmpeg.txt`) | https://ffmpeg.org / builds from https://www.gyan.dev/ffmpeg/builds/ |
| libmpv and its dependencies (`mpv\`) | loaded by the client for video/audio playback | GPL-2.0-or-later (MSYS2 build) | https://mpv.io / https://packages.msys2.org |
| whisper.cpp (`whisper\whisper-cli.exe`, `whisper.dll`, `ggml*.dll`) | run as a separate process for voice note transcription | MIT | https://github.com/ggerganov/whisper.cpp |
| Whisper large-v3-turbo model (`whisper\ggml-large-v3-turbo.bin`) | downloaded from the official Hugging Face repo, not redistributed here | MIT (OpenAI Whisper weights, converted by ggerganov) | https://huggingface.co/ggerganov/whisper.cpp |
| NVIDIA CUDA runtime (`whisper\cublas64_12.dll`, `cublasLt64_12.dll`, `nvrtc*.dll`) | needed by whisper.cpp on NVIDIA GPUs | NVIDIA CUDA Toolkit EULA (redistributable runtime libraries) | https://docs.nvidia.com/cuda/eula/ |
| WebView2Loader.dll | loads the Edge WebView2 runtime for calls | Microsoft Edge WebView2 license (redistributable) | https://developer.microsoft.com/microsoft-edge/webview2/ |
| Segoe UI / Segoe MDL2 Assets / Segoe UI Emoji | system fonts, not shipped | Windows | — |

WhatsApp is a trademark of Meta Platforms, Inc. kciwapp is an independent,
unofficial client built on whatsmeow; it is not affiliated with or endorsed
by WhatsApp or Meta. Using unofficial clients is against WhatsApp's Terms of
Service and may get an account suspended.
