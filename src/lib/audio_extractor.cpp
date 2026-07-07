extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include "audio_extractor.h"
#include "log.h"

#include <cstring>

namespace JVNote
{

// ============================================================
// 自定义删除器实现
// ============================================================

void AVFormatInputDeleter::operator()(AVFormatContext* p) const
{
    if (p) avformat_close_input(&p);
}

void AVFormatOutputDeleter::operator()(AVFormatContext* p) const
{
    if (p) {
        if (!(p->oformat->flags & AVFMT_NOFILE) && p->pb) {
            avio_closep(&p->pb);
        }
        avformat_free_context(p);
    }
}

void AVCodecContextDeleter::operator()(AVCodecContext* p) const
{
    if (p) avcodec_free_context(&p);
}

void SwrContextDeleter::operator()(SwrContext* p) const
{
    if (p) swr_free(&p);
}

void AudioFifoDeleter::operator()(AVAudioFifo* p) const
{
    if (p) av_audio_fifo_free(p);
}

// ============================================================
// close()
// ============================================================

void AudioExtractor::close()
{
    m_up_audio_fifo.reset();
    m_up_swr_ctx.reset();
    m_up_enc_ctx.reset();
    m_up_dec_ctx.reset();
    m_up_out_fmt_ctx.reset();
    m_up_in_fmt_ctx.reset();
    m_p_in_audio_stream    = nullptr;
    m_p_out_stream        = nullptr;
    m_audio_stream_index = -1;
    m_samples_written     = 0;
}

// ============================================================
// open_input()
// ============================================================

bool AudioExtractor::open_input(const std::string& input_file)
{
  bool ret = false;
  close();
  do {
    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, input_file.c_str(), nullptr, nullptr) < 0) {
      Log{}.error("Cannot open input file: %s", input_file.c_str());
      ret = false;
      break;
    }
    m_up_in_fmt_ctx.reset(raw);

    if (avformat_find_stream_info(m_up_in_fmt_ctx.get(), nullptr) < 0) {
      Log{}.error("Cannot find stream info.");
      ret = false;
      break;
    }

    ret = find_audio_stream() && setup_decoder();
  } while (false);
  return ret;
}

// ============================================================
// open_output()
// ============================================================

bool AudioExtractor::open_output(const std::string& output_file)
{
  bool ret = false;
  do {
    if (!m_up_in_fmt_ctx || !m_up_dec_ctx) {
      Log{}.error("Must call open_input() first.");
      break;
    }

    AVFormatContext* raw = nullptr;
    if (avformat_alloc_output_context2(&raw, nullptr, nullptr,
                                       output_file.c_str()) < 0) {
      Log{}.error("Cannot create output context. "
                "Try a different extension (e.g. .mp3, .m4a).");
      break;
    }
    m_up_out_fmt_ctx.reset(raw);

    if (!setup_output_stream()) break;
    if (!write_output_header()) break;
    if (!setup_resampler()) break;

    ret = true;
  } while (false);
  return ret;
}

// ============================================================
// extract()
// ============================================================

bool AudioExtractor::extract()
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
      if (m_up_enc_ctx) process_encoded_packet(pkt);
      else              process_stream_copy_packet(pkt);
      av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);

    flush_encoder();
    write_trailer();
    ret = true;
  } while (false);
  return ret;
}

// ============================================================
// 私有方法
// ============================================================

