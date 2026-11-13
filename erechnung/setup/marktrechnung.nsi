; Setup für Marktrechnung unter Windows (gebaut mit NSIS, https://nsis.sourceforge.io).
;
; Richtet das Programm nur für den angemeldeten Benutzer ein - dafür braucht
; es keine Administratorrechte. Rechnungen und Daten (Dokumente\Marktrechnung)
; werden vom Setup und vom Entfernen nicht angefasst.
;
; Aufruf über bauen.sh:
;   makensis -INPUTCHARSET UTF8 -DVERSION=1.0.0 -DEXE=/pfad/Marktrechnung.exe \
;            -DLIESMICH=/pfad/LIESMICH.txt -DAUSGABE=/pfad/Setup.exe setup/marktrechnung.nsi

Unicode true
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!ifndef VIERSTELLIG
  !define VIERSTELLIG "${VERSION}.0"
!endif
!ifndef EXE
  !define EXE "..\dist\Marktrechnung.exe"
!endif
!ifndef LIESMICH
  !define LIESMICH "..\LIESMICH.txt"
!endif
!ifndef AUSGABE
  !define AUSGABE "..\dist\Marktrechnung-Setup-${VERSION}.exe"
!endif

!define PROGRAMM "Marktrechnung.exe"
!define ENTFERNER "Marktrechnung entfernen.exe"
!define SCHLUESSEL "Software\Marktrechnung"
!define DEINSTALLATION "Software\Microsoft\Windows\CurrentVersion\Uninstall\Marktrechnung"

Name "Marktrechnung"
OutFile "${AUSGABE}"
InstallDir "$LOCALAPPDATA\Programs\Marktrechnung"
InstallDirRegKey HKCU "${SCHLUESSEL}" "Installationsordner"
RequestExecutionLevel user
ManifestDPIAware true
BrandingText "Marktrechnung ${VERSION}"
ShowInstDetails nevershow
ShowUninstDetails nevershow

; ---------------------------------------------------------------- Seiten

!define MUI_ICON "marktrechnung.ico"
!define MUI_UNICON "marktrechnung.ico"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "Marktrechnung einrichten"
!define MUI_WELCOMEPAGE_TEXT "Dieses Setup richtet Marktrechnung ${VERSION} auf diesem Rechner ein: E-Rechnungen für REWE, EDEKA und den Handel erstellen, prüfen und verschicken.$\r$\n$\r$\nDas Programm wird nur für Ihr Benutzerkonto eingerichtet. Im Startmenü und auf dem Desktop entsteht ein Symbol.$\r$\n$\r$\nIst bereits eine ältere Fassung vorhanden, wird sie ersetzt. Ihre Rechnungen und Daten bleiben dabei erhalten."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_TITLE "Marktrechnung ist eingerichtet"
!define MUI_FINISHPAGE_TEXT "Das Programm startet künftig über das Symbol „Marktrechnung“ auf dem Desktop oder im Startmenü. Es öffnet sich im Browser, läuft aber nur auf diesem Rechner.$\r$\n$\r$\nRechnungen und Daten liegen im Ordner Dokumente\Marktrechnung."
!define MUI_FINISHPAGE_RUN "$INSTDIR\${PROGRAMM}"
!define MUI_FINISHPAGE_RUN_TEXT "Marktrechnung jetzt starten"
!insertmacro MUI_PAGE_FINISH

!define MUI_UNCONFIRMPAGE_TEXT_TOP "Marktrechnung wird von diesem Rechner entfernt. Ihre Rechnungen und Daten im Ordner Dokumente\Marktrechnung bleiben erhalten. Das gespeicherte E-Mail-Passwort wird gelöscht."
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "German"

VIProductVersion "${VIERSTELLIG}"
VIAddVersionKey /LANG=${LANG_GERMAN} "ProductName" "Marktrechnung"
VIAddVersionKey /LANG=${LANG_GERMAN} "FileDescription" "Setup für Marktrechnung"
VIAddVersionKey /LANG=${LANG_GERMAN} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_GERMAN} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_GERMAN} "LegalCopyright" "Freie Software"

