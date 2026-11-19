file(WRITE "${OUT}" "Vox 使用的第三方元件與授權條款\n")

function(component name url)
  file(APPEND "${OUT}" "\n\n------------------------------------------------------------\n${name}\n${url}\n------------------------------------------------------------\n\n")
  foreach(license IN LISTS ARGN)
    file(READ "${ROOT}/${license}" text)
    file(APPEND "${OUT}" "${text}\n")
  endforeach()
endfunction()

component("WebView2 SDK" "https://www.nuget.org/packages/Microsoft.Web.WebView2"
  deps/webview2/LICENSE.txt deps/webview2/NOTICE.txt)
component("ONNX Runtime" "https://github.com/microsoft/onnxruntime"
  deps/onnxruntime/LICENSE deps/onnxruntime/ThirdPartyNotices.txt)
component("Opus" "https://opus-codec.org"
  deps/opus/COPYING)
component("libopusenc" "https://github.com/xiph/libopusenc"
  deps/libopusenc/COPYING)
component("PocketFFT" "https://github.com/mreineck/pocketfft"
  deps/pocketfft/LICENSE.md)
component("SoundTouch JS" "https://github.com/cutterbl/SoundTouchJS"
  ui/vendor/soundtouch/LICENSE)
component("opencc-js" "https://github.com/nk2028/opencc-js"
  ui/vendor/opencc/LICENSE ui/vendor/opencc/Apache-2.0.txt ui/vendor/opencc/THIRD_PARTY_LICENSES.md)
