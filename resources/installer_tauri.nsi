; ==============================================================================
; NanoDecompiler Tauri v2 Windows Installer (NSIS)
; Разработчик: NanoDev (t.me/WineSquad, t.me/radoqi)
; ==============================================================================

Unicode true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!define PRODUCT_NAME "NanoDecompiler"
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "2.0.3"
!endif
!define PRODUCT_PUBLISHER "NanoDev"
!define PRODUCT_WEB_SITE "https://github.com/nanodev1488"
!define PRODUCT_EXE "NanoDecompiler.exe"
!define UNINST_EXE "Uninstall NanoDecompiler.exe"
!define REG_UNINSTALL "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

!include "MUI2.nsh"
!include "FileFunc.nsh"

; Иконки
!define MUI_ICON "..\resources\engine\icon.ico"
!define MUI_UNICON "..\resources\engine\icon.ico"

; Настройки интерфейса Modern UI
!define MUI_ABORTWARNING
!define MUI_HEADERIMAGE
!define MUI_WELCOMEFINISHPAGE_BITMAP_NOSTRETCH

; Страницы установщика
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${PRODUCT_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Запустить ${PRODUCT_NAME} сейчас"
!insertmacro MUI_PAGE_FINISH

; Страницы деинсталлятора
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; Языки
!insertmacro MUI_LANGUAGE "Russian"

Name "${PRODUCT_NAME} v${PRODUCT_VERSION}"
OutFile "..\NanoDecompiler-Tauri-Setup-${PRODUCT_VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\${PRODUCT_NAME}"
InstallDirRegKey HKCU "Software\${PRODUCT_NAME}" "InstallDir"

Section "MainSection" SEC01
    SetOutPath "$INSTDIR"
    SetOverwrite on

    ; Основной исполняемый файл Tauri v2
    File "/oname=${PRODUCT_EXE}" "..\tauri-v2\src-tauri\target\release\nanodecompiler-tauri.exe"

    ; Нативные движки C++ и Rust
    File "..\resources\engine\NanoDecompilerCLI.exe"
    File "..\resources\engine\nanofastscan.exe"
    File "..\resources\engine\icon.ico"

    ; Вспомогательный каталог ресурсов
    CreateDirectory "$INSTDIR\resources\engine"
    SetOutPath "$INSTDIR\resources\engine"
    File "..\resources\engine\NanoDecompilerCLI.exe"
    File "..\resources\engine\nanofastscan.exe"
    File "..\resources\engine\icon.ico"
    SetOutPath "$INSTDIR"

    ; Запись деинсталлятора
    WriteUninstaller "$INSTDIR\${UNINST_EXE}"

    ; Ярлыки в меню Пуск
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\icon.ico" 0
    CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}\Удалить ${PRODUCT_NAME}.lnk" "$INSTDIR\${UNINST_EXE}" "" "$INSTDIR\${UNINST_EXE}" 0

    ; Ярлык на Рабочем столе
    CreateShortcut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\icon.ico" 0

    ; Контекстное меню Проводника Windows (.jar, .class, .so, .dll)
    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.jar\shell\NanoDecompiler" "" "Декомпилировать в NanoDecompiler"
    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.jar\shell\NanoDecompiler" "Icon" "$INSTDIR\${PRODUCT_EXE},0"
    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.jar\shell\NanoDecompiler\command" "" '"$INSTDIR\${PRODUCT_EXE}" "%1"'

    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.class\shell\NanoDecompiler" "" "Открыть байткод в NanoDecompiler"
    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.class\shell\NanoDecompiler" "Icon" "$INSTDIR\${PRODUCT_EXE},0"
    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.class\shell\NanoDecompiler\command" "" '"$INSTDIR\${PRODUCT_EXE}" "%1"'

    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.so\shell\NanoDecompiler" "" "Реверс нативной библиотеки в NanoDecompiler"
    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.so\shell\NanoDecompiler" "Icon" "$INSTDIR\${PRODUCT_EXE},0"
    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.so\shell\NanoDecompiler\command" "" '"$INSTDIR\${PRODUCT_EXE}" "%1"'

    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.dll\shell\NanoDecompiler" "" "Реверс нативной библиотеки в NanoDecompiler"
    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.dll\shell\NanoDecompiler" "Icon" "$INSTDIR\${PRODUCT_EXE},0"
    WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.dll\shell\NanoDecompiler\command" "" '"$INSTDIR\${PRODUCT_EXE}" "%1"'

    ; Регистрация в App Paths (Win + R диалог Выполнить)
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\NanoDecompiler.exe" "" "$INSTDIR\${PRODUCT_EXE}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\NanoDecompiler.exe" "Path" "$INSTDIR"

    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\Decompiler.exe" "" "$INSTDIR\${PRODUCT_EXE}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\Decompiler.exe" "Path" "$INSTDIR"

    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecompiler.exe" "" "$INSTDIR\${PRODUCT_EXE}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecompiler.exe" "Path" "$INSTDIR"

    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecomp.exe" "" "$INSTDIR\${PRODUCT_EXE}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecomp.exe" "Path" "$INSTDIR"

    ; Создание .cmd оберток в WindowsApps (находится в системном %PATH% по умолчанию)
    CreateDirectory "$LOCALAPPDATA\Microsoft\WindowsApps"
    FileOpen $0 "$LOCALAPPDATA\Microsoft\WindowsApps\NanoDecompiler.cmd" w
    FileWrite $0 '@start "" "$INSTDIR\${PRODUCT_EXE}" %*$\r$\n'
    FileClose $0

    FileOpen $0 "$LOCALAPPDATA\Microsoft\WindowsApps\Decompiler.cmd" w
    FileWrite $0 '@start "" "$INSTDIR\${PRODUCT_EXE}" %*$\r$\n'
    FileClose $0

    FileOpen $0 "$LOCALAPPDATA\Microsoft\WindowsApps\ndecompiler.cmd" w
    FileWrite $0 '@start "" "$INSTDIR\${PRODUCT_EXE}" %*$\r$\n'
    FileClose $0

    FileOpen $0 "$LOCALAPPDATA\Microsoft\WindowsApps\ndecomp.cmd" w
    FileWrite $0 '@start "" "$INSTDIR\${PRODUCT_EXE}" %*$\r$\n'
    FileClose $0

    ; Запись информации об установке в Установка и удаление программ
    WriteRegStr HKCU "${REG_UNINSTALL}" "DisplayName" "${PRODUCT_NAME} (Tauri v2 Native Edition)"
    WriteRegStr HKCU "${REG_UNINSTALL}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKCU "${REG_UNINSTALL}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKCU "${REG_UNINSTALL}" "DisplayIcon" "$INSTDIR\${PRODUCT_EXE},0"
    WriteRegStr HKCU "${REG_UNINSTALL}" "UninstallString" '"$INSTDIR\${UNINST_EXE}"'
    WriteRegStr HKCU "${REG_UNINSTALL}" "QuietUninstallString" '"$INSTDIR\${UNINST_EXE}" /S'
    WriteRegStr HKCU "${REG_UNINSTALL}" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKCU "${REG_UNINSTALL}" "NoModify" 1
    WriteRegDWORD HKCU "${REG_UNINSTALL}" "NoRepair" 1
