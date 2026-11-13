; Setup für Marktrechnung unter Windows (gebaut mit NSIS, https://nsis.sourceforge.io).
;
; Richtet das Programm nur für den angemeldeten Benutzer ein - dafür braucht
; es keine Administratorrechte. Rechnungen und Daten (Dokumente\Marktrechnung)
; werden vom Setup und vom Entfernen nicht angefasst.
;
; Aufruf über bauen.sh:
;   makensis -INPUTCHARSET UTF8 -DVERSION=1.0.0 -DEXE=/pfad/Marktrechnung.exe \
;            -DLIESMICH=/pfad/LIESMICH.txt -DANLEITUNG=/pfad/Anleitung.pdf -DLIZENZEN=/pfad/LIZENZEN.txt \
;            -DAUSGABE=/pfad/Setup.exe setup/marktrechnung.nsi

Unicode true
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!ifndef VIERSTELLIG
  !define VIERSTELLIG "${VERSION}.0"
!endif
!ifndef EXE
  !define EXE "..\dist\Marktrechnung.exe"
!endif
!ifndef LIZENZEN
  !error "Bitte über bauen.sh bauen: -DLIZENZEN=/pfad/LIZENZEN.txt fehlt (erzeugt von lizenzen.sh)"
!endif
!ifndef ANLEITUNG
  !define ANLEITUNG "..\docs\Anleitung.pdf"
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
;
; Der Assistent hat drei Schritte: Willkommen, Einrichten, Fertig. Nach dem
; Zielordner fragt er nicht - wer einen anderen möchte, ruft das Setup mit
; /D=Ordner auf.

Caption "Marktrechnung einrichten"
UninstallCaption "Marktrechnung entfernen"
; Keine Zusätze wie ": Installation" hinter dem Fenstertitel.
SubCaption 3 " "
SubCaption 4 " "
UninstallSubCaption 0 " "
UninstallSubCaption 1 " "
UninstallSubCaption 2 " "

!define GRUEN "1E6F52"
!define MUI_ICON "marktrechnung.ico"
!define MUI_UNICON "marktrechnung.ico"
!define MUI_ABORTWARNING
!define MUI_ABORTWARNING_TEXT "Möchten Sie die Einrichtung von Marktrechnung wirklich abbrechen?"

; Bilder: links auf der ersten und letzten Seite der Marktstand, auf den
; übrigen Seiten oben rechts das Kopfbild. Die Vorlagen und das Werkzeug zum
; Erzeugen liegen im Ordner bilder.
!define MUI_WELCOMEFINISHPAGE_BITMAP "bilder/willkommen-100.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "bilder/kopf-100.bmp"
!define MUI_CUSTOMFUNCTION_GUIINIT BilderLaden
!define MUI_CUSTOMFUNCTION_UNGUIINIT un.BilderLaden

Var Bildstufe    ; mit dem Schalter /BILD=150 erzwungene Bildgröße (zum Testen), sonst leer
Var Titelschrift ; große Schrift der Überschriften
Var Textschrift  ; Schrift der Texte

!define MUI_WELCOMEPAGE_TITLE "Herzlich willkommen!"
!define MUI_WELCOMEPAGE_TEXT "Schön, dass Sie da sind. Marktrechnung hilft Ihnen, E-Rechnungen für REWE, EDEKA und den Handel zu schreiben, zu prüfen und zu verschicken.$\r$\n$\r$\n✓  In einer Minute eingerichtet$\r$\n✓  Keine Administratorrechte nötig$\r$\n✓  Symbol auf dem Desktop und im Startmenü$\r$\n$\r$\nEin Klick auf „Installieren“ genügt."
!define MUI_PAGE_CUSTOMFUNCTION_SHOW WillkommenZeigen
!insertmacro MUI_PAGE_WELCOME

!define MUI_PAGE_HEADER_TEXT "Einen Augenblick bitte …"
!define MUI_PAGE_HEADER_SUBTEXT "Marktrechnung wird auf diesem Rechner eingerichtet."
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_TITLE "Geschafft!"
!define MUI_FINISHPAGE_TEXT "Marktrechnung ist eingerichtet und startklar.$\r$\n$\r$\nSie starten das Programm ab jetzt über das Symbol „Marktrechnung“ auf dem Desktop oder im Startmenü.$\r$\n$\r$\nIhre Rechnungen und Daten liegen im Ordner Dokumente\Marktrechnung.$\r$\n$\r$\nViel Erfolg mit Ihren E-Rechnungen!"
!define MUI_FINISHPAGE_TEXT_LARGE
!define MUI_FINISHPAGE_NOREBOOTSUPPORT
!define MUI_FINISHPAGE_RUN "$INSTDIR\${PROGRAMM}"
!define MUI_FINISHPAGE_RUN_TEXT "Marktrechnung jetzt starten"
!define MUI_PAGE_CUSTOMFUNCTION_SHOW FertigZeigen
!insertmacro MUI_PAGE_FINISH

!define MUI_PAGE_HEADER_TEXT "Marktrechnung entfernen"
!define MUI_PAGE_HEADER_SUBTEXT "Ihre Rechnungen und Daten bleiben erhalten."
!define MUI_UNCONFIRMPAGE_TEXT_TOP "Schade, dass Sie gehen! Marktrechnung wird von diesem Rechner entfernt. Ihre Rechnungen und Daten im Ordner Dokumente\Marktrechnung bleiben erhalten; nur das gespeicherte E-Mail-Passwort wird gelöscht."
!insertmacro MUI_UNPAGE_CONFIRM
!define MUI_PAGE_HEADER_TEXT "Einen Augenblick bitte …"
!define MUI_PAGE_HEADER_SUBTEXT "Marktrechnung wird entfernt."
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "German"

; ------------------------------------------------- Bilder und Schriften
;
; Windows vergrößert auf vielen Bildschirmen alles auf 125 bis 200 %. Ein
; einzelnes Bild würde dabei grob aufgeblasen; deshalb liegt jedes Bild in
; fünf Größen bei, und das Setup nimmt die, die zum Fenster passt.

; Stufe: $0 = Bild-Steuerelement, $1 = Höhe des Bildes bei 100 %.
; Liefert in $0 die passende Stufe (100, 125, 150, 175 oder 200).
!macro StufeFunktion UN
Function ${UN}Stufe
  System::Call '*(i 0, i 0, i 0, i 0) p .r2'
  System::Call 'user32::GetClientRect(p r0, p r2)'
  System::Call '*$2(i, i, i, i .r3)'
  System::Free $2
  IntOp $3 $3 * 100
  IntOp $3 $3 / $1
  StrCpy $0 100
  ${If} $3 >= 188
    StrCpy $0 200
  ${ElseIf} $3 >= 163
    StrCpy $0 175
  ${ElseIf} $3 >= 138
    StrCpy $0 150
  ${ElseIf} $3 >= 113
    StrCpy $0 125
  ${EndIf}
  ${If} $Bildstufe != ""
    StrCpy $0 $Bildstufe
  ${EndIf}
FunctionEnd
!macroend
!insertmacro StufeFunktion ""
!insertmacro StufeFunktion "un."

!macro KopfbildSetzen UN
  File "/oname=$PLUGINSDIR\kopf-125.bmp" "bilder/kopf-125.bmp"
  File "/oname=$PLUGINSDIR\kopf-150.bmp" "bilder/kopf-150.bmp"
  File "/oname=$PLUGINSDIR\kopf-175.bmp" "bilder/kopf-175.bmp"
  File "/oname=$PLUGINSDIR\kopf-200.bmp" "bilder/kopf-200.bmp"
  GetDlgItem $0 $HWNDPARENT 1046
  StrCpy $1 57
  Call ${UN}Stufe
  ${If} $0 != 100
  ${AndIf} ${FileExists} "$PLUGINSDIR\kopf-$0.bmp"
    SetBrandingImage /IMGID=1046 /RESIZETOFIT "$PLUGINSDIR\kopf-$0.bmp"
  ${EndIf}
!macroend

Function BilderLaden
  InitPluginsDir
  File "/oname=$PLUGINSDIR\willkommen-125.bmp" "bilder/willkommen-125.bmp"
  File "/oname=$PLUGINSDIR\willkommen-150.bmp" "bilder/willkommen-150.bmp"
  File "/oname=$PLUGINSDIR\willkommen-175.bmp" "bilder/willkommen-175.bmp"
  File "/oname=$PLUGINSDIR\willkommen-200.bmp" "bilder/willkommen-200.bmp"
  !insertmacro KopfbildSetzen ""
  CreateFont $Titelschrift "Segoe UI" 17 700
  CreateFont $Textschrift "Segoe UI" 10 400
FunctionEnd

Function un.BilderLaden
  InitPluginsDir
  !insertmacro KopfbildSetzen "un."
FunctionEnd

Function .onInit
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/BILD=" $Bildstufe
  ${If} ${Errors}
    StrCpy $Bildstufe ""
  ${EndIf}
FunctionEnd

; Seitenbild in der Größe, die zum Fenster passt.
!macro SeitenbildSetzen ELEMENT GRIFF
  StrCpy $0 ${ELEMENT}
  StrCpy $1 314
  Call Stufe
  ${If} $0 != 100
  ${AndIf} ${FileExists} "$PLUGINSDIR\willkommen-$0.bmp"
    ${NSD_FreeImage} ${GRIFF}
    ${NSD_SetStretchedImage} ${ELEMENT} "$PLUGINSDIR\willkommen-$0.bmp" ${GRIFF}
  ${EndIf}
!macroend

; Lage und Größe eines Steuerelements, in den Einheiten des Dialogs.
!macro Lege SEITE ELEMENT X Y BREITE HOEHE
  System::Call '*(i ${X}, i ${Y}, i ${BREITE}, i ${HOEHE}) p .r9'
  System::Call 'user32::MapDialogRect(p ${SEITE}, p r9)'
  System::Call '*$9(i .r5, i .r6, i .r7, i .r8)'
  System::Free $9
  System::Call 'user32::MoveWindow(p ${ELEMENT}, i r5, i r6, i r7, i r8, i 1)'
!macroend

Function WillkommenZeigen
  !insertmacro SeitenbildSetzen $mui.WelcomePage.Image $mui.WelcomePage.Image.Bitmap
  SendMessage $mui.WelcomePage.Title ${WM_SETFONT} $Titelschrift 0
  SetCtlColors $mui.WelcomePage.Title "${GRUEN}" "${MUI_BGCOLOR}"
  SendMessage $mui.WelcomePage.Text ${WM_SETFONT} $Textschrift 0
  !insertmacro Lege $mui.WelcomePage $mui.WelcomePage.Title 122 14 190 24
  !insertmacro Lege $mui.WelcomePage $mui.WelcomePage.Text 122 46 186 140
  ; Ist das Programm schon da, bringt das Setup es auf den neuesten Stand.
  ${If} ${FileExists} "$INSTDIR\${PROGRAMM}"
    SendMessage $mui.WelcomePage.Title ${WM_SETTEXT} 0 "STR:Schön, Sie wiederzusehen!"
    SendMessage $mui.WelcomePage.Text ${WM_SETTEXT} 0 "STR:Marktrechnung ist auf diesem Rechner schon eingerichtet. Dieses Setup bringt das Programm auf den neuesten Stand (Version ${VERSION}).$\r$\n$\r$\n✓  Ihre Daten bleiben vollständig erhalten$\r$\n✓  Das laufende Programm wird dafür beendet$\r$\n$\r$\nEin Klick auf „Installieren“ genügt."
  ${EndIf}
FunctionEnd

Function FertigZeigen
  !insertmacro SeitenbildSetzen $mui.FinishPage.Image $mui.FinishPage.Image.Bitmap
  SendMessage $mui.FinishPage.Title ${WM_SETFONT} $Titelschrift 0
  SetCtlColors $mui.FinishPage.Title "${GRUEN}" "${MUI_BGCOLOR}"
  SendMessage $mui.FinishPage.Text ${WM_SETFONT} $Textschrift 0
  SendMessage $mui.FinishPage.Run ${WM_SETFONT} $Textschrift 0
  !insertmacro Lege $mui.FinishPage $mui.FinishPage.Title 122 14 190 24
  !insertmacro Lege $mui.FinishPage $mui.FinishPage.Text 122 46 186 106
  !insertmacro Lege $mui.FinishPage $mui.FinishPage.Run 122 160 186 14
FunctionEnd

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
  File "/oname=Anleitung.pdf" "${ANLEITUNG}"
  File "/oname=LIZENZEN.txt" "${LIZENZEN}"
  WriteUninstaller "$INSTDIR\${ENTFERNER}"

  CreateShortcut "$SMPROGRAMS\Marktrechnung.lnk" "$INSTDIR\${PROGRAMM}" "" "$INSTDIR\marktrechnung.ico"
  CreateShortcut "$DESKTOP\Marktrechnung.lnk" "$INSTDIR\${PROGRAMM}" "" "$INSTDIR\marktrechnung.ico"
  CreateShortcut "$SMPROGRAMS\Marktrechnung Anleitung.lnk" "$INSTDIR\Anleitung.pdf"

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
  Delete "$INSTDIR\Anleitung.pdf"
  Delete "$INSTDIR\LIZENZEN.txt"
  Delete "$INSTDIR\${ENTFERNER}"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\Marktrechnung.lnk"
  Delete "$SMPROGRAMS\Marktrechnung Anleitung.lnk"
  Delete "$DESKTOP\Marktrechnung.lnk"
  DeleteRegKey HKCU "${DEINSTALLATION}"
  DeleteRegKey HKCU "${SCHLUESSEL}"

  ; Einstellungen des Programms samt gespeichertem E-Mail-Passwort sowie
  ; das Profil des Programmfensters. Die Rechnungen und Stammdaten liegen
  ; woanders und bleiben erhalten.
  RMDir /r "$APPDATA\Marktrechnung"
  RMDir /r "$LOCALAPPDATA\Marktrechnung"
SectionEnd
