extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include "audio_resampler.h"
#include "log.h"

#include <cstring>

namespace JVNote
{

// ============================================================
// 自定义删除器实现
// ============================================================

void ResamplerAVFormatInputDeleter::operator()(AVFormatContext* p) const
{
  if (p) avformat_close_input(&p);
}

void ResamplerAVFormatOutputDeleter::operator()(AVFormatContext* p) const
{
  if (p) {
    if (!(p->oformat->flags & AVFMT_NOFILE) && p->pb) {
      avio_closep(&p->pb);
    }
    avformat_free_context(p);
  }
}

void ResamplerAVCodecContextDeleter::operator()(AVCodecContext* p) const
{
  if (p) avcodec_free_context(&p);
}

void ResamplerSwrContextDeleter::operator()(SwrContext* p) const
{
  if (p) swr_free(&p);
}

// ============================================================
// close()
// ============================================================

void AudioResampler::close()
{
  m_up_swr_ctx.reset();
  m_up_enc_ctx.reset();
  m_up_dec_ctx.reset();
  m_up_out_fmt_ctx.reset();
  m_up_in_fmt_ctx.reset();
  m_p_in_audio_stream  = nullptr;
  m_p_out_stream       = nullptr;
  m_audio_stream_index = -1;
  m_samples_written    = 0;
}

// ============================================================
// resample()
// ============================================================

bool AudioResampler::resample(const std::string& input_file,
                               const std::string& output_file,
                               int target_rate,
                               int target_ch)
{
  bool ret = false;
  close();
  do {
    if (!open_input(input_file)) break;
    if (!open_output(output_file, target_rate, target_ch)) break;
    if (!process()) break;
    ret = true;
  } while (false);
  return ret;
}

// ============================================================
// open_input()
// ============================================================

bool AudioResampler::open_input(const std::string& input_file)
{
  bool ret = false;
  do {
    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, input_file.c_str(), nullptr, nullptr) < 0) {
      Log{}.error("Cannot open input file: %s", input_file.c_str());
      break;
    }
    m_up_in_fmt_ctx.reset(raw);

    if (avformat_find_stream_info(m_up_in_fmt_ctx.get(), nullptr) < 0) {
      Log{}.error("Cannot find stream info.");
      break;
    }

    // 查找第一个音频流
    m_audio_stream_index =
      av_find_best_stream(m_up_in_fmt_ctx.get(), AVMEDIA_TYPE_AUDIO,
                          -1, -1, nullptr, 0);
    if (m_audio_stream_index < 0) {
      Log{}.error("No audio stream found in input file.");
      break;
    }
    m_p_in_audio_stream =
      m_up_in_fmt_ctx->streams[m_audio_stream_index];

    ret = setup_decoder();
  } while (false);
  return ret;
}

// ============================================================
// setup_decoder()
// ============================================================

bool AudioResampler::setup_decoder()
{
  bool ret = false;
  do {
    const AVCodec* codec =
      avcodec_find_decoder(m_p_in_audio_stream->codecpar->codec_id);
    if (!codec) {
      Log{}.error("Cannot find decoder for audio stream.");
      break;
    }

    AVCodecContext* raw = avcodec_alloc_context3(codec);
    if (!raw) {
      Log{}.error("Cannot allocate decoder context.");
      break;
    }
    m_up_dec_ctx.reset(raw);

    if (avcodec_parameters_to_context(m_up_dec_ctx.get(),
        m_p_in_audio_stream->codecpar) < 0) {
      Log{}.error("Cannot copy codec parameters.");
      break;
    }

    if (avcodec_open2(m_up_dec_ctx.get(), codec, nullptr) < 0) {
      Log{}.error("Cannot open decoder.");
      break;
    }
    ret = true;
  } while (false);
  return ret;
}

// ============================================================
// open_output()
// ============================================================

bool AudioResampler::open_output(const std::string& output_file,
                                  int target_rate,
                                  int target_ch)
{
  bool ret = false;
  do {
    if (!m_up_in_fmt_ctx || !m_up_dec_ctx) {
      Log{}.error("Must call open_input() first.");
      break;
    }

    // 显式指定 WAV 容器
    AVFormatContext* raw = nullptr;
    if (avformat_alloc_output_context2(&raw, nullptr, "wav",
                                       output_file.c_str()) < 0) {
      Log{}.error("Cannot create WAV output context.");
      break;
    }
    m_up_out_fmt_ctx.reset(raw);

    if (!setup_output_stream(target_rate, target_ch)) break;
    if (!write_output_header()) break;
    if (!setup_resampler(target_rate, target_ch)) break;

    ret = true;
  } while (false);
  return ret;
}

// ============================================================
// setup_output_stream()
// ============================================================

bool AudioResampler::setup_output_stream(int target_rate, int target_ch)
{
  bool ret = false;
  do {
    // 创建输出流
    m_p_out_stream =
      avformat_new_stream(m_up_out_fmt_ctx.get(), nullptr);
    if (!m_p_out_stream) {
      Log{}.error("Cannot create output stream.");
      break;
    }

    // 固定 PCM S16LE 编码器
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!codec) {
      Log{}.error("Cannot find PCM S16LE encoder.");
      break;
    }

    AVCodecContext* raw = avcodec_alloc_context3(codec);
    if (!raw) {
      Log{}.error("Cannot allocate encoder context.");
      break;
    }
    m_up_enc_ctx.reset(raw);

    m_up_enc_ctx->sample_rate = target_rate;
    m_up_enc_ctx->sample_fmt  = AV_SAMPLE_FMT_S16;
    m_up_enc_ctx->time_base   = AVRational{1, target_rate};

    av_channel_layout_default(&m_up_enc_ctx->ch_layout, target_ch);

    if (avcodec_open2(m_up_enc_ctx.get(), codec, nullptr) < 0) {
      Log{}.error("Cannot open PCM encoder.");
      m_up_enc_ctx.reset();
      break;
    }

    avcodec_parameters_from_context(m_p_out_stream->codecpar,
                                    m_up_enc_ctx.get());
    m_p_out_stream->time_base = m_up_enc_ctx->time_base;

    ret = true;
  } while (false);
  return ret;
}

