#include "speech_recognizer.hpp"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <utility>

#include "log.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"

namespace
{

/**
 * @brief 检查文件是否存在且非空，失败时输出错误信息。
 * @return 文件有效返回 true。
 */
bool check_file(const std::string& path, const char* label)
{
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    JVNote::Log().error("%s not found: %s", label, path.c_str());
    return false;
  }
  if (f.tellg() == 0) {
    JVNote::Log().error("%s is empty: %s", label, path.c_str());
    return false;
  }
  return true;
}

} // namespace

namespace JVNote
{

// ============================================================
// PIMPL 实现 — OfflineRecognizer 不可默认构造，用 unique_ptr
// ============================================================

struct SpeechRecognizer::Impl
{
  using OfflineRec = sherpa_onnx::cxx::OfflineRecognizer;

  std::unique_ptr<OfflineRec> up_recognizer;
  int32_t chunk_duration_sec = 30;
};

// ============================================================
// 构造 / 析构
// ============================================================

SpeechRecognizer::SpeechRecognizer() = default;
SpeechRecognizer::~SpeechRecognizer() {}

SpeechRecognizer::SpeechRecognizer(SpeechRecognizer&&) noexcept = default;
SpeechRecognizer& SpeechRecognizer::operator=(SpeechRecognizer&&) noexcept =
    default;

// ============================================================
// init
// ============================================================

bool SpeechRecognizer::init(const SpeechRecognizerConfig& cfg)
{
  bool ok = false;
  do {
    if (!m_up_impl) {
      m_up_impl = std::make_unique<Impl>();
    }

    m_up_impl->chunk_duration_sec = cfg.chunk_duration_sec;

    // 预检：模型和词表文件必须存在且非空
    if (!check_file(cfg.model_path, "Model")) break;
    if (!check_file(cfg.tokens_path, "Tokens")) break;

    sherpa_onnx::cxx::OfflineRecognizerConfig sherpa_cfg;
    sherpa_cfg.model_config.num_threads = cfg.num_threads;
    sherpa_cfg.model_config.tokens = cfg.tokens_path;
    sherpa_cfg.model_config.provider = cfg.provider;

    if (cfg.model_type == "sense_voice") {
      sherpa_cfg.model_config.sense_voice.model = cfg.model_path;
      sherpa_cfg.model_config.sense_voice.language = cfg.language;
      sherpa_cfg.model_config.sense_voice.use_itn = cfg.use_itn;
    } else {
      Log().error("Unsupported model_type: %s", cfg.model_type.c_str());
      break;
    }

    std::unique_ptr<Impl::OfflineRec> up_sr;
    try {
      up_sr = std::make_unique<Impl::OfflineRec>(
          sherpa_onnx::cxx::OfflineRecognizer::Create(sherpa_cfg));
    } catch (const std::exception& e) {
      Log().error("Failed to create recognizer: %s", e.what());
      break;
    }
    if (!up_sr->Get()) {
      Log().error("Failed to create OfflineRecognizer.");
      break;
    }

    m_up_impl->up_recognizer = std::move(up_sr);
    ok = true;
  } while (false);
  return ok;
}

// ============================================================
// transcribe_file (无回调版本，委托给回调版本)
// ============================================================

std::string SpeechRecognizer::transcribe_file(const std::string& wav_path)
{
  return transcribe_file(wav_path, nullptr);
}

// ============================================================
// transcribe_file (回调版本)
// ============================================================

std::string SpeechRecognizer::transcribe_file(const std::string& wav_path,
                                               ChunkCallback on_chunk)
{
  std::string result;
  do {
    if (!m_up_impl || !m_up_impl->up_recognizer) {
      Log().error("Recognizer not initialized.");
      break;
    }

    auto wave = sherpa_onnx::cxx::ReadWave(wav_path);
    if (wave.samples.empty()) {
      Log().error("Failed to read WAV file: %s", wav_path.c_str());
      break;
    }

    int32_t chunk_sec = m_up_impl->chunk_duration_sec;
    int64_t total_samples = static_cast<int64_t>(wave.samples.size());
    int64_t chunk_samples = chunk_sec > 0
      ? static_cast<int64_t>(chunk_sec) * wave.sample_rate
      : total_samples;

    if (chunk_samples >= total_samples) {
      // 音频较短，一次性处理
      auto stream = m_up_impl->up_recognizer->CreateStream();
      if (!stream.Get()) {
        Log().error("Failed to create stream.");
        break;
      }
      stream.AcceptWaveform(wave.sample_rate, wave.samples.data(),
                            static_cast<int32_t>(wave.samples.size()));
      m_up_impl->up_recognizer->Decode(&stream);
      auto rec_result = m_up_impl->up_recognizer->GetResult(&stream);
      result = std::move(rec_result.text);

      if (on_chunk) on_chunk(0, 1, result);
    } else {
      // 长音频，分片处理
      int total_chunks = static_cast<int>(
        (total_samples + chunk_samples - 1) / chunk_samples);
      Log().print("Audio duration ~%llds, splitting into %d chunks.",
                  (long long)(total_samples / wave.sample_rate),
                  total_chunks);

      for (int i = 0; i < total_chunks; ++i) {
        int64_t start = static_cast<int64_t>(i) * chunk_samples;
        int64_t count = chunk_samples;
        if (start + count > total_samples) {
          count = total_samples - start;
        }

        auto stream = m_up_impl->up_recognizer->CreateStream();
        if (!stream.Get()) {
          Log().error("Failed to create stream for chunk %d.", i);
          break;
        }

        stream.AcceptWaveform(
          wave.sample_rate,
          wave.samples.data() + start,
          static_cast<int32_t>(count));

        m_up_impl->up_recognizer->Decode(&stream);
        auto rec_result = m_up_impl->up_recognizer->GetResult(&stream);

        if (!rec_result.text.empty()) {
          if (!result.empty()) result += " ";
          result += rec_result.text;
        }

        if (on_chunk) {
          on_chunk(i, total_chunks,
                   rec_result.text.empty() ? "" : rec_result.text);
        }
      }

      Log().print("Chunked transcription done.");
    }
  } while (false);
  return result;
}

// ============================================================
// close
// ============================================================

void SpeechRecognizer::close()
{
  m_up_impl.reset();
}

} // namespace JVNote
