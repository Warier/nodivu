Unicode True
ManifestDPIAware True
RequestExecutionLevel user
SetCompressor /SOLID lzma
!include MUI2.nsh
!include LogicLib.nsh
!include x64.nsh
!ifdef MIGRATION_TEST
  !define APP_GUID "Nodivu.QtMigration.IsolatedTest"
  !define PRODUCT_NAME "Nodivu Qt Migration Test"
  !ifdef UPDATE_TEST
    ; Only the isolated acceptance identity may run without interactive pages.
    SilentInstall silent
  !endif
!else
  !ifdef UPDATE_TEST
    !error "UPDATE_TEST requires MIGRATION_TEST"
  !endif
  !define APP_GUID "97ab4969-abe1-5c91-bf9d-ddc217a0e511"
  !define PRODUCT_NAME "Nodivu"
!endif
!define INSTALL_KEY "Software\${APP_GUID}"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_GUID}"
!define LEGACY_INNO_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\{C4F2A6B0-FF2A-45ED-9CD0-B964E50B4754}_is1"
!ifndef MIGRATION_TEST
  Var LegacyNodivuDir
!endif
Name "${PRODUCT_NAME}"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\${PRODUCT_NAME}"
InstallDirRegKey HKCU "${INSTALL_KEY}" "InstallLocation"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "PortugueseBR"
!macro CheckExecutableUnlocked FILE
  ${If} ${FileExists} "${FILE}"
    ClearErrors
    FileOpen $R8 "${FILE}" a
    ${If} ${Errors}
      MessageBox MB_OK|MB_ICONSTOP "Encerre o Nodivu antes de continuar. Nenhum processo será finalizado à força." /SD IDOK
      SetErrorLevel 1
      Abort
    ${EndIf}
    FileClose $R8
  ${EndIf}
!macroend
Function un.onInit
  !insertmacro CheckExecutableUnlocked "$INSTDIR\nodivu.exe"
  !insertmacro CheckExecutableUnlocked "$INSTDIR\bin\nodivu-app-backend.exe"
FunctionEnd
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Este pacote exige Windows x64."
    Abort
  ${EndIf}
  SetShellVarContext current
!ifndef MIGRATION_TEST
  ReadRegStr $LegacyNodivuDir HKCU "${LEGACY_INNO_KEY}" "InstallLocation"
  ${If} $LegacyNodivuDir != ""
  ${AndIf} ${FileExists} "$LegacyNodivuDir\nodivu-electron.exe"
    GetFullPathName $LegacyNodivuDir "$LegacyNodivuDir\."
    StrCpy $INSTDIR $LegacyNodivuDir
  ${EndIf}
!endif
  ; Do not replace files held by either frontend. No forced process termination.
  IfFileExists "$INSTDIR\nodivu.exe" 0 +5
    ClearErrors
    FileOpen $0 "$INSTDIR\nodivu.exe" a
    IfErrors busy
    FileClose $0
  IfFileExists "$INSTDIR\nodivu-electron.exe" 0 done
    ClearErrors
    FileOpen $0 "$INSTDIR\nodivu-electron.exe" a
    IfErrors busy
    FileClose $0
    Goto done
  busy:
    MessageBox MB_OK|MB_ICONSTOP "Encerre o Nodivu antes de instalar. Nenhum processo será finalizado à força."
    Abort
  done:
FunctionEnd
Section "Nodivu" Main
  InitPluginsDir
  SetOutPath "$PLUGINSDIR\payload"
  SetOverwrite on
  File /r "${STAGE}\*"
  WriteUninstaller "$PLUGINSDIR\payload\Uninstall Nodivu.exe"
  CreateDirectory "$INSTDIR\.."
  DetailPrint "Preparando, verificando e validando a nova instalação…"
  nsExec::ExecToStack '"$PLUGINSDIR\payload\nodivu-install-tool.exe" --payload "$PLUGINSDIR\payload" --target "$INSTDIR"'
  Pop $0
  Pop $1
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "A instalação não foi concluída. A versão anterior foi preservada.$\r$\n$1"
    SetErrorLevel 1
    Abort
  ${EndIf}
  SetOutPath "$INSTDIR"
  ClearErrors
  WriteRegStr HKCU "${INSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${INSTALL_KEY}" "ShortcutName" "${PRODUCT_NAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall Nodivu.exe$\"'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}.lnk" "$INSTDIR\nodivu.exe"
  ${If} ${Errors}
    MessageBox MB_OK|MB_ICONSTOP "Os arquivos do Nodivu foram verificados, mas não foi possível registrar o aplicativo ou criar o atalho. Execute o instalador novamente." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
!ifndef MIGRATION_TEST
  CreateDirectory "$DOCUMENTS\Nodivu\Projetos"
  GetFullPathName $R0 "$INSTDIR\."
  ${If} $LegacyNodivuDir != ""
  ${AndIf} $LegacyNodivuDir == $R0
    DeleteRegKey HKCU "${LEGACY_INNO_KEY}"
    Delete "$SMPROGRAMS\Nodivu\Nodivu.lnk"
    RMDir "$SMPROGRAMS\Nodivu"
  ${EndIf}
!endif
SectionEnd
Section "Uninstall"
  SetShellVarContext current
  ReadRegStr $0 HKCU "${INSTALL_KEY}" "InstallLocation"
  ${If} $0 != $INSTDIR
    MessageBox MB_OK|MB_ICONSTOP "A pasta não corresponde à instalação registrada. Arquivos preservados."
    Abort
  ${EndIf}
  !include "${UNINSTALL_FILES}"
  Delete "$INSTDIR\Uninstall Nodivu.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}.lnk"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${INSTALL_KEY}"
  ; AppData, Documents, user plugins and unknown files are deliberately preserved.
  Goto uninstall_done
  uninstall_failed:
    MessageBox MB_OK|MB_ICONSTOP "Um arquivo do aplicativo não pôde ser removido. O registro e o atalho foram preservados; encerre o Nodivu e tente novamente." /SD IDOK
    SetErrorLevel 1
    Abort
  uninstall_done:
SectionEnd
