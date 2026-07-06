# ---- 静态库 ----
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

# ---- 动态库 ----
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
