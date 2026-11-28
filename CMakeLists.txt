cmake_minimum_required(VERSION 3.25)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Oldest macOS Vox runs on")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "ONNX Runtime ships macOS binaries for Apple Silicon only")
project(vox LANGUAGES C CXX)

if(NOT VOX_VERSION)
  string(TIMESTAMP VOX_VERSION "%Y.%m.%d")
endif()
if(NOT VOX_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(\\.[0-9]+)?$")
  message(FATAL_ERROR "VOX_VERSION 要是年.月.日：${VOX_VERSION}")
endif()
set(VOX_VERSION_NUMBERS "${VOX_VERSION}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  string(APPEND VOX_VERSION_NUMBERS ".0")
endif()
string(REGEX REPLACE "\\.0+([0-9])" ".\\1" VOX_VERSION_NUMBERS "${VOX_VERSION_NUMBERS}")
string(REPLACE "." "," VOX_VERSION_NUMBERS "${VOX_VERSION_NUMBERS}")

set(APP_NAME "Vox")
set(APP_ID "vox")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(src/version.h.in "${GENERATED_DIR}/version.h" @ONLY)

set(VOX_UPDATE_URL "https://vox.lichitdev.com/latest" CACHE STRING "Where the signed update manifest is published")
if(NOT VOX_UPDATE_URL)
  message(FATAL_ERROR "VOX_UPDATE_URL 沒設")
endif()
file(READ "${CMAKE_SOURCE_DIR}/res/update.pub" VOX_UPDATE_KEY)
string(STRIP "${VOX_UPDATE_KEY}" VOX_UPDATE_KEY)
set(MODEL_NAME "demucs-hdemucs_mmi-10s.onnx")
set(MODEL_FILE "${CMAKE_SOURCE_DIR}/deps/models/${MODEL_NAME}")
option(VOX_DEVTOOLS "Keep DevTools, browser shortcuts and remote debugging for development" OFF)

set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
if(APPLE)
  set(OPUS_DISABLE_INTRINSICS ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(deps/opus EXCLUDE_FROM_ALL)

set(OPUSENC_DIR "${CMAKE_SOURCE_DIR}/deps/libopusenc")
add_library(opusenc STATIC
  ${OPUSENC_DIR}/src/ogg_packer.c
  ${OPUSENC_DIR}/src/opusenc.c
  ${OPUSENC_DIR}/src/opus_header.c
  ${OPUSENC_DIR}/src/picture.c
  ${OPUSENC_DIR}/src/resample.c
  ${OPUSENC_DIR}/src/unicode_support.c
)
target_include_directories(opusenc PUBLIC "${OPUSENC_DIR}/include")
target_compile_definitions(opusenc PRIVATE OUTSIDE_SPEEX RANDOM_PREFIX=libopusenc OPE_BUILD PACKAGE_NAME="libopusenc"
                                           PACKAGE_VERSION="0.2.1" RESAMPLE_FULL_SINC_TABLE=1 _CRT_SECURE_NO_WARNINGS)
target_link_libraries(opusenc PUBLIC opus)

if(WIN32)
  include(scripts/windows.cmake)
elseif(APPLE)
  include(scripts/mac.cmake)
else()
  message(FATAL_ERROR "Vox 只支援 Windows 和 macOS")
endif()
