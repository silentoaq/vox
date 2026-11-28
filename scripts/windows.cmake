set(WEBVIEW2_DIR "${CMAKE_SOURCE_DIR}/deps/webview2/build/native")

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
  src/vox.rc
)

target_include_directories(${APP_ID} PRIVATE "${WEBVIEW2_DIR}/include" "${GENERATED_DIR}")
set_target_properties(${APP_ID} PROPERTIES OUTPUT_NAME "${APP_NAME}")
target_link_libraries(${APP_ID} PRIVATE
  "${WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib"
  winhttp windowsapp shell32 shlwapi advapi32 version dwmapi d2d1 dwrite bcrypt crypt32
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
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/res/vox.png" "$<TARGET_FILE_DIR:${APP_ID}>/ui/vox.png"
  COMMAND ${CMAKE_COMMAND} "-DROOT=${CMAKE_SOURCE_DIR}" "-DOUT=$<TARGET_FILE_DIR:${APP_ID}>/ui/vox-pitch-worklet.js"
          -P "${CMAKE_SOURCE_DIR}/scripts/pitchWorklet.cmake"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_FILE}" "$<TARGET_FILE_DIR:${APP_ID}>/models/${MODEL_NAME}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:${APP_ID}>/LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} "-DROOT=${CMAKE_SOURCE_DIR}" "-DOUT=$<TARGET_FILE_DIR:${APP_ID}>/THIRD_PARTY_NOTICES.txt" -DWEBVIEW2=ON -DORT=deps/onnxruntime
          -P "${CMAKE_SOURCE_DIR}/scripts/notices.cmake"
)

set(ORT_DIR "${CMAKE_SOURCE_DIR}/deps/onnxruntime")
set(ORT_NATIVE "${ORT_DIR}/runtimes/win-x64/native")

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
