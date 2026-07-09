# ---- 动态库: jvid_note (libjvid_note.so) ----
add_library(jvid_note SHARED
    audio_extractor.cpp
    audio_resampler.cpp
    speech_recognizer.cpp
)

target_include_directories(jvid_note PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jvid_note PUBLIC
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${AVFILTER_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
    sherpa_onnx_cxx
)

target_include_directories(jvid_note SYSTEM PUBLIC
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${AVFILTER_INCLUDE_DIRS}
    ${SWRESAMPLE_INCLUDE_DIRS}
)

set_target_properties(jvid_note PROPERTIES
    VERSION 0.1.0
    SOVERSION 0
)