// ============================================================
// setup_resampler()
// ============================================================

bool AudioResampler::setup_resampler(int target_rate, int target_ch)
{
  bool ret = false;
  do {
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, target_ch);

    SwrContext* raw = nullptr;
    int rc = swr_alloc_set_opts2(
      &raw,
      &out_ch_layout, AV_SAMPLE_FMT_S16, target_rate,
      &m_up_dec_ctx->ch_layout, m_up_dec_ctx->sample_fmt,
      m_up_dec_ctx->sample_rate,
      0, nullptr);
    if (rc < 0 || swr_init(raw) < 0) {
      Log{}.error("Cannot initialize resampler.");
      break;
    }
    m_up_swr_ctx.reset(raw);
    ret = true;
  } while (false);
  return ret;
}

// ============================================================
// write_output_header()
// ============================================================

bool AudioResampler::write_output_header()
{
  bool ret = false;
  do {
    if (!(m_up_out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
      if (avio_open(&m_up_out_fmt_ctx->pb,
                    m_up_out_fmt_ctx->url,
                    AVIO_FLAG_WRITE) < 0) {
        Log{}.error("Cannot open output file: %s",
                  m_up_out_fmt_ctx->url);
        break;
      }
    }
    if (avformat_write_header(m_up_out_fmt_ctx.get(), nullptr) < 0) {
      Log{}.error("Cannot write output header.");
      break;
    }
    ret = true;
  } while (false);
  return ret;
}

// ============================================================
// process()
// ============================================================

bool AudioResampler::process()
{
  bool ret = false;
  do {
    if (!m_up_in_fmt_ctx || !m_up_out_fmt_ctx) {
      Log{}.error("Must call open_input() and open_output() first.");
      break;
    }

    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();

    while (av_read_frame(m_up_in_fmt_ctx.get(), pkt) >= 0) {
      if (pkt->stream_index != m_audio_stream_index) {
        av_packet_unref(pkt);
        continue;
      }

      if (avcodec_send_packet(m_up_dec_ctx.get(), pkt) < 0) {
        av_packet_unref(pkt);
        continue;
      }

      while (avcodec_receive_frame(m_up_dec_ctx.get(), frame) >= 0) {
        // 计算重采样后样本数
        int out_samples =
          swr_get_out_samples(m_up_swr_ctx.get(), frame->nb_samples);

        // 分配重采样输出缓冲区
        AVFrame* resampled = av_frame_alloc();
        if (!resampled) break;

        resampled->sample_rate = m_up_enc_ctx->sample_rate;
        resampled->format      = m_up_enc_ctx->sample_fmt;
        av_channel_layout_copy(&resampled->ch_layout,
                               &m_up_enc_ctx->ch_layout);
        resampled->nb_samples = out_samples;

        if (av_frame_get_buffer(resampled, 0) < 0) {
          av_frame_free(&resampled);
          break;
        }

        out_samples = swr_convert(
          m_up_swr_ctx.get(),
          resampled->data, out_samples,
          (const uint8_t**)frame->extended_data,
          frame->nb_samples);

        if (out_samples < 0) {
          av_frame_free(&resampled);
          break;
        }
        resampled->nb_samples = out_samples;

        // 编码并写入
        resampled->pts = m_samples_written;
        m_samples_written += out_samples;

        if (avcodec_send_frame(m_up_enc_ctx.get(), resampled) >= 0) {
          AVPacket* out_pkt = av_packet_alloc();
          while (avcodec_receive_packet(m_up_enc_ctx.get(), out_pkt) >= 0) {
            av_packet_rescale_ts(out_pkt,
                                 m_up_enc_ctx->time_base,
                                 m_p_out_stream->time_base);
            out_pkt->stream_index = m_p_out_stream->index;
            av_interleaved_write_frame(m_up_out_fmt_ctx.get(), out_pkt);
            av_packet_unref(out_pkt);
          }
          av_packet_free(&out_pkt);
        }
        av_frame_free(&resampled);
      }
      av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);

    flush_encoder();

    // 写文件尾
    if (m_up_out_fmt_ctx) {
      av_write_trailer(m_up_out_fmt_ctx.get());
    }

    ret = true;
  } while (false);
  return ret;
}

// ============================================================
// flush_encoder()
// ============================================================

void AudioResampler::flush_encoder()
{
  if (!m_up_enc_ctx) return;

  avcodec_send_frame(m_up_enc_ctx.get(), nullptr);

  AVPacket* out_pkt = av_packet_alloc();
  while (avcodec_receive_packet(m_up_enc_ctx.get(), out_pkt) >= 0) {
    av_packet_rescale_ts(out_pkt,
                         m_up_enc_ctx->time_base,
                         m_p_out_stream->time_base);
    out_pkt->stream_index = m_p_out_stream->index;
    av_interleaved_write_frame(m_up_out_fmt_ctx.get(), out_pkt);
    av_packet_unref(out_pkt);
  }
  av_packet_free(&out_pkt);
}

} // namespace JVNote
