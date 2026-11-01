; build/installer.nsh - Кастомный NSIS скрипт для установщика NanoDecompiler v2.0.2
; Добавляет интеграцию с Проводником Windows (контекстное меню для .jar, .class, .so)
; и настраивает русскоязычные баннеры и ярлыки.

!macro customHeader
  !system "echo Custom NSIS Header applied for NanoDecompiler"
!macroend

!macro customInstall
  DetailPrint "Регистрация ассоциаций файлов и контекстного меню..."

  ; Контекстное меню для .jar файлов: "Декомпилировать в NanoDecompiler"
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.jar\shell\NanoDecompiler" "" "Декомпилировать в NanoDecompiler"
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.jar\shell\NanoDecompiler" "Icon" "$INSTDIR\NanoDecompiler.exe,0"
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.jar\shell\NanoDecompiler\command" "" '"$INSTDIR\NanoDecompiler.exe" "%1"'

  ; Контекстное меню для .class файлов: "Просмотреть байткод в NanoDecompiler"
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.class\shell\NanoDecompiler" "" "Открыть байткод в NanoDecompiler"
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.class\shell\NanoDecompiler" "Icon" "$INSTDIR\NanoDecompiler.exe,0"
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.class\shell\NanoDecompiler\command" "" '"$INSTDIR\NanoDecompiler.exe" "%1"'

  ; Контекстное меню для .so и .dll файлов: "Реверс JNI / JNA в NanoDecompiler"
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.so\shell\NanoDecompiler" "" "Реверс нативной библиотеки в NanoDecompiler"
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.so\shell\NanoDecompiler" "Icon" "$INSTDIR\NanoDecompiler.exe,0"
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.so\shell\NanoDecompiler\command" "" '"$INSTDIR\NanoDecompiler.exe" "%1"'

  ; Регистрация в App Paths для прямого вызова из диалога "Выполнить" (Win + R)
  ; Позволяет писать: NanoDecompiler, Decompiler, ndecompiler, ndecomp
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\NanoDecompiler.exe" "" "$INSTDIR\NanoDecompiler.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\NanoDecompiler.exe" "Path" "$INSTDIR"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\Decompiler.exe" "" "$INSTDIR\NanoDecompiler.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\Decompiler.exe" "Path" "$INSTDIR"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecompiler.exe" "" "$INSTDIR\NanoDecompiler.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecompiler.exe" "Path" "$INSTDIR"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecomp.exe" "" "$INSTDIR\NanoDecompiler.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecomp.exe" "Path" "$INSTDIR"

  ; Создание батников быстрого запуска в WindowsApps (находится в системном %PATH% по умолчанию)
  ; Позволяет запускать из любого окна cmd / powershell без указания расширения
  FileOpen $0 "$LOCALAPPDATA\Microsoft\WindowsApps\NanoDecompiler.cmd" w
  FileWrite $0 '@start "" "$INSTDIR\NanoDecompiler.exe" %*$\r$\n'
  FileClose $0

  FileOpen $0 "$LOCALAPPDATA\Microsoft\WindowsApps\Decompiler.cmd" w
  FileWrite $0 '@start "" "$INSTDIR\NanoDecompiler.exe" %*$\r$\n'
  FileClose $0

  FileOpen $0 "$LOCALAPPDATA\Microsoft\WindowsApps\ndecompiler.cmd" w
  FileWrite $0 '@start "" "$INSTDIR\NanoDecompiler.exe" %*$\r$\n'
  FileClose $0

  FileOpen $0 "$LOCALAPPDATA\Microsoft\WindowsApps\ndecomp.cmd" w
  FileWrite $0 '@start "" "$INSTDIR\NanoDecompiler.exe" %*$\r$\n'
  FileClose $0

  DetailPrint "Очистка устаревших временных файлов..."
  RMDir /r "$LOCALAPPDATA\Temp\nano_decompiler_old"
!macroend

!macro customUnInstall
  DetailPrint "Удаление ассоциаций контекстного меню и алиасов быстрого запуска..."
  DeleteRegKey HKCU "Software\Classes\SystemFileAssociations\.jar\shell\NanoDecompiler"
  DeleteRegKey HKCU "Software\Classes\SystemFileAssociations\.class\shell\NanoDecompiler"
  DeleteRegKey HKCU "Software\Classes\SystemFileAssociations\.so\shell\NanoDecompiler"

  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\NanoDecompiler.exe"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\Decompiler.exe"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecompiler.exe"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\ndecomp.exe"

  Delete "$LOCALAPPDATA\Microsoft\WindowsApps\NanoDecompiler.cmd"
  Delete "$LOCALAPPDATA\Microsoft\WindowsApps\Decompiler.cmd"
  Delete "$LOCALAPPDATA\Microsoft\WindowsApps\ndecompiler.cmd"
  Delete "$LOCALAPPDATA\Microsoft\WindowsApps\ndecomp.cmd"
!macroend
