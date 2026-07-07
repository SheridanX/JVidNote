cmake_minimum_required(VERSION 3.16)
project(JVidNote VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认 Debug，release 构建时传 -DCMAKE_BUILD_TYPE=Release
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# 匹配 sherpa-onnx 预编译库的 C++ ABI（旧版，无 __cxx11）
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

# Debug: 嵌入源码路径 RPATH，方便直接运行调试
# Release: 不嵌入任何 RPATH，由打包/部署脚本自行处理 .so 路径
if(CMAKE_BUILD_TYPE MATCHES "Debug")
  set(CMAKE_BUILD_RPATH "${CMAKE_SOURCE_DIR}/lib")
  set(CMAKE_INSTALL_RPATH "${CMAKE_SOURCE_DIR}/lib")
else()
  set(CMAKE_SKIP_BUILD_RPATH TRUE)
  set(CMAKE_SKIP_INSTALL_RPATH TRUE)
endif()

# ---- Sherpa-ONNX (bundled in lib/) ----
set(SHERPA_ONNX_DIR "${CMAKE_SOURCE_DIR}/lib")
set(SHERPA_ONNX_INC "${SHERPA_ONNX_DIR}/include")

add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${SHERPA_ONNX_DIR}/libonnxruntime.so"
    INTERFACE_INCLUDE_DIRECTORIES "${SHERPA_ONNX_INC}")

add_library(sherpa_onnx_c_api SHARED IMPORTED)
set_target_properties(sherpa_onnx_c_api PROPERTIES
    IMPORTED_LOCATION "${SHERPA_ONNX_DIR}/libsherpa-onnx-c-api.so"
    INTERFACE_INCLUDE_DIRECTORIES "${SHERPA_ONNX_INC}"
    INTERFACE_LINK_LIBRARIES onnxruntime)

add_library(sherpa_onnx_cxx SHARED IMPORTED)
set_target_properties(sherpa_onnx_cxx PROPERTIES
    IMPORTED_LOCATION "${SHERPA_ONNX_DIR}/libsherpa-onnx-cxx-api.so"
    INTERFACE_INCLUDE_DIRECTORIES "${SHERPA_ONNX_INC}"
    INTERFACE_LINK_LIBRARIES sherpa_onnx_c_api)

# Find FFmpeg packages
find_package(PkgConfig REQUIRED)

pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC  REQUIRED libavcodec)
pkg_check_modules(AVUTIL   REQUIRED libavutil)
pkg_check_modules(AVFILTER REQUIRED libavfilter)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)

add_subdirectory(src)
