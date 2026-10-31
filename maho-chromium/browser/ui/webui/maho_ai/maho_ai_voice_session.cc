// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_voice_session.h"

#include <array>
#include <cstdint>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "crypto/secure_hash.h"
#include "maho/third_party/maho/maho_stt_ffi.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "url/gurl.h"

namespace {

// GGML small English q5_1 whisper model (~180 MB), MIT-licensed weights.
// Pinned to an immutable repo revision so the digest below cannot drift; the
// digest is the upstream LFS object id (sha256) of that revision's file.
constexpr char kModelUrl[] =
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/"
    "5359861c739e955e79d9a303bcbc70fb988958b1/ggml-small.en-q5_1.bin";
constexpr char kModelFileName[] = "ggml-small.en-q5_1.bin";
constexpr char kModelSubdir[] = "MahoVoiceModels";
constexpr char kModelSha256[] =
    "bfdff4894dcb76bbf647d56263ea2a96645423f1669176f4844a1bf8e478ad30";

// Generous single-utterance transcript buffers (push-to-talk).
constexpr size_t kInitialTranscriptCap = 8192;
constexpr size_t kMaxTranscriptCap = 128 * 1024;

bool VerifySha256(const base::FilePath& path, std::string_view expected_hex) {
  if (expected_hex.empty()) {
    LOG(WARNING) << "maho-voice: model SHA-256 unset; skipping verification";
    return true;
  }
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    return false;
  }
  std::unique_ptr<crypto::SecureHash> hasher =
      crypto::SecureHash::Create(crypto::SecureHash::SHA256);
  std::array<uint8_t, 4096> buffer{};
  while (true) {
    std::optional<size_t> bytes_read =
        file.ReadAtCurrentPos(base::span<uint8_t>(buffer));
    if (!bytes_read.has_value()) {
      return false;
    }
    if (*bytes_read == 0) {
      break;
    }
    hasher->Update(base::span<const uint8_t>(buffer).first(*bytes_read));
  }
  std::array<uint8_t, 32> hash{};
  hasher->Finish(base::span<uint8_t>(hash));
  const std::string hex = base::HexEncode(base::span<const uint8_t>(hash));
  return base::EqualsCaseInsensitiveASCII(hex, expected_hex);
}

// Bound args are passed as an opaque uintptr_t token: MahoSttSession is an
// incomplete FFI type, and base::Bind's unretained check static_asserts on a
// raw pointer to an incomplete type (clang-cl is strict; libc++ on mac lets it
// pass). Casting to/from uintptr_t keeps the handle opaque past that check.
void FreeEngineOnWorker(uintptr_t engine_token) {
  maho_stt_session_free(reinterpret_cast<MahoSttSession*>(engine_token));
}

}  // namespace

MahoAiVoiceSession::MahoAiVoiceSession(
    Profile* profile,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    TranscriptCallback on_final,
    ErrorCallback on_error)
    : url_loader_factory_(std::move(url_loader_factory)),
      on_final_(std::move(on_final)),
      on_error_(std::move(on_error)),
      stt_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE})) {
  model_path_ = profile->GetPath()
                    .AppendASCII(kModelSubdir)
                    .AppendASCII(kModelFileName);
}

MahoAiVoiceSession::~MahoAiVoiceSession() {
  if (engine_) {
    // Abort any in-flight decode (thread-safe atomic), then free on the worker
    // sequence after the transcribe task drains.
    maho_stt_session_cancel(engine_);
    stt_runner_->PostTask(
        FROM_HERE, base::BindOnce(&FreeEngineOnWorker,
                                  reinterpret_cast<uintptr_t>(engine_)));
    engine_ = nullptr;
  }
}

void MahoAiVoiceSession::Start(ReadyCallback on_ready) {
  if (active_ || engine_) {
    std::move(on_ready).Run(false);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&MahoAiVoiceSession::CheckModelOnWorker, model_path_),
      base::BindOnce(&MahoAiVoiceSession::OnModelChecked,
                     weak_factory_.GetWeakPtr(), std::move(on_ready)));
}

void MahoAiVoiceSession::PushAudio(const std::vector<float>& pcm16k) {
  if (!active_) {
    return;
  }
  pcm_.insert(pcm_.end(), pcm16k.begin(), pcm16k.end());
}

void MahoAiVoiceSession::Stop() {
  if (!active_ || !engine_) {
    return;
  }
  active_ = false;
  std::vector<float> pcm = std::move(pcm_);
  pcm_.clear();
  stt_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&MahoAiVoiceSession::TranscribeOnWorker,
                     reinterpret_cast<uintptr_t>(engine_), std::move(pcm)),
      base::BindOnce(&MahoAiVoiceSession::OnTranscribed,
                     weak_factory_.GetWeakPtr()));
}

// static
MahoAiVoiceSession::ModelState MahoAiVoiceSession::CheckModelOnWorker(
    base::FilePath model_path) {
  if (!base::PathExists(model_path)) {
    return ModelState::kMissing;
  }
  if (!VerifySha256(model_path, kModelSha256)) {
    return ModelState::kCorrupt;
  }
  return ModelState::kReady;
}

