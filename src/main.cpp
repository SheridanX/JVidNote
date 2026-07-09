#include "lib/audio_extractor.h"
#include "lib/audio_resampler.h"
#include "lib/log.h"
#include "lib/speech_recognizer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using namespace JVNote;

static void print_usage(const char* prog_name)
{
    std::printf("Usage:\n");
    std::printf("  %s <input.mp4> [output_audio.ext]\n", prog_name);
    std::printf("  %s transcribe <wav_file> <model> <tokens>\n", prog_name);
    std::printf("  %s transcribe-video <video> <model> <tokens>\n", prog_name);
    std::printf("\n");
    std::printf("Commands:\n");
    std::printf("  (default)          Extract audio track from a video file.\n");
    std::printf("  transcribe         Transcribe a WAV audio file to text.\n");
    std::printf("  transcribe-video   Extract audio from video and transcribe.\n");
    std::printf("\n");
    std::printf("Examples:\n");
    std::printf("  %s video.mp4                              → video.mp3\n",
               prog_name);
    std::printf("  %s video.mp4 audio.wav                    → audio.wav\n",
               prog_name);
    std::printf("  %s transcribe audio.wav model.onnx tokens.txt\n",
               prog_name);
    std::printf("  %s transcribe-video video.mp4 model.onnx tokens.txt\n",
               prog_name);
}

/**
 * @brief 转写 WAV 文件，结果输出到 stdout。
 */
static int do_transcribe(const char* wav_path,
                          const char* model_path,
                          const char* tokens_path)
{
  int exit_code = 0;
  std::string text;
  do {
    SpeechRecognizer sr;
    SpeechRecognizerConfig cfg;
    cfg.model_path = model_path;
    cfg.tokens_path = tokens_path;

    if (!sr.init(cfg)) {
      Log().error("Failed to initialize speech recognizer.");
      exit_code = 3;
      break;
    }

    text = sr.transcribe_file(
      wav_path,
      [](int chunk_idx, int total, const std::string& chunk_text) {
        if (total > 1) {
          std::fprintf(stderr, "\r[chunk %d/%d] %s\n",
                       chunk_idx + 1, total, chunk_text.c_str());
        }
      });

    if (text.empty()) {
      Log().error("Transcription returned empty result.");
      exit_code = 4;
      break;
    }

    std::printf("%s\n", text.c_str());
  } while (false);
  return exit_code;
}

/**
 * @brief 从视频提取音频（WAV，16kHz 单声道），然后转写。
 */
static int do_transcribe_video(const char* video_path,
                                const char* model_path,
                                const char* tokens_path)
{
  int exit_code = 0;
  do {
    std::string wav_path = video_path;
    auto dot_pos = wav_path.rfind('.');
    if (dot_pos != std::string::npos) {
      wav_path = wav_path.substr(0, dot_pos) + ".wav";
    } else {
      wav_path += ".wav";
    }

    // 步骤1: 提取音频（保持原始格式）
    if (!extract_audio(video_path, wav_path)) {
      Log().error("Failed to extract audio from video.");
      exit_code = 2;
      break;
    }

    // 步骤2: 转为 16kHz 单声道 PCM（SenseVoice 要求）
    {
      std::string tmp_wav = wav_path + ".tmp.wav";
      if (!resample_audio(wav_path, tmp_wav)) {
        Log().error("Failed to convert audio to 16kHz mono.");
        exit_code = 2;
        break;
      }
      // 用重采样后的文件替换原始 WAV
      if (std::rename(tmp_wav.c_str(), wav_path.c_str()) != 0) {
        Log().error("Failed to replace original WAV with resampled file.");
        exit_code = 2;
        break;
      }
    }

    exit_code = do_transcribe(wav_path.c_str(), model_path, tokens_path);
  } while (false);
  return exit_code;
}

/**
 * @brief 在终端打印进度条（由子线程调用）。
 * @param extractor 音频提取器引用
 * @param done      主线程完成后将其设为 true 以终止循环
 */