SectionEnd

Section "Uninstall"
    ; Удаление ярлыков
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\Удалить ${PRODUCT_NAME}.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"

    ; Удаление контекстного меню
    DeleteRegKey HKCU "Software\Classes\SystemFileAssociations\.jar\shell\NanoDecompiler"
    DeleteRegKey HKCU "Software\Classes\SystemFileAssociations\.class\shell\NanoDecompiler"
    DeleteRegKey HKCU "Software\Classes\SystemFileAssociations\.so\shell\NanoDecompiler"
    DeleteRegKey HKCU "Software\Classes\SystemFileAssociations\.dll\shell\NanoDecompiler"

    ; Удаление App Paths
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\NanoDecompiler.exe"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\Decompiler.exe"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecompiler.exe"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecomp.exe"

    ; Удаление cmd оберток
    Delete "$LOCALAPPDATA\Microsoft\WindowsApps\NanoDecompiler.cmd"
    Delete "$LOCALAPPDATA\Microsoft\WindowsApps\Decompiler.cmd"
    Delete "$LOCALAPPDATA\Microsoft\WindowsApps\ndecompiler.cmd"
    Delete "$LOCALAPPDATA\Microsoft\WindowsApps\ndecomp.cmd"

    ; Удаление записи из реестра удаления
    DeleteRegKey HKCU "${REG_UNINSTALL}"
    DeleteRegKey HKCU "Software\${PRODUCT_NAME}"

    ; Удаление установленных файлов
    Delete "$INSTDIR\${PRODUCT_EXE}"
    Delete "$INSTDIR\NanoDecompilerCLI.exe"
    Delete "$INSTDIR\nanofastscan.exe"
    Delete "$INSTDIR\icon.ico"
    Delete "$INSTDIR\resources\engine\NanoDecompilerCLI.exe"
    Delete "$INSTDIR\resources\engine\nanofastscan.exe"
    Delete "$INSTDIR\resources\engine\icon.ico"
    RMDir "$INSTDIR\resources\engine"
    RMDir "$INSTDIR\resources"
    Delete "$INSTDIR\${UNINST_EXE}"
    RMDir "$INSTDIR"
SectionEnd