// static
bool MahoAiVoiceSession::InstallAndVerifyOnWorker(base::FilePath temp_path,
                                                  base::FilePath dest_path) {
  base::File::Error error = base::File::FILE_OK;
  if (!base::CreateDirectoryAndGetError(dest_path.DirName(), &error)) {
    return false;
  }
  if (!base::Move(temp_path, dest_path)) {
    return false;
  }
  return VerifySha256(dest_path, kModelSha256);
}

// static
MahoSttSession* MahoAiVoiceSession::CreateEngineOnWorker(
    base::FilePath model_path) {
  const std::string utf8_path = model_path.AsUTF8Unsafe();
  return maho_stt_session_create(utf8_path.c_str());
}

// static
std::optional<std::string> MahoAiVoiceSession::TranscribeOnWorker(
    uintptr_t engine_token,
    std::vector<float> pcm) {
  auto* engine = reinterpret_cast<MahoSttSession*>(engine_token);
  if (!engine) {
    return std::nullopt;
  }
  std::string out(kInitialTranscriptCap, '\0');
  int written = maho_stt_session_transcribe(engine, pcm.data(), pcm.size(),
                                            out.data(), out.size());
  if (written == MAHO_STT_ERR_BUFFER_TOO_SMALL) {
    out.assign(kMaxTranscriptCap, '\0');
    written = maho_stt_session_transcribe(engine, pcm.data(), pcm.size(),
                                          out.data(), out.size());
  }
  if (written < 0) {
    return std::nullopt;
  }
  out.resize(static_cast<size_t>(written));
  return out;
}

void MahoAiVoiceSession::OnModelChecked(ReadyCallback on_ready,
                                        ModelState state) {
  switch (state) {
    case ModelState::kReady:
      CreateEngine(std::move(on_ready));
      return;
    case ModelState::kMissing:
    case ModelState::kCorrupt:
      StartDownload(std::move(on_ready));
      return;
  }
}

void MahoAiVoiceSession::StartDownload(ReadyCallback on_ready) {
  if (!url_loader_factory_) {
    on_error_.Run("Voice model unavailable");
    std::move(on_ready).Run(false);
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(kModelUrl);
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("maho_stt_model_download", R"(
        semantics {
          sender: "Maho AI Voice Input"
          description:
            "Downloads the on-device Whisper speech-to-text model on first use "
            "of voice input in the Maho AI panel."
          trigger: "User starts a voice input session for the first time."
          data: "None. A static model file is fetched."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "Disabled by not using voice input."
          policy_exception_justification: "Not implemented."
        })");

  loader_ = network::SimpleURLLoader::Create(std::move(request),
                                             traffic_annotation);
  // TODO(maho-voice): make the download resumable (Range requests + partial
  // file cache). This is a single-shot fetch for the first pass.
  loader_->DownloadToTempFile(
      url_loader_factory_.get(),
      base::BindOnce(&MahoAiVoiceSession::OnDownloaded,
                     weak_factory_.GetWeakPtr(), std::move(on_ready)));
}

void MahoAiVoiceSession::OnDownloaded(ReadyCallback on_ready,
                                      base::FilePath temp_path) {
  loader_.reset();
  if (temp_path.empty()) {
    on_error_.Run("Voice model download failed");
    std::move(on_ready).Run(false);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&MahoAiVoiceSession::InstallAndVerifyOnWorker, temp_path,
                     model_path_),
      base::BindOnce(&MahoAiVoiceSession::OnDownloadVerified,
                     weak_factory_.GetWeakPtr(), std::move(on_ready)));
}

void MahoAiVoiceSession::OnDownloadVerified(ReadyCallback on_ready, bool ok) {
  if (!ok) {
    on_error_.Run("Voice model verification failed");
    std::move(on_ready).Run(false);
    return;
  }
  CreateEngine(std::move(on_ready));
}

void MahoAiVoiceSession::CreateEngine(ReadyCallback on_ready) {
  stt_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&MahoAiVoiceSession::CreateEngineOnWorker, model_path_),
      base::BindOnce(&MahoAiVoiceSession::OnEngineCreated,
                     weak_factory_.GetWeakPtr(), std::move(on_ready)));
}

void MahoAiVoiceSession::OnEngineCreated(ReadyCallback on_ready,
                                         MahoSttSession* engine) {
  engine_ = engine;
  active_ = engine != nullptr;
  if (!active_) {
    on_error_.Run("Voice engine failed to initialize");
  }
  std::move(on_ready).Run(active_);
}

void MahoAiVoiceSession::OnTranscribed(std::optional<std::string> transcript) {
  if (!transcript.has_value()) {
    on_error_.Run("Transcription failed");
    return;
  }
  if (transcript->empty()) {
    on_error_.Run("Didn't catch that");
    return;
  }
  on_final_.Run(*transcript);
}