; ------------------------------------------------------------ Einrichten

Section "Marktrechnung"
  SectionIn RO

  ; Eine laufende Fassung beenden, sonst lässt Windows die Programmdatei
  ; nicht ersetzen.
  IfFileExists "$INSTDIR\${PROGRAMM}" 0 frei
    ExecWait '"$INSTDIR\${PROGRAMM}" -beenden'
  nochmal:
    ClearErrors
    Delete "$INSTDIR\${PROGRAMM}"
    IfErrors 0 frei
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Marktrechnung läuft noch und kann deshalb nicht ersetzt werden.$\r$\n$\r$\nBitte im Programm unten links „Programm beenden“ wählen und dann auf „Wiederholen“ klicken." /SD IDCANCEL IDRETRY nochmal
      Abort "Das Setup wurde abgebrochen, weil Marktrechnung noch läuft."
  frei:

  SetOutPath "$INSTDIR"
  File "/oname=${PROGRAMM}" "${EXE}"
  File "marktrechnung.ico"
  File "/oname=LIESMICH.txt" "${LIESMICH}"
  WriteUninstaller "$INSTDIR\${ENTFERNER}"

  CreateShortcut "$SMPROGRAMS\Marktrechnung.lnk" "$INSTDIR\${PROGRAMM}" "" "$INSTDIR\marktrechnung.ico"
  CreateShortcut "$DESKTOP\Marktrechnung.lnk" "$INSTDIR\${PROGRAMM}" "" "$INSTDIR\marktrechnung.ico"

  ; Eintrag unter Einstellungen > Apps, damit sich das Programm dort
  ; wieder entfernen lässt.
  WriteRegStr HKCU "${SCHLUESSEL}" "Installationsordner" "$INSTDIR"
  WriteRegStr HKCU "${DEINSTALLATION}" "DisplayName" "Marktrechnung"
  WriteRegStr HKCU "${DEINSTALLATION}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${DEINSTALLATION}" "DisplayIcon" "$INSTDIR\marktrechnung.ico"
  WriteRegStr HKCU "${DEINSTALLATION}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${DEINSTALLATION}" "UninstallString" '"$INSTDIR\${ENTFERNER}"'
  WriteRegStr HKCU "${DEINSTALLATION}" "QuietUninstallString" '"$INSTDIR\${ENTFERNER}" /S'
  WriteRegDWORD HKCU "${DEINSTALLATION}" "NoModify" 1
  WriteRegDWORD HKCU "${DEINSTALLATION}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${DEINSTALLATION}" "EstimatedSize" "$0"
SectionEnd

; -------------------------------------------------------------- Entfernen

Section "Uninstall"
  IfFileExists "$INSTDIR\${PROGRAMM}" 0 +2
    ExecWait '"$INSTDIR\${PROGRAMM}" -beenden'

  ; Nur die eigenen Dateien löschen, nie den ganzen Ordner: Hätte jemand
  ; das Programm in seinen Datenordner gelegt, blieben die Rechnungen stehen.
  Delete "$INSTDIR\${PROGRAMM}"
  Delete "$INSTDIR\marktrechnung.ico"
  Delete "$INSTDIR\LIESMICH.txt"
  Delete "$INSTDIR\${ENTFERNER}"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\Marktrechnung.lnk"
  Delete "$DESKTOP\Marktrechnung.lnk"
  DeleteRegKey HKCU "${DEINSTALLATION}"
  DeleteRegKey HKCU "${SCHLUESSEL}"

  ; Einstellungen des Programms samt gespeichertem E-Mail-Passwort. Die
  ; Rechnungen und Stammdaten liegen woanders und bleiben erhalten.
  RMDir /r "$APPDATA\Marktrechnung"
SectionEnd
