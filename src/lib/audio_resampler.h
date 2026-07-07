#pragma once

#include <memory>
#include <string>

// 前向声明，避免在头文件中暴露 FFmpeg 细节
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwrContext;

namespace JVNote
{

// ---- 自定义删除器（实现在 .cpp 中） ----
struct ResamplerAVFormatInputDeleter
{
  void operator()(AVFormatContext* p) const;
};

struct ResamplerAVFormatOutputDeleter
{
  void operator()(AVFormatContext* p) const;
};

struct ResamplerAVCodecContextDeleter
{
  void operator()(AVCodecContext* p) const;
};

struct ResamplerSwrContextDeleter
{
  void operator()(SwrContext* p) const;
};

// ---- unique_ptr 类型别名 ----
using ResamplerInputCtxPtr =
  std::unique_ptr<AVFormatContext, ResamplerAVFormatInputDeleter>;

using ResamplerOutputCtxPtr =
  std::unique_ptr<AVFormatContext, ResamplerAVFormatOutputDeleter>;

using ResamplerCodecCtxPtr =
  std::unique_ptr<AVCodecContext, ResamplerAVCodecContextDeleter>;

using ResamplerSwrCtxPtr =
  std::unique_ptr<SwrContext, ResamplerSwrContextDeleter>;

/**
 * @brief 音频重采样器，将音频文件转为指定采样率和声道数。
 *
 * 输出固定为 PCM S16LE WAV 格式。
 *
 * 用法：
 * @code
 *   AudioResampler resampler;
 *   resampler.resample("input.wav", "output.wav", 16000, 1);
 * @endcode
 *
 * 或一步完成：
 * @code
 *   resample_audio("input.wav", "output.wav");
 * @endcode
 */
class AudioResampler
{
public:
  AudioResampler() = default;
  ~AudioResampler() = default;

  // 禁止拷贝，允许移动
  AudioResampler(const AudioResampler&) = delete;
  AudioResampler& operator=(const AudioResampler&) = delete;
  AudioResampler(AudioResampler&&) noexcept = default;
  AudioResampler& operator=(AudioResampler&&) noexcept = default;

  /**
   * @brief 对音频文件重采样并写入新文件。
   * @param input_file   输入音频文件路径。
   * @param output_file  输出 WAV 文件路径。
   * @param target_rate  目标采样率（默认 16000）。
   * @param target_ch    目标声道数（默认 1）。
   * @return 成功返回 true。
   */
  bool resample(const std::string& input_file,
                const std::string& output_file,
                int target_rate = 16000,
                int target_ch = 1);

  /**
   * @brief 关闭所有资源，允许复用。
   */
  void close();

private:
  // ---- 内部步骤 ----
  bool open_input(const std::string& input_file);
  bool open_output(const std::string& output_file,
                   int target_rate,
                   int target_ch);
  bool process();

  bool setup_decoder();
  bool setup_output_stream(int target_rate, int target_ch);
  bool setup_resampler(int target_rate, int target_ch);
  bool write_output_header();

  void flush_encoder();

  // FFmpeg 资源（unique_ptr 自动管理生命周期）
  ResamplerInputCtxPtr  m_up_in_fmt_ctx;
  ResamplerOutputCtxPtr m_up_out_fmt_ctx;
  ResamplerCodecCtxPtr  m_up_dec_ctx;
  ResamplerCodecCtxPtr  m_up_enc_ctx;
  ResamplerSwrCtxPtr    m_up_swr_ctx;

  // 借用的指针（生命周期由上面的 context 管理）
  AVStream* m_p_in_audio_stream = nullptr;
  AVStream* m_p_out_stream      = nullptr;
  int       m_audio_stream_index = -1;

  // 累积已写入编码器的采样数，用于生成正确的 PTS
  int64_t   m_samples_written = 0;
};

/**
 * @brief 便捷函数：一步完成音频重采样（16kHz 单声道 PCM S16LE）。
 */
inline bool resample_audio(const std::string& input_file,
                            const std::string& output_file)
{
  AudioResampler r;
  return r.resample(input_file, output_file);
}

} // namespace JVNote
