; Adopt the per-user Inno installation without running its uninstaller over new files.
; Preserve the legacy plugins folder until the app migrates it into userData.
!define LEGACY_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\{C4F2A6B0-FF2A-45ED-9CD0-B964E50B4754}_is1"
!ifndef BUILD_UNINSTALLER
Var LegacyNodivuDir
!endif
!macro customInit
  ReadRegStr $LegacyNodivuDir HKCU "${LEGACY_KEY}" "InstallLocation"
  ${If} $LegacyNodivuDir != ""
    ${If} ${FileExists} "$LegacyNodivuDir\nodivu-electron.exe"
      GetFullPathName $LegacyNodivuDir "$LegacyNodivuDir\."
      StrCpy $INSTDIR $LegacyNodivuDir
    ${EndIf}
  ${EndIf}
!macroend
!macro customInstall
  CreateDirectory "$DOCUMENTS\Nodivu\Projetos"
  GetFullPathName $R0 "$INSTDIR\."
  ${If} $LegacyNodivuDir != ""
  ${AndIf} $LegacyNodivuDir == $R0
    ; Only retire the exact former Nodivu registration after successful extraction.
    DeleteRegKey HKCU "${LEGACY_KEY}"
    Delete "$INSTDIR\unins000.exe"
    Delete "$INSTDIR\unins000.dat"
    Delete "$SMPROGRAMS\Nodivu\Nodivu.lnk"
  ${EndIf}
!macroend
