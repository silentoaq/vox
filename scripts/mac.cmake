enable_language(OBJCXX)

set(ORT_DIR "${CMAKE_SOURCE_DIR}/deps/onnxruntime")
set(ORT_NATIVE "${ORT_DIR}/runtimes/osx-arm64/native")
set(BUNDLE_ID "com.lichitdev.vox")

add_executable(${APP_ID}-separate
  separate/main.cpp
  separate/demucs.cpp
  separate/spectrogram.cpp
  separate/audioReaderMac.cpp
)
target_include_directories(${APP_ID}-separate PRIVATE "${ORT_DIR}/build/native/include" "${CMAKE_SOURCE_DIR}/deps/pocketfft" "${GENERATED_DIR}")
target_link_libraries(${APP_ID}-separate PRIVATE "${ORT_NATIVE}/libonnxruntime.dylib" opusenc "-framework AudioToolbox"
                                                 "-framework CoreFoundation")
set_target_properties(${APP_ID}-separate PROPERTIES BUILD_RPATH "${ORT_NATIVE}" INSTALL_RPATH "@executable_path/../Frameworks"
                                                   BUILD_WITH_INSTALL_RPATH ON)
target_compile_options(${APP_ID}-separate PRIVATE -Wall -Wextra)

set(MAC_SOURCES
  mac/main.mm
  mac/mainWindow.mm
  mac/server.mm
  mac/bridge.mm
  mac/http.mm
  mac/files.mm
  mac/text.mm
  mac/link.mm
  mac/accounts.mm
  mac/karaoke.mm
  mac/nowPlaying.mm
  mac/desktopLyrics.mm
  mac/fonts.mm
  mac/updater.mm
)
set(APP_ICON "${CMAKE_BINARY_DIR}/Vox.icns")
add_custom_command(OUTPUT "${APP_ICON}"
  COMMAND "${CMAKE_SOURCE_DIR}/scripts/macIcon.sh" "${CMAKE_SOURCE_DIR}/res/vox.png" "${APP_ICON}"
  DEPENDS "${CMAKE_SOURCE_DIR}/res/vox.png" "${CMAKE_SOURCE_DIR}/scripts/macIcon.sh")
set_source_files_properties("${APP_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

add_executable(${APP_ID} MACOSX_BUNDLE ${MAC_SOURCES} "${APP_ICON}")
set_target_properties(${APP_ID} PROPERTIES
  OUTPUT_NAME "${APP_NAME}"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/mac/Info.plist.in"
  MACOSX_BUNDLE_BUNDLE_NAME "${APP_NAME}"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${BUNDLE_ID}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${VOX_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${VOX_VERSION}"
  MACOSX_BUNDLE_ICON_FILE "Vox.icns"
)
target_include_directories(${APP_ID} PRIVATE "${GENERATED_DIR}" "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(${APP_ID} PRIVATE
  APP_NAME=L"${APP_NAME}"
  APP_ID=L"${APP_ID}"
  VOX_DEVTOOLS=$<BOOL:${VOX_DEVTOOLS}>
  VOX_UPDATE_URL="${VOX_UPDATE_URL}"
  VOX_UPDATE_KEY="${VOX_UPDATE_KEY}"
)
target_compile_options(${APP_ID} PRIVATE -fobjc-arc -Wall -Wextra)
target_link_libraries(${APP_ID} PRIVATE
  "-framework Cocoa" "-framework WebKit" "-framework MediaPlayer" "-framework CoreText" "-framework Security"
  "-framework QuartzCore"
)
add_dependencies(${APP_ID} ${APP_ID}-separate)

set(BUNDLE_CONTENTS "$<TARGET_BUNDLE_CONTENT_DIR:${APP_ID}>")
add_custom_command(TARGET ${APP_ID} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${BUNDLE_CONTENTS}/Resources/ui"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/ui" "${BUNDLE_CONTENTS}/Resources/ui"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/res/vox.png" "${BUNDLE_CONTENTS}/Resources/ui/vox.png"
  COMMAND ${CMAKE_COMMAND} "-DROOT=${CMAKE_SOURCE_DIR}" "-DOUT=${BUNDLE_CONTENTS}/Resources/ui/vox-pitch-worklet.js"
          -P "${CMAKE_SOURCE_DIR}/scripts/pitchWorklet.cmake"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_FILE}" "${BUNDLE_CONTENTS}/Resources/models/${MODEL_NAME}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "${BUNDLE_CONTENTS}/Resources/LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} "-DROOT=${CMAKE_SOURCE_DIR}" "-DOUT=${BUNDLE_CONTENTS}/Resources/THIRD_PARTY_NOTICES.txt"
          -DORT=deps/onnxruntime -P "${CMAKE_SOURCE_DIR}/scripts/notices.cmake"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${APP_ID}-separate>" "${BUNDLE_CONTENTS}/MacOS/${APP_ID}-separate"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_CONTENTS}/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy "${ORT_NATIVE}/libonnxruntime.dylib" "${BUNDLE_CONTENTS}/Frameworks/libonnxruntime.1.dylib"
  COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${APP_ID}>"
)
