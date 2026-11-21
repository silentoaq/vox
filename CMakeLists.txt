cmake_minimum_required(VERSION 3.25)
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

set(WEBVIEW2_DIR "${CMAKE_SOURCE_DIR}/deps/webview2/build/native")
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(src/version.h.in "${GENERATED_DIR}/version.h" @ONLY)

set(VOX_UPDATE_URL "https://vox.lichitdev.com/latest" CACHE STRING "Where the signed update manifest is published")
if(NOT VOX_UPDATE_URL)
  message(FATAL_ERROR "VOX_UPDATE_URL 沒設")
endif()
file(READ "${CMAKE_SOURCE_DIR}/res/update.pub" VOX_UPDATE_KEY)
string(STRIP "${VOX_UPDATE_KEY}" VOX_UPDATE_KEY)
set(MODEL_FILE "${CMAKE_SOURCE_DIR}/deps/models/demucs-hdemucs_mmi-10s.onnx")

add_executable(${APP_ID} WIN32
  src/main.cpp
  src/mainWindow.cpp
  src/bridge.cpp
  src/http.cpp
  src/link.cpp
  src/files.cpp
  src/desktopLyrics.cpp
  src/karaoke.cpp
  src/accounts.cpp
  src/fonts.cpp
  src/lockdown.cpp
  src/nowPlaying.cpp
  src/install.cpp
  src/updater.cpp
  src/text.cpp
  src/tray.cpp
  src/vox.rc
)

option(VOX_DEVTOOLS "Keep DevTools, browser shortcuts and remote debugging for development" OFF)

target_include_directories(${APP_ID} PRIVATE "${WEBVIEW2_DIR}/include" "${GENERATED_DIR}")
set_target_properties(${APP_ID} PROPERTIES OUTPUT_NAME "${APP_NAME}")
target_link_libraries(${APP_ID} PRIVATE
  "${WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib"
  winhttp windowsapp shell32 advapi32 version dwmapi d2d1 dwrite bcrypt crypt32
)
target_compile_definitions(${APP_ID} PRIVATE
  APP_NAME=L"${APP_NAME}"
  APP_ID=L"${APP_ID}"
  VOX_DEVTOOLS=$<BOOL:${VOX_DEVTOOLS}>
  VOX_UPDATE_URL=L"${VOX_UPDATE_URL}"
  VOX_UPDATE_KEY=L"${VOX_UPDATE_KEY}"
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
target_compile_options(${APP_ID} PRIVATE /utf-8 /W4 /permissive- /EHsc)

add_custom_command(TARGET ${APP_ID} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/ui" "$<TARGET_FILE_DIR:${APP_ID}>/ui"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_FILE}" "$<TARGET_FILE_DIR:${APP_ID}>/models/demucs-hdemucs_mmi-10s.onnx"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:${APP_ID}>/LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} "-DROOT=${CMAKE_SOURCE_DIR}" "-DOUT=$<TARGET_FILE_DIR:${APP_ID}>/THIRD_PARTY_NOTICES.txt"
          -P "${CMAKE_SOURCE_DIR}/scripts/notices.cmake"
)

set(ORT_DIR "${CMAKE_SOURCE_DIR}/deps/onnxruntime")
set(ORT_NATIVE "${ORT_DIR}/runtimes/win-x64/native")

set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
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

add_executable(${APP_ID}-separate
  separate/main.cpp
  separate/demucs.cpp
  separate/spectrogram.cpp
  separate/audioReader.cpp
  separate/incoming.cpp
  separate/separate.rc
)
target_include_directories(${APP_ID}-separate PRIVATE "${ORT_DIR}/build/native/include" "${CMAKE_SOURCE_DIR}/deps/pocketfft"
                                                       "${GENERATED_DIR}")
target_link_libraries(${APP_ID}-separate PRIVATE "${ORT_NATIVE}/onnxruntime.lib" mfplat mfreadwrite mfuuid opusenc)
target_compile_definitions(${APP_ID}-separate PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(${APP_ID}-separate PRIVATE /utf-8 /W4 /permissive- /EHsc)
add_custom_command(TARGET ${APP_ID}-separate POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_NATIVE}/onnxruntime.dll" "$<TARGET_FILE_DIR:${APP_ID}-separate>"
)
