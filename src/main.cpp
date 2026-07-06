#include "lib/audio_extractor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void print_usage(const char* prog_name)
{
    std::printf("Usage: %s <input.mp4> [output_audio.ext]\n", prog_name);
    std::printf("\n");
    std::printf("Extract audio track from a video file.\n");
    std::printf("\n");
    std::printf("Arguments:\n");
    std::printf("  input.mp4         Input video file (supports any ffmpeg-compatible format)\n");
    std::printf("  output_audio.ext  Output audio file (default: input filename with .mp3 extension)\n");
    std::printf("\n");
    std::printf("Examples:\n");
    std::printf("  %s video.mp4                    → video.mp3\n", prog_name);
    std::printf("  %s video.mp4 audio.aac          → audio.aac\n", prog_name);
    std::printf("  %s video.mp4 soundtrack.m4a     → soundtrack.m4a\n", prog_name);
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

    if (!extract_audio(input_file, output_file)) {
      std::fprintf(stderr, "Failed to extract audio.\n");
      exit_code = 2;
      break;
    }
  } while (false);
  return exit_code;
}