static void show_progress_bar(const AudioExtractor& extractor,
                               const std::atomic<bool>& done)
{
  constexpr int bar_width = 40;
  int last_pct = -1;

  while (!done.load()) {
    int pct = extractor.get_progress();
    if (pct == last_pct) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    last_pct = pct;

    int filled = pct * bar_width / 100;
    std::fprintf(stderr, "\r[");
    for (int i = 0; i < bar_width; ++i) {
      std::fputc(i < filled ? '=' : (i == filled ? '>' : ' '), stderr);
    }
    std::fprintf(stderr, "] %3d%%", pct);
    std::fflush(stderr);
  }

  // 最终 100% 刷新
  int filled = bar_width;
  std::fprintf(stderr, "\r[");
  for (int i = 0; i < bar_width; ++i) {
    std::fputc('=', stderr);
  }
  std::fprintf(stderr, "] 100%%\n");
  std::fflush(stderr);
}

/**
 * @brief 从视频提取音频，同时在独立线程显示进度条。
 */
static int do_extract_with_progress(const std::string& input_file,
                                     const std::string& output_file)
{
  int exit_code = 0;
  do {
    AudioExtractor ex;
    if (!ex.open_input(input_file)) {
      Log().error("Failed to open input.");
      exit_code = 2;
      break;
    }
    if (!ex.open_output(output_file)) {
      Log().error("Failed to open output.");
      exit_code = 2;
      break;
    }

    std::atomic<bool> done{false};
    std::thread progress_thread(
      [&]() { show_progress_bar(ex, done); });

    bool ok = ex.extract();

    done.store(true);
    progress_thread.join();

    if (!ok) {
      Log().error("Failed to extract audio.");
      exit_code = 2;
      break;
    }

    Log().print("Audio extracted to: %s", output_file.c_str());
  } while (false);
  return exit_code;
}

int main(int argc, char* argv[])
{
  int exit_code = 0;
  do {
    if (argc < 2) {
      print_usage(argv[0]);
      exit_code = 1;
      break;
    }

    if (std::strcmp(argv[1], "-h") == 0 ||
        std::strcmp(argv[1], "--help") == 0) {
      print_usage(argv[0]);
      exit_code = 0;
      break;
    }

    // ---- transcribe 子命令 ----
    if (std::strcmp(argv[1], "transcribe") == 0) {
      if (argc < 5) {
        Log().error("Usage: %s transcribe <wav> <model> <tokens>", argv[0]);
        exit_code = 1;
        break;
      }
      exit_code = do_transcribe(argv[2], argv[3], argv[4]);
      break;
    }

    // ---- transcribe-video 子命令 ----
    if (std::strcmp(argv[1], "transcribe-video") == 0) {
      if (argc < 5) {
        Log().error("Usage: %s transcribe-video <video> <model> <tokens>",
                    argv[0]);
        exit_code = 1;
        break;
      }
      exit_code = do_transcribe_video(argv[2], argv[3], argv[4]);
      break;
    }

    // ---- 未知子命令 ----
    if (argv[1][0] != '-' && argv[1][0] != '.' && argv[1][0] != '/'
        && std::strchr(argv[1], '.') == nullptr) {
      Log().error("Unknown command: %s", argv[1]);
      print_usage(argv[0]);
      exit_code = 1;
      break;
    }

    // ---- 默认：提取音频 ----
    const char* input_file = argv[1];

    std::string output_file;
    if (argc >= 3) {
      output_file = argv[2];
    } else {
      output_file = input_file;
      auto dot_pos = output_file.rfind('.');
      if (dot_pos != std::string::npos) {
        output_file = output_file.substr(0, dot_pos) + ".mp3";
      } else {
        output_file += ".mp3";
      }
    }

    if (do_extract_with_progress(input_file, output_file) != 0) {
      Log().error("Failed to extract audio.");
      exit_code = 2;
      break;
    }
  } while (false);
  return exit_code;
}
