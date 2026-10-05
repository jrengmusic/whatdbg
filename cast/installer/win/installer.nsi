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

!define PRODUCT "whatdbg"
!define VERSION "0.1.0"
!define PUBLISHER "Jubilant Research of Eclectic Novelty Generation"
!define COMPANY "JRENG"
!define THUMBPRINT "8274AC1EA9DEA10AEFF7FD683D5E2BC4B5FAAAF1"
!define UNINSTALLKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT}"

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

  EnVar::SetHKCU
  EnVar::AddValue "Path" "$INSTDIR"
  Pop $0

  ${If} $0 <> 0
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
