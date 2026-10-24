// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_VOICE_SESSION_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_VOICE_SESSION_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"

class Profile;
struct MahoSttSession;

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

// Owns one desktop on-device whisper voice session for chrome://maho-ai.
//
// Lives on the UI sequence but runs the blocking whisper FFI (maho-stt-ffi) on
// a dedicated `base::ThreadPool` sequence. Push-to-talk model: `PushAudio`
// accumulates 16 kHz mono f32 PCM, `Stop` transcribes the buffer. The GGML
// model is downloaded (single-shot) + SHA-256 verified into the profile dir on
// first run. VAD-gated sliding-window partials are out of scope for this pass.
class MahoAiVoiceSession {
 public:
  using ReadyCallback = base::OnceCallback<void(bool accepted)>;
  using TranscriptCallback = base::RepeatingCallback<void(const std::string&)>;
  using ErrorCallback = base::RepeatingCallback<void(const std::string&)>;

  MahoAiVoiceSession(
      Profile* profile,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      TranscriptCallback on_final,
      ErrorCallback on_error);
  MahoAiVoiceSession(const MahoAiVoiceSession&) = delete;
  MahoAiVoiceSession& operator=(const MahoAiVoiceSession&) = delete;
  ~MahoAiVoiceSession();

  // Ensures the model is present (downloading if needed), opens the engine, and
  // runs |on_ready| with whether the session is ready to receive audio. A
  // Start() that arrives while model setup is still running does not restart
  // setup: it becomes the pending request (newest wins; a superseded request
  // is answered with false) and is served when setup finishes.
  void Start(ReadyCallback on_ready);

  // Appends a chunk of 16 kHz mono f32 PCM. No-op until the engine is ready.
  void PushAudio(const std::vector<float>& pcm16k);

  // Finalizes the utterance: transcribes the accumulated buffer and forwards
  // the result via the final/error callbacks. Exactly one terminal callback
  // (final or error) follows unless a newer Start() is accepted first, in which
  // case this utterance's result is dropped so it cannot leak into the new
  // session. During model setup it cancels the pending request (answered with
  // false) while setup itself keeps running for the next Start().
  void Stop();

 private:
  enum class ModelState { kMissing, kReady, kCorrupt };

  static ModelState CheckModelOnWorker(base::FilePath model_path);
  static bool InstallAndVerifyOnWorker(base::FilePath temp_path,
                                       base::FilePath dest_path);
  static MahoSttSession* CreateEngineOnWorker(base::FilePath model_path);
  static std::optional<std::string> TranscribeOnWorker(uintptr_t engine_token,
                                                       std::vector<float> pcm);

  void OnModelChecked(ModelState state);
  void StartDownload();
  void OnDownloaded(base::FilePath temp_path);
  void OnDownloadVerified(bool ok);
  void CreateEngine();
  void OnEngineCreated(MahoSttSession* engine);
  // Ends model setup with |error| and rejects the pending request, if any.
  void FailSetup(const std::string& error);
  void OnTranscribed(uint64_t session_seq,
                     std::optional<std::string> transcript);

  base::FilePath model_path_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  TranscriptCallback on_final_;
  ErrorCallback on_error_;
  scoped_refptr<base::SequencedTaskRunner> stt_runner_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  std::vector<float> pcm_;
  // Owned; created/used/freed on |stt_runner_|. `maho_stt_session_cancel` is
  // thread-safe and may be invoked from this (UI) sequence during teardown.
  // RAW_PTR_EXCLUSION: points to Rust-heap (non-PartitionAlloc) memory owned by
  // maho-stt-ffi and handed across sequences; not a raw_ptr candidate.
  RAW_PTR_EXCLUSION MahoSttSession* engine_ = nullptr;
  bool active_ = false;
  // Incremented by every accepted Start(). Stop() binds the current value into
  // its transcription reply; replies from an older session are discarded.
  uint64_t session_seq_ = 0;
  // True while the first Start() is checking/downloading the model or creating
  // the engine, so a second Start() cannot race it.
  bool starting_ = false;
  // The request served when setup finishes; empty if it was cancelled by
  // Stop(). Only the newest Start() during setup is kept.
  ReadyCallback pending_ready_;

  base::WeakPtrFactory<MahoAiVoiceSession> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_VOICE_SESSION_H_
