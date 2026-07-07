# ---- 静态库: audio_extractor ----
add_library(audio_extractor STATIC
    audio_extractor.cpp
)

target_include_directories(audio_extractor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(audio_extractor PUBLIC
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${AVFILTER_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
)

target_include_directories(audio_extractor SYSTEM PUBLIC
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${AVFILTER_INCLUDE_DIRS}
    ${SWRESAMPLE_INCLUDE_DIRS}
)

# ---- 静态库: audio_resampler ----
add_library(audio_resampler STATIC
    audio_resampler.cpp
)

target_include_directories(audio_resampler PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(audio_resampler PUBLIC
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
)

target_include_directories(audio_resampler SYSTEM PUBLIC
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWRESAMPLE_INCLUDE_DIRS}
)

# ---- 静态库: speech_recognizer ----
add_library(speech_recognizer STATIC
    speech_recognizer.cpp
)

target_include_directories(speech_recognizer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(speech_recognizer PUBLIC
    sherpa_onnx_cxx
)

# ---- 动态库: audio_extractor ----
add_library(audio_extractor_shared SHARED
    audio_extractor.cpp
)

target_include_directories(audio_extractor_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(audio_extractor_shared PUBLIC
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${AVFILTER_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
)

target_include_directories(audio_extractor_shared SYSTEM PUBLIC
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${AVFILTER_INCLUDE_DIRS}
    ${SWRESAMPLE_INCLUDE_DIRS}
)
