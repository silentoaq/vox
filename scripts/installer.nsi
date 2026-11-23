Unicode true
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!include MUI2.nsh
!include FileFunc.nsh
!include LogicLib.nsh

!define APP_NAME "Vox"
!define APP_EXE "Vox.exe"
!define UNINSTALLER "Uninstall Vox.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Vox"
!define LINK_KEY "Software\Classes\vox"
!define WINDOW_CLASS "vox.main"
!define INSTANCE_MUTEX "Local\vox.instance"
!define CLOSE_POLL_MS 200
!define CLOSE_POLLS 50

Name "${APP_NAME}"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
BrandingText "${APP_NAME} ${VERSION}"
ShowInstDetails nevershow
ShowUninstDetails nevershow

Var updating

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"

!define MUI_PAGE_CUSTOMFUNCTION_PRE skipWhenUpdating
!insertmacro MUI_PAGE_DIRECTORY
!define MUI_PAGE_CUSTOMFUNCTION_PRE appendAppFolder
!insertmacro MUI_PAGE_INSTFILES
!define MUI_PAGE_CUSTOMFUNCTION_PRE skipWhenUpdating
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "TradChinese"
!insertmacro MUI_LANGUAGE "SimpChinese"

LangString appRunning ${LANG_ENGLISH} "Vox is running.$\r$\n$\r$\nClick OK to close Vox and continue."
LangString appRunning ${LANG_TRADCHINESE} "Vox 正在執行。$\r$\n$\r$\n按「確定」會關閉 Vox 並繼續。"
LangString appRunning ${LANG_SIMPCHINESE} "Vox 正在运行。$\r$\n$\r$\n点击“确定”会关闭 Vox 并继续。"
LangString appStuck ${LANG_ENGLISH} "Vox didn't close. Close the Vox window and try again."
LangString appStuck ${LANG_TRADCHINESE} "Vox 沒有關閉。請關閉 Vox 的視窗，再試一次。"
LangString appStuck ${LANG_SIMPCHINESE} "Vox 没有关闭。请关闭 Vox 的窗口，然后重试。"
LangString removeData ${LANG_ENGLISH} "Also delete your Vox favorites, playlists, play history, sign-ins and karaoke cache?$\r$\n$\r$\nChoose No to keep them for when you install Vox again."
LangString removeData ${LANG_TRADCHINESE} "要一併刪除 Vox 的收藏、歌單、播放紀錄、登入資料和伴唱快取嗎？$\r$\n$\r$\n選「否」會保留，之後重新安裝可以接續使用。"
LangString removeData ${LANG_SIMPCHINESE} "要同时删除 Vox 的收藏、歌单、播放记录、登录信息和伴唱缓存吗？$\r$\n$\r$\n选“否”会保留，以后重新安装可以继续使用。"

VIProductVersion "${VERSION_NUMBERS}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "${APP_NAME} Setup"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "GPL-3.0"

!include "${FILES_NSH}"

!macro pickLanguage
  System::Call 'kernel32::GetUserDefaultUILanguage() i.r0'
  IntOp $1 $0 & 0x3FF
  IntOp $2 $0 >> 10
  ${If} $1 = 0x04
    ${If} $2 = 2
    ${OrIf} $2 = 4
      StrCpy $LANGUAGE ${LANG_SIMPCHINESE}
    ${Else}
      StrCpy $LANGUAGE ${LANG_TRADCHINESE}
    ${EndIf}
  ${Else}
    StrCpy $LANGUAGE ${LANG_ENGLISH}
  ${EndIf}
!macroend

!macro closeApp PREFIX
  Function ${PREFIX}closeApp
    FindWindow $0 "${WINDOW_CLASS}"
    ${If} $0 <> 0
    ${AndIf} $updating != 1
      MessageBox MB_OKCANCEL|MB_ICONINFORMATION "$(appRunning)" /SD IDOK IDOK +2
      Quit
      SendMessage $0 ${WM_CLOSE} 0 0
    ${EndIf}
    StrCpy $3 0
    ${Do}
      System::Call 'kernel32::OpenMutexW(i 0x00100000, i 0, w "${INSTANCE_MUTEX}") p.r1'
      ${If} $1 P= 0
        ${Break}
      ${EndIf}
      System::Call 'kernel32::CloseHandle(p r1)'
      IntOp $3 $3 + 1
      ${If} $3 > ${CLOSE_POLLS}
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(appStuck)" /SD IDCANCEL IDRETRY +2
        Quit
        StrCpy $3 0
      ${EndIf}
      Sleep ${CLOSE_POLL_MS}
    ${Loop}
  FunctionEnd
!macroend
!insertmacro closeApp ""
!insertmacro closeApp "un."

Function .onInit
  !insertmacro pickLanguage
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "--updated" $R1
  ${IfNot} ${Errors}
    StrCpy $updating 1
  ${EndIf}
FunctionEnd

Function un.onInit
  !insertmacro pickLanguage
FunctionEnd

Function skipWhenUpdating
  ${If} $updating = 1
    Abort
  ${EndIf}
FunctionEnd

Function appendAppFolder
  ${GetFileName} $INSTDIR $0
  ${If} $0 != "${APP_NAME}"
    StrCpy $INSTDIR "$INSTDIR\${APP_NAME}"
  ${EndIf}
FunctionEnd

Section
  Call closeApp
  SetOutPath "$INSTDIR"
  !insertmacro removeAppFiles
  File /r "${SOURCE}\*.*"
  WriteUninstaller "$INSTDIR\${UNINSTALLER}"

  WriteRegStr HKCU "${LINK_KEY}" "" "URL:${APP_NAME}"
  WriteRegStr HKCU "${LINK_KEY}" "URL Protocol" ""
  WriteRegStr HKCU "${LINK_KEY}\DefaultIcon" "" '"$INSTDIR\${APP_EXE}",0'
  WriteRegStr HKCU "${LINK_KEY}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" '"$INSTDIR\${APP_EXE}",0'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\${UNINSTALLER}"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\${UNINSTALLER}" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" $0

  CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

  ${If} $updating = 1
    Exec '"$INSTDIR\${APP_EXE}"'
    SetAutoClose true
  ${EndIf}
SectionEnd

Section "Uninstall"
  Call un.closeApp
  !insertmacro removeAppFiles
  Delete "$INSTDIR\${UNINSTALLER}"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${APP_NAME}.lnk"
  Delete "$DESKTOP\${APP_NAME}.lnk"
  DeleteRegKey HKCU "${LINK_KEY}"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "$(removeData)" /SD IDNO IDNO keepData
  RMDir /r "$LOCALAPPDATA\${APP_NAME}"
  keepData:
SectionEnd
