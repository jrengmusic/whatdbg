/*******************************************************************************
                        Codegen Annotated Source of Truth
————————————————————————————————————————————————————————————————————————————————

            ░░████████████░░████████████░░████████████░░████████████
            ░░████  ░░████░░████  ░░████░░████  ░░████    ░░████
            ░░████        ░░████  ░░████░░████            ░░████
            ░░████        ░░████████████░░████████████    ░░████
            ░░████        ░░████  ░░████        ░░████    ░░████
            ░░████  ░░████░░████  ░░████░░████  ░░████    ░░████
            ░░████████████░░████  ░░████░░████████████    ░░████

————————————————————————————————————————————————————————————————————————————————
                         FOR YOUR EYES ONLY, DO NOT EDIT
********************************************************************************/

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "WinCore.nsh"

!define PRODUCT "whatdbg"
!define VERSION "0.0.1"
!define PUBLISHER "Jubilant Research of Eclectic Novelty Generation"
!define COMPANY "JRENG"
!define THUMBPRINT "8274AC1EA9DEA10AEFF7FD683D5E2BC4B5FAAAF1"
!define UNINSTALLKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT}"
!define ENVIRONMENTKEY "Environment"
!define PATHVALUE "Path"

!define RRF_RT_REG_SZ 0x00000002
!define RRF_RT_REG_EXPAND_SZ 0x00000004
!define RRF_NOEXPAND 0x10000000
!define /math PATH_VALUE_TYPES ${RRF_RT_REG_SZ} | ${RRF_RT_REG_EXPAND_SZ}
!define /math PATH_READ_FLAGS ${PATH_VALUE_TYPES} | ${RRF_NOEXPAND}
!define WCHAR_SIZE 2
!define PATH_SEPARATOR_COUNT 2
!define BROADCAST_TIMEOUT_MS 5000

Name "${PRODUCT}"
OutFile "${OUTFILE}"
Unicode True
BrandingText "${COMPANY}"
RequestExecutionLevel user
InstallDir "$PROFILE\.local\bin"

!system '"${SIGNTOOL}" sign /sha1 ${THUMBPRINT} /fd SHA256 "${ARTEFACT}"' = 0
!finalize '"${SIGNTOOL}" sign /sha1 ${THUMBPRINT} /fd SHA256 "%1"' = 0
!uninstfinalize '"${SIGNTOOL}" sign /sha1 ${THUMBPRINT} /fd SHA256 "%1"' = 0

!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "${RESOURCES}\header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${RESOURCES}\welcome.bmp"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE}"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"

  SetOutPath "$INSTDIR"
  File "${ARTEFACT}"

  System::Call 'advapi32::RegGetValueW(p ${HKEY_CURRENT_USER}, w "${ENVIRONMENTKEY}", w "${PATHVALUE}", i ${PATH_READ_FLAGS}, *i .r1, p 0, *i .r2) i .r0'

  ${If} $0 = ${ERROR_FILE_NOT_FOUND}
    WriteRegExpandStr HKCU "${ENVIRONMENTKEY}" "${PATHVALUE}" ""
    StrCpy $0 ${ERROR_SUCCESS}
    StrCpy $1 ${REG_EXPAND_SZ}
    StrCpy $2 ${WCHAR_SIZE}
  ${EndIf}

  ${If} $0 = ${ERROR_SUCCESS}
    StrLen $3 "$INSTDIR"
    IntOp $3 $3 + ${PATH_SEPARATOR_COUNT}
    IntOp $3 $3 * ${WCHAR_SIZE}
    IntOp $3 $3 + $2
    System::Alloc $3
    Pop $4
    IntOp $5 $4 + ${WCHAR_SIZE}
    System::Call 'kernel32::lstrcpyW(p r4, w ";")'
    System::Call 'advapi32::RegGetValueW(p ${HKEY_CURRENT_USER}, w "${ENVIRONMENTKEY}", w "${PATHVALUE}", i ${PATH_READ_FLAGS}, p 0, p r5, *i r2) i .r0'

    ${If} $0 = ${ERROR_SUCCESS}
      System::Call 'kernel32::lstrcatW(p r4, w ";")'
      System::Call 'shlwapi::StrStrIW(p r4, w ";$INSTDIR;") p .r3'

      ${If} $3 P= 0
        System::Call 'kernel32::lstrcatW(p r4, w "$INSTDIR")'
        System::Call 'kernel32::lstrlenW(p r5) i .r3'
        IntOp $3 $3 * ${WCHAR_SIZE}
        IntOp $3 $3 + ${WCHAR_SIZE}
        System::Call 'advapi32::RegSetKeyValueW(p ${HKEY_CURRENT_USER}, w "${ENVIRONMENTKEY}", w "${PATHVALUE}", i r1, p r5, i r3) i .r0'
        SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:${ENVIRONMENTKEY}" /TIMEOUT=${BROADCAST_TIMEOUT_MS}
      ${EndIf}
    ${EndIf}

    System::Free $4
  ${EndIf}

  ${If} $0 <> ${ERROR_SUCCESS}
    Abort "PATH update failed: $0"
  ${EndIf}

  SetShellVarContext current
  SetOutPath "$DOCUMENTS\${COMPANY}\Uninstaller"
  WriteUninstaller "$DOCUMENTS\${COMPANY}\Uninstaller\${PRODUCT} Uninstaller.exe"

  WriteRegStr HKCU "${UNINSTALLKEY}" "DisplayName" "${PRODUCT}"
  WriteRegStr HKCU "${UNINSTALLKEY}" "UninstallString" '"$DOCUMENTS\${COMPANY}\Uninstaller\${PRODUCT} Uninstaller.exe"'
  WriteRegStr HKCU "${UNINSTALLKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALLKEY}" "Publisher" "${PUBLISHER}"
  WriteRegDWORD HKCU "${UNINSTALLKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALLKEY}" "NoRepair" 1

SectionEnd

Section "Uninstall"

  Delete "$PROFILE\.local\bin\whatdbg.exe"
  DeleteRegKey HKCU "${UNINSTALLKEY}"

  SetShellVarContext current
  Delete "$DOCUMENTS\${COMPANY}\Uninstaller\${PRODUCT} Uninstaller.exe"
  RMDir "$DOCUMENTS\${COMPANY}\Uninstaller"

SectionEnd
