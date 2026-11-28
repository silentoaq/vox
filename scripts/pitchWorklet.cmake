file(READ "${ROOT}/ui/vendor/soundtouch/soundtouch.js" SOUNDTOUCH)
file(READ "${ROOT}/ui/js/pitchWorklet.js" PROCESSOR)
file(WRITE "${OUT}" "${SOUNDTOUCH}\n${PROCESSOR}")
