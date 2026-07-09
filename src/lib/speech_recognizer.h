#pragma once

#include <functional>
#include <memory>
#include <string>

namespace JVNote
{

/**
 * @brief 分片转写回调。
 * @param chunk_index  当前片段索引（从 0 开始）
 * @param total_chunks 总片段数
 * @param text         当前片段识别出的文字
 */
using ChunkCallback = std::function<void(
  int chunk_index, int total_chunks, const std::string& text)>;

/**
 * @brief 语音识别器配置。
 *
 * 支持 SenseVoice、Whisper 等多种模型，通过 model_type 指定。
 */
struct SpeechRecognizerConfig
{
  /** SenseVoice 模型文件路径（model.int8.onnx）。 */
  std::string model_path;
  /** Token 文件路径（tokens.txt）。 */
  std::string tokens_path;
  /** 推理线程数。 */
  int32_t num_threads = 1;
  /** 语言提示，"auto" 表示自动检测，"zh"、"en" 等。 */
  std::string language = "auto";
  /** 是否启用逆文本正则化（ITN），将数字等转为可读形式。 */
  bool use_itn = true;
  /** 模型类型，"sense_voice" 或 "whisper" 等。 */
  std::string model_type = "sense_voice";
  /**
   * @brief 分段时长（秒）。音频超过此长度时自动分片转写，避免 OOM。
   *        设为 0 表示不分片，一次性处理。
   */
  int32_t chunk_duration_sec = 30;
};

/**
 * @brief 离线语音识别器，将 WAV 音频文件转写为文字。
 *
 * 用法：
 * @code
 *   SpeechRecognizer sr;
 *   SpeechRecognizerConfig cfg;
 *   cfg.model_path = "/path/to/model.int8.onnx";
 *   cfg.tokens_path = "/path/to/tokens.txt";
 *   if (sr.init(cfg)) {
 *       std::string text = sr.transcribe_file("audio.wav");
 *   }
 * @endcode
 */
class SpeechRecognizer
{
public:
  SpeechRecognizer();
  ~SpeechRecognizer();

  // 禁止拷贝，允许移动
  SpeechRecognizer(const SpeechRecognizer&) = delete;
  SpeechRecognizer& operator=(const SpeechRecognizer&) = delete;
  SpeechRecognizer(SpeechRecognizer&&) noexcept;
  SpeechRecognizer& operator=(SpeechRecognizer&&) noexcept;

  /**
   * @brief 初始化识别器，加载模型。
   * @return 成功返回 true。
   */
  bool init(const SpeechRecognizerConfig& cfg);

  /**
   * @brief 转写单个 WAV 音频文件。
   * @param wav_path WAV 文件路径（单声道，采样率与模型匹配）。
   * @return 识别出的文字，失败返回空字符串。
   */
  std::string transcribe_file(const std::string& wav_path);

  /**
   * @brief 转写单个 WAV 音频文件（流式回调版本）。
   *
   * 当音频需要分段处理时，每完成一个片段就调用一次 @p on_chunk，
   * 方便实时显示进度和中间结果。短音频一次性处理时也会调用一次。
   *
   * @param wav_path  WAV 文件路径。
   * @param on_chunk  每完成一个片段的回调。
   * @return 完整拼接后的识别文字，失败返回空字符串。
   */
  std::string transcribe_file(const std::string& wav_path,
                               ChunkCallback on_chunk);

  /**
   * @brief 关闭识别器，释放模型资源。
   */
  void close();

private:
  struct Impl;
  std::unique_ptr<Impl> m_up_impl;
};

} // namespace JVNote
