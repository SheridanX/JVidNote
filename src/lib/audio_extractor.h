#pragma once

#include <memory>
#include <string>

// 前向声明，避免在头文件中暴露 FFmpeg 细节
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwrContext;
struct AVAudioFifo;

namespace JVNote
{

// ---- 自定义删除器（实现在 .cpp 中） ----
struct AVFormatInputDeleter  { void operator()(AVFormatContext* p) const; };
struct AVFormatOutputDeleter { void operator()(AVFormatContext* p) const; };
struct AVCodecContextDeleter { void operator()(AVCodecContext* p) const; };
struct SwrContextDeleter     { void operator()(SwrContext* p)     const; };
struct AudioFifoDeleter      { void operator()(AVAudioFifo* p)    const; };

// ---- unique_ptr 类型别名 ----
using InputCtxPtr  = std::unique_ptr<AVFormatContext, AVFormatInputDeleter>;
using OutputCtxPtr = std::unique_ptr<AVFormatContext, AVFormatOutputDeleter>;
using CodecCtxPtr  = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using SwrCtxPtr    = std::unique_ptr<SwrContext,     SwrContextDeleter>;
using AudioFifoPtr = std::unique_ptr<AVAudioFifo,    AudioFifoDeleter>;

/**
 * @brief 音频提取器，将视频文件中的音轨提取为独立音频文件。
 *
 * 用法：
 * @code
 *   AudioExtractor ex;
 *   if (ex.open_input("video.mp4") && ex.open_output("audio.mp3")) {
 *       ex.extract();
 *   }
 * @endcode
 *
 * 或一步完成：
 * @code
 *   extract_audio("video.mp4", "audio.mp3");
 * @endcode
 */
class AudioExtractor {
public:
    AudioExtractor() = default;
    ~AudioExtractor() = default;

    // 禁止拷贝，允许移动
    AudioExtractor(const AudioExtractor&) = delete;
    AudioExtractor& operator=(const AudioExtractor&) = delete;
    AudioExtractor(AudioExtractor&&) noexcept = default;
    AudioExtractor& operator=(AudioExtractor&&) noexcept = default;

    /**
     * @brief 打开输入视频文件并定位音频流
     * @return 成功返回 true
     */
    bool open_input(const std::string& input_file);

    /**
     * @brief 创建输出文件及编码器
     * @return 成功返回 true
     */
    bool open_output(const std::string& output_file);

    /**
     * @brief 执行提取（必须先调用 openInput 和 openOutput）
     * @return 成功返回 true
     */
    bool extract();

    /**
     * @brief 关闭所有资源，允许复用提取器处理新文件
     */
    void close();

private:
    // ---- 内部步骤 ----
    bool find_audio_stream();
    bool setup_decoder();
    bool setup_output_stream();
    bool setup_resampler();
    bool write_output_header();

    void process_encoded_packet(struct AVPacket* pkt);
    void process_stream_copy_packet(struct AVPacket* pkt);
    void flush_encoder();
    void write_trailer();

    // FFmpeg 资源（unique_ptr 自动管理生命周期）
    InputCtxPtr  m_up_in_fmt_ctx;
    OutputCtxPtr m_up_out_fmt_ctx;
    CodecCtxPtr  m_up_dec_ctx;
    CodecCtxPtr  m_up_enc_ctx;
    SwrCtxPtr    m_up_swr_ctx;
    AudioFifoPtr m_up_audio_fifo;

    // 借用的指针（生命周期由上面的 context 管理）
    AVStream* m_p_in_audio_stream    = nullptr;
    AVStream* m_p_out_stream        = nullptr;
    int       m_audio_stream_index  = -1;

    // 累积已写入编码器的采样数，用于生成正确的 PTS
    int64_t   m_samples_written     = 0;
};

/**
 * @brief 便捷函数：一步完成音频提取
 */
inline bool extract_audio(const std::string& input_file,
                           const std::string& output_file) {
    AudioExtractor ex;
    return ex.open_input(input_file)
        && ex.open_output(output_file)
        && ex.extract();
}

} // namespace JVNote