bool AudioExtractor::find_audio_stream() 
{
  m_audio_stream_index = av_find_best_stream(m_up_in_fmt_ctx.get(),
    AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (m_audio_stream_index < 0) {
    Log{}.error("No audio stream found in input file.");
    return false;
  }
  m_p_in_audio_stream = m_up_in_fmt_ctx->streams[m_audio_stream_index];
  return true;
}

bool AudioExtractor::setup_decoder()
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

bool AudioExtractor::setup_output_stream()
{
  bool ret = false;
  do {
    // 1. 创建输出流
    m_p_out_stream = avformat_new_stream(m_up_out_fmt_ctx.get(), nullptr);
    if (!m_p_out_stream) {
      Log{}.error("Cannot create output stream.");
      break;
    }

    // 2. 优先尝试 stream copy（无损、快速，不经过解码→编码）
    avcodec_parameters_copy(m_p_out_stream->codecpar,
                            m_p_in_audio_stream->codecpar);
    m_p_out_stream->codecpar->codec_tag = 0;
    m_p_out_stream->time_base = m_p_in_audio_stream->time_base;

    // WAV 容器只支持 PCM 编码，其他格式必须重编码
    bool is_wav = m_up_out_fmt_ctx->oformat->name &&
                  std::strcmp(m_up_out_fmt_ctx->oformat->name, "wav") == 0;
    bool is_pcm = m_p_in_audio_stream->codecpar->codec_id == AV_CODEC_ID_PCM_S16LE
               || m_p_in_audio_stream->codecpar->codec_id == AV_CODEC_ID_PCM_S16BE
               || m_p_in_audio_stream->codecpar->codec_id == AV_CODEC_ID_PCM_U8;

    if (avformat_query_codec(m_up_out_fmt_ctx->oformat,
                             m_p_in_audio_stream->codecpar->codec_id,
                             FF_COMPLIANCE_NORMAL) == 1
        && !(is_wav && !is_pcm)) {
      ret = true;  // stream copy 模式，无需 encoder
      break;
    }

    // 3. 输出容器不支持源编码，fallback 到重编码
    Log{}.print("Output container does not support source codec, "
              "falling back to re-encoding.");

    AVCodecID codec_id = m_up_out_fmt_ctx->oformat->audio_codec;
    const AVCodec* codec = avcodec_find_encoder(codec_id);

    if (!codec) {
      Log{}.error("No suitable encoder found and stream copy not supported.");
      break;
    }

    AVCodecContext* raw = avcodec_alloc_context3(codec);
    if (!raw) {
      Log{}.error("Cannot allocate encoder context.");
      break;
    }
    m_up_enc_ctx.reset(raw);

    m_up_enc_ctx->sample_rate = m_up_dec_ctx->sample_rate;
    av_channel_layout_copy(&m_up_enc_ctx->ch_layout,
                           &m_up_dec_ctx->ch_layout);
    m_up_enc_ctx->sample_fmt =
      codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;

    if (codec->supported_samplerates) {
      m_up_enc_ctx->sample_rate = codec->supported_samplerates[0];
      for (int i = 0; codec->supported_samplerates[i]; ++i) {
        if (codec->supported_samplerates[i] == m_up_dec_ctx->sample_rate) {
          m_up_enc_ctx->sample_rate = m_up_dec_ctx->sample_rate;
          break;
        }
      }
    }

    m_up_enc_ctx->time_base = AVRational{1, m_up_enc_ctx->sample_rate};

    if (m_up_out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
      m_up_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(m_up_enc_ctx.get(), codec, nullptr) < 0) {
      Log{}.error("Cannot open encoder.");
      m_up_enc_ctx.reset();
      break;
    }

    // 用编码器参数覆盖输出流
    avcodec_parameters_from_context(m_p_out_stream->codecpar,
                                    m_up_enc_ctx.get());
    m_p_out_stream->time_base = m_up_enc_ctx->time_base;

    ret = true;
  } while (false);
  return ret;
}

bool AudioExtractor::setup_resampler()
{
  bool ret = false;
  do {
    if (!m_up_enc_ctx) { ret = true; break; }

    SwrContext* raw = nullptr;
    int rc = swr_alloc_set_opts2(
      &raw,
      &m_up_enc_ctx->ch_layout, m_up_enc_ctx->sample_fmt,
      m_up_enc_ctx->sample_rate,
      &m_up_dec_ctx->ch_layout, m_up_dec_ctx->sample_fmt,
      m_up_dec_ctx->sample_rate, 0, nullptr);
    if (rc < 0 || swr_init(raw) < 0) {
      Log{}.error("Cannot initialize resampler.");
      break;
    }
    m_up_swr_ctx.reset(raw);
    ret = true;

    // 4. 创建音频 FIFO 用于缓冲重采样后的数据
    int frame_size = m_up_enc_ctx->frame_size;
    if (frame_size <= 0) {
      frame_size = 1024;  // 如果编码器没指定，使用默认值
    }
    m_up_audio_fifo.reset(
      av_audio_fifo_alloc(m_up_enc_ctx->sample_fmt,
                          m_up_enc_ctx->ch_layout.nb_channels,
                          frame_size * 2));
    if (!m_up_audio_fifo) {
      Log{}.error("Cannot allocate audio FIFO.");
      ret = false;
      break;
    }
  } while (false);
  return ret;
}

bool AudioExtractor::write_output_header()
{
  bool ret = false;
  do {
    if (!(m_up_out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
      if (avio_open(&m_up_out_fmt_ctx->pb, m_up_out_fmt_ctx->url,
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

void AudioExtractor::process_encoded_packet(AVPacket* pkt)
{
  bool ret = false;
  do {
    if (avcodec_send_packet(m_up_dec_ctx.get(), pkt) < 0) {
      break;
    }

    AVFrame* decoded_frame = av_frame_alloc();
    if (!decoded_frame) break;

    while (avcodec_receive_frame(m_up_dec_ctx.get(), decoded_frame) >= 0) {
      // 重采样到输出格式
      AVFrame* resampled_frame = av_frame_alloc();
      if (!resampled_frame) break;

      uint8_t* tmp_buf[8] = {nullptr};
      int out_samples = swr_get_out_samples(m_up_swr_ctx.get(),
                                            decoded_frame->nb_samples);
      if (av_samples_alloc(tmp_buf, nullptr,
                           m_up_enc_ctx->ch_layout.nb_channels,
                           out_samples,
                           m_up_enc_ctx->sample_fmt, 0) < 0) {
        av_frame_free(&resampled_frame);
        break;
      }

      out_samples = swr_convert(m_up_swr_ctx.get(), tmp_buf, out_samples,
                                (const uint8_t**)decoded_frame->extended_data,
                                decoded_frame->nb_samples);

      if (out_samples < 0) {
        av_freep(&tmp_buf[0]);
        av_frame_free(&resampled_frame);
        break;
      }

      // 写入 FIFO
      if (av_audio_fifo_write(m_up_audio_fifo.get(), (void**)tmp_buf,
                              out_samples) < 0) {
        av_freep(&tmp_buf[0]);
        av_frame_free(&resampled_frame);
        break;
      }
      av_freep(&tmp_buf[0]);
      av_frame_free(&resampled_frame);

      // 从 FIFO 取出完整帧送入编码器
      int frame_size = m_up_enc_ctx->frame_size;
      if (frame_size <= 0) frame_size = 1024;

      while (av_audio_fifo_size(m_up_audio_fifo.get()) >= frame_size) {
        AVFrame* enc_frame = av_frame_alloc();
        if (!enc_frame) break;

        enc_frame->nb_samples = frame_size;
        enc_frame->sample_rate = m_up_enc_ctx->sample_rate;
        enc_frame->format = m_up_enc_ctx->sample_fmt;
        av_channel_layout_copy(&enc_frame->ch_layout,
                               &m_up_enc_ctx->ch_layout);

        if (av_frame_get_buffer(enc_frame, 0) < 0) {
          av_frame_free(&enc_frame);
          break;
        }

        if (av_audio_fifo_read(m_up_audio_fifo.get(),
                               (void**)enc_frame->extended_data,
                               frame_size) < frame_size) {
          av_frame_free(&enc_frame);
          break;
        }

        enc_frame->pts = m_samples_written;
        m_samples_written += frame_size;

        if (avcodec_send_frame(m_up_enc_ctx.get(), enc_frame) >= 0) {
          AVPacket* out_pkt = av_packet_alloc();
          while (avcodec_receive_packet(m_up_enc_ctx.get(), out_pkt) >= 0) {
            av_packet_rescale_ts(out_pkt, m_up_enc_ctx->time_base,
                                 m_p_out_stream->time_base);
            out_pkt->stream_index = m_p_out_stream->index;
            av_interleaved_write_frame(m_up_out_fmt_ctx.get(), out_pkt);
            av_packet_unref(out_pkt);
          }
          av_packet_free(&out_pkt);
        }
        av_frame_free(&enc_frame);
      }
    }
    av_frame_free(&decoded_frame);
    ret = true;
  } while (false);
  (void)ret;
}

void AudioExtractor::process_stream_copy_packet(AVPacket* pkt)
{
    av_packet_rescale_ts(pkt, m_p_in_audio_stream->time_base, m_p_out_stream->time_base);
    pkt->stream_index = m_p_out_stream->index;
    av_interleaved_write_frame(m_up_out_fmt_ctx.get(), pkt);
}

void AudioExtractor::flush_encoder()
{
    if (!m_up_enc_ctx) return;

    // 将 FIFO 中剩余的样本送入编码器
    int remaining = av_audio_fifo_size(m_up_audio_fifo.get());
    if (remaining > 0) {
        AVFrame* enc_frame = av_frame_alloc();
        if (enc_frame) {
            enc_frame->nb_samples = remaining;
            enc_frame->sample_rate = m_up_enc_ctx->sample_rate;
            enc_frame->format = m_up_enc_ctx->sample_fmt;
            av_channel_layout_copy(&enc_frame->ch_layout,
                                   &m_up_enc_ctx->ch_layout);

            if (av_frame_get_buffer(enc_frame, 0) >= 0) {
                av_audio_fifo_read(m_up_audio_fifo.get(),
                                   (void**)enc_frame->extended_data,
                                   remaining);
                enc_frame->pts = m_samples_written;
                m_samples_written += remaining;
                avcodec_send_frame(m_up_enc_ctx.get(), enc_frame);
            }
            av_frame_free(&enc_frame);
        }
    }

    avcodec_send_frame(m_up_enc_ctx.get(), nullptr);
    AVPacket* out_pkt = av_packet_alloc();
    while (avcodec_receive_packet(m_up_enc_ctx.get(), out_pkt) >= 0) {
        av_packet_rescale_ts(out_pkt, m_up_enc_ctx->time_base, m_p_out_stream->time_base);
        out_pkt->stream_index = m_p_out_stream->index;
        av_interleaved_write_frame(m_up_out_fmt_ctx.get(), out_pkt);
        av_packet_unref(out_pkt);
    }
    av_packet_free(&out_pkt);
}

void AudioExtractor::write_trailer()
{
    if (m_up_out_fmt_ctx) {
        av_write_trailer(m_up_out_fmt_ctx.get());
    }
}

} // namespace JVNote
