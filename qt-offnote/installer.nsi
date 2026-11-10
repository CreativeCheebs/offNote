Unicode true
!include "MUI2.nsh"

!ifndef VERSION
!define VERSION "0.1.0"
!endif

Name "Offnote ${VERSION}"
OutFile "..\dist\Offnote-${VERSION}-Windows-x64-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\Offnote"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "..\dist\Offnote-Windows-x64\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\Offnote"
  CreateShortcut "$SMPROGRAMS\Offnote\Offnote.lnk" "$INSTDIR\Offnote.exe"
  CreateShortcut "$SMPROGRAMS\Offnote\Uninstall Offnote.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Offnote" "DisplayName" "Offnote"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Offnote" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Offnote" "DisplayIcon" "$INSTDIR\Offnote.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Offnote" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Offnote" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Offnote" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\Offnote\Offnote.lnk"
  Delete "$SMPROGRAMS\Offnote\Uninstall Offnote.lnk"
  RMDir "$SMPROGRAMS\Offnote"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Offnote"
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "OffnoteQt"
  RMDir /r "$INSTDIR"
SectionEnd
